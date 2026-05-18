#include "fusiondesk/modules/clipboard/clipboard_large_data_scheduler.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

bool pressureBlocksBulk(network::PressureLevel level)
{
    return level == network::PressureLevel::Congested ||
           level == network::PressureLevel::Closed ||
           level == network::PressureLevel::Failed;
}

bool windowBlocksBulk(std::uint64_t payloadBytes,
                      const ClipboardLargeDataScheduleOptions& options)
{
    if (options.largeDataWindowBytes == 0)
        return false;
    if (options.largeDataInFlightBytes >= options.largeDataWindowBytes)
        return true;
    return payloadBytes >
           options.largeDataWindowBytes - options.largeDataInFlightBytes;
}

ClipboardLargeDataWindowReserveResult backPressureWindowResult(
    std::uint64_t inFlightBytes)
{
    ClipboardLargeDataWindowReserveResult result;
    result.status = protocol::ResponseStatus::BackPressure;
    result.inFlightBytes = inFlightBytes;
    result.message = "clipboard large_data window is full";
    return result;
}

} // namespace

network::ChannelKey clipboardSmallDataChannelKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
        protocol::ChannelType::Standard};
}

network::ChannelKey clipboardLargeDataChannelKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::LargeData),
        protocol::ChannelType::Standard};
}

ClipboardLargeDataScheduleResult scheduleClipboardBulkResponse(
    const network::ChannelRegistry* channels,
    const protocol::PacketEnvelope& packet,
    std::uint64_t payloadBytes,
    const ClipboardLargeDataScheduleOptions& options)
{
    ClipboardLargeDataScheduleResult result;
    result.channel = clipboardSmallDataChannelKey();

    if (packet.packetType != protocol::PacketType::Clipboard ||
        packet.priority != protocol::PacketPriority::Bulk) {
        return result;
    }

    if (channels == nullptr)
        return result;

    protocol::PacketEnvelope largeProbe = packet;
    const network::ChannelKey largeData = clipboardLargeDataChannelKey();
    largeProbe.channelId = largeData.channelId;
    largeProbe.channelType = largeData.channelType;

    const network::ChannelRegistryResult validation =
        channels->validatePacket(largeProbe);
    if (validation.ok) {
        const network::ChannelSnapshot snapshot =
            channels->snapshot(largeData);
        if (pressureBlocksBulk(snapshot.pressure.level)) {
            result.status = protocol::ResponseStatus::BackPressure;
            result.message =
                snapshot.message.empty()
                    ? "clipboard large_data channel is congested"
                    : snapshot.message;
            return result;
        }
        if (windowBlocksBulk(payloadBytes, options)) {
            result.status = protocol::ResponseStatus::BackPressure;
            result.message = "clipboard large_data window is full";
            return result;
        }

        result.channel = largeData;
        result.usesLargeData = true;
        return result;
    }

    const std::uint64_t fallbackBytes =
        options.smallDataFallbackBytes == 0 ? 1024 * 1024
                                            : options.smallDataFallbackBytes;
    if (payloadBytes <= fallbackBytes)
        return result;

    result.status = validation.responseStatus;
    result.message =
        validation.message.empty()
            ? "clipboard large_data channel is unavailable for file content"
            : validation.message;
    return result;
}

void applyClipboardLargeDataSchedule(
    const ClipboardLargeDataScheduleResult& schedule,
    protocol::PacketEnvelope& packet)
{
    packet.channelId = schedule.channel.channelId;
    packet.channelType = schedule.channel.channelType;
}

ClipboardLargeDataWindow::ClipboardLargeDataWindow(
    std::uint64_t maxInFlightBytes)
    : maxInFlightBytes_(maxInFlightBytes)
{
}

void ClipboardLargeDataWindow::setMaxInFlightBytes(
    std::uint64_t maxInFlightBytes)
{
    maxInFlightBytes_ = maxInFlightBytes;
}

ClipboardLargeDataWindowReserveResult
ClipboardLargeDataWindow::reserve(std::uint64_t bytes)
{
    if (maxInFlightBytes_ != 0) {
        if (inFlightBytes_ >= maxInFlightBytes_ ||
            bytes > maxInFlightBytes_ - inFlightBytes_) {
            return backPressureWindowResult(inFlightBytes_);
        }
    }

    inFlightBytes_ += bytes;
    ++reservations_;

    ClipboardLargeDataWindowReserveResult result;
    result.status = protocol::ResponseStatus::Ok;
    result.inFlightBytes = inFlightBytes_;
    return result;
}

void ClipboardLargeDataWindow::release(std::uint64_t bytes)
{
    const std::uint64_t released =
        bytes > inFlightBytes_ ? inFlightBytes_ : bytes;
    inFlightBytes_ -= released;
    releasedBytes_ += released;
}

void ClipboardLargeDataWindow::acknowledge(std::uint64_t bytes)
{
    release(bytes);
}

ClipboardLargeDataWindowSnapshot ClipboardLargeDataWindow::snapshot() const
{
    ClipboardLargeDataWindowSnapshot result;
    result.maxInFlightBytes = maxInFlightBytes_;
    result.inFlightBytes = inFlightBytes_;
    result.reservations = reservations_;
    result.releasedBytes = releasedBytes_;
    return result;
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
