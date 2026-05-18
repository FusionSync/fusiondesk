#include <cassert>
#include <memory>
#include <string>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/modules/clipboard/clipboard_large_data_scheduler.h"

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;

namespace {

class FakeChannel final : public network::IChannel
{
public:
    explicit FakeChannel(network::ChannelKey key)
        : key_(key)
    {
    }

    protocol::ChannelId id() const override
    {
        return key_.channelId;
    }

    protocol::ChannelType type() const override
    {
        return key_.channelType;
    }

    bool isOpen() const override
    {
        return true;
    }

    network::SendResult send(const protocol::PacketEnvelope&) override
    {
        return network::SendResult::sent();
    }

private:
    network::ChannelKey key_;
};

protocol::NegotiatedCapabilities clipboardCapabilities()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Standard};
    capabilities.packetTypes = {protocol::PacketType::Clipboard};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Error};
    return capabilities;
}

protocol::PacketEnvelope bulkClipboardResponse()
{
    protocol::PacketEnvelope packet;
    packet.channelId = clipboardSmallDataChannelKey().channelId;
    packet.channelType = clipboardSmallDataChannelKey().channelType;
    packet.packetType = protocol::PacketType::Clipboard;
    packet.messageKind = protocol::MessageKind::Response;
    packet.priority = protocol::PacketPriority::Bulk;
    return packet;
}

void registerReadyLargeData(network::ChannelRegistry& channels)
{
    const network::ChannelSpec spec = network::defaultLargeDataChannelSpec();
    assert(channels.registerSpec(spec).ok);
    assert(channels.bind(spec.key, std::make_shared<FakeChannel>(spec.key)).ok);
    assert(channels.markReady(spec.key, {}).ok);
}

void smallPayloadFallsBackToSmallDataWithoutLargeData()
{
    network::ChannelRegistry channels(clipboardCapabilities());
    ClipboardLargeDataScheduleOptions options;
    options.smallDataFallbackBytes = 64;

    const ClipboardLargeDataScheduleResult schedule =
        scheduleClipboardBulkResponse(&channels,
                                      bulkClipboardResponse(),
                                      12,
                                      options);

    assert(schedule.ok());
    assert(!schedule.usesLargeData);
    assert(schedule.channel == clipboardSmallDataChannelKey());
}

void largePayloadRequiresLargeDataChannel()
{
    network::ChannelRegistry channels(clipboardCapabilities());
    ClipboardLargeDataScheduleOptions options;
    options.smallDataFallbackBytes = 64;

    const ClipboardLargeDataScheduleResult schedule =
        scheduleClipboardBulkResponse(&channels,
                                      bulkClipboardResponse(),
                                      128,
                                      options);

    assert(!schedule.ok());
    assert(schedule.status == protocol::ResponseStatus::ChannelUnavailable);
    assert(!schedule.message.empty());
}

void readyLargeDataIsPreferred()
{
    network::ChannelRegistry channels(clipboardCapabilities());
    registerReadyLargeData(channels);

    const ClipboardLargeDataScheduleResult schedule =
        scheduleClipboardBulkResponse(&channels,
                                      bulkClipboardResponse(),
                                      12);

    assert(schedule.ok());
    assert(schedule.usesLargeData);
    assert(schedule.channel == clipboardLargeDataChannelKey());
}

void congestedLargeDataReturnsBackPressure()
{
    network::ChannelRegistry channels(clipboardCapabilities());
    registerReadyLargeData(channels);
    network::ChannelPressure pressure;
    pressure.level = network::PressureLevel::Congested;
    pressure.queuedPackets = 512;
    assert(channels.updatePressure(clipboardLargeDataChannelKey(),
                                   pressure)
               .ok);

    const ClipboardLargeDataScheduleResult schedule =
        scheduleClipboardBulkResponse(&channels,
                                      bulkClipboardResponse(),
                                      128);

    assert(!schedule.ok());
    assert(schedule.status == protocol::ResponseStatus::BackPressure);
}

void fullLargeDataWindowReturnsBackPressure()
{
    network::ChannelRegistry channels(clipboardCapabilities());
    registerReadyLargeData(channels);

    ClipboardLargeDataScheduleOptions options;
    options.largeDataWindowBytes = 128;
    options.largeDataInFlightBytes = 96;

    const ClipboardLargeDataScheduleResult schedule =
        scheduleClipboardBulkResponse(&channels,
                                      bulkClipboardResponse(),
                                      64,
                                      options);

    assert(!schedule.ok());
    assert(schedule.status == protocol::ResponseStatus::BackPressure);
    assert(schedule.message.find("window") != std::string::npos);
}

void largeDataWindowTracksReserveAndAcknowledge()
{
    ClipboardLargeDataWindow window(128);

    ClipboardLargeDataWindowReserveResult reserved = window.reserve(96);
    assert(reserved.ok());
    assert(reserved.inFlightBytes == 96);
    assert(window.snapshot().reservations == 1);

    reserved = window.reserve(64);
    assert(!reserved.ok());
    assert(reserved.status == protocol::ResponseStatus::BackPressure);
    assert(window.snapshot().inFlightBytes == 96);

    window.acknowledge(64);
    assert(window.snapshot().inFlightBytes == 32);
    assert(window.snapshot().releasedBytes == 64);

    reserved = window.reserve(64);
    assert(reserved.ok());
    assert(reserved.inFlightBytes == 96);
    assert(window.snapshot().reservations == 2);
}

void nonBulkClipboardResponseStaysOnSmallData()
{
    network::ChannelRegistry channels(clipboardCapabilities());
    registerReadyLargeData(channels);

    protocol::PacketEnvelope packet = bulkClipboardResponse();
    packet.priority = protocol::PacketPriority::Normal;
    ClipboardLargeDataScheduleResult schedule =
        scheduleClipboardBulkResponse(&channels, packet, 128);
    applyClipboardLargeDataSchedule(schedule, packet);

    assert(schedule.ok());
    assert(!schedule.usesLargeData);
    assert(packet.channelId == clipboardSmallDataChannelKey().channelId);
}

} // namespace

int main()
{
    smallPayloadFallsBackToSmallDataWithoutLargeData();
    largePayloadRequiresLargeDataChannel();
    readyLargeDataIsPreferred();
    congestedLargeDataReturnsBackPressure();
    fullLargeDataWindowReturnsBackPressure();
    largeDataWindowTracksReserveAndAcknowledge();
    nonBulkClipboardResponseStaysOnSmallData();
    return 0;
}
