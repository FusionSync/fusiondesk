#include "fusiondesk/core/network/channel_defaults.h"

namespace fusiondesk {
namespace network {

namespace {

ChannelSpec makeControlSpec()
{
    ChannelSpec spec;
    spec.key = ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
                          protocol::ChannelType::Control};
    spec.name = "control";
    spec.socketClass = SocketClass::Control;
    spec.defaultPriority = protocol::PacketPriority::Critical;
    spec.reliability = ReliabilityMode::Reliable;
    spec.ordering = OrderingMode::Ordered;
    spec.flowControl = FlowControlMode::Pressure;
    spec.queuePolicy = QueuePolicy::Bounded;
    spec.allowlist = {protocol::PacketType::ChannelInit,
                      protocol::PacketType::Heartbeat,
                      protocol::PacketType::Login,
                      protocol::PacketType::Control,
                      protocol::PacketType::Exchange,
                      protocol::PacketType::CheckLicense};
    spec.ownerModuleId = "network.control";
    spec.required = true;
    return spec;
}

ChannelSpec makeSmallDataSpec()
{
    ChannelSpec spec;
    spec.key = ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
                          protocol::ChannelType::Standard};
    spec.name = "small_data";
    spec.socketClass = SocketClass::Auxiliary;
    spec.defaultPriority = protocol::PacketPriority::Normal;
    spec.reliability = ReliabilityMode::Reliable;
    spec.ordering = OrderingMode::Ordered;
    spec.flowControl = FlowControlMode::Pressure;
    spec.queuePolicy = QueuePolicy::Bounded;
    spec.allowlist = {protocol::PacketType::PayloadAck,
                      protocol::PacketType::Mouse,
                      protocol::PacketType::Keyboard,
                      protocol::PacketType::Touchscreen,
                      protocol::PacketType::Gamepad,
                      protocol::PacketType::Clipboard,
                      protocol::PacketType::FilesystemControl,
                      protocol::PacketType::Printer,
                      protocol::PacketType::Control};
    spec.ownerModuleId = "network.small_data";
    spec.required = true;
    return spec;
}

ChannelSpec makeMainScreenSpec()
{
    ChannelSpec spec;
    spec.key = ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                          protocol::ChannelType::Video};
    spec.name = "main_screen";
    spec.socketClass = SocketClass::Realtime;
    spec.defaultPriority = protocol::PacketPriority::Realtime;
    spec.reliability = ReliabilityMode::BestEffort;
    spec.ordering = OrderingMode::LatestOnly;
    spec.flowControl = FlowControlMode::Pressure;
    spec.queuePolicy = QueuePolicy::KeepLatest;
    spec.allowlist = {protocol::PacketType::Video,
                      protocol::PacketType::PayloadAck,
                      protocol::PacketType::CursorChange,
                      protocol::PacketType::Watermark};
    spec.ownerModuleId = "display.screen";
    spec.required = true;
    return spec;
}

ChannelSpec makeLargeDataSpec()
{
    ChannelSpec spec;
    spec.key = ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::LargeData),
                          protocol::ChannelType::Standard};
    spec.name = "large_data";
    spec.socketClass = SocketClass::Bulk;
    spec.defaultPriority = protocol::PacketPriority::Normal;
    spec.reliability = ReliabilityMode::Reliable;
    spec.ordering = OrderingMode::Ordered;
    spec.flowControl = FlowControlMode::Pressure;
    spec.queuePolicy = QueuePolicy::Bounded;
    spec.allowlist = {protocol::PacketType::Filesystem,
                      protocol::PacketType::FilesystemControl,
                      protocol::PacketType::FilesystemIrp,
                      protocol::PacketType::Printer,
                      protocol::PacketType::Clipboard};
    spec.ownerModuleId = "redirection.large_data";
    spec.required = false;
    return spec;
}

bool isBulkPacket(protocol::PacketType packetType)
{
    return packetType == protocol::PacketType::Filesystem ||
           packetType == protocol::PacketType::FilesystemIrp ||
           packetType == protocol::PacketType::Printer;
}

} // namespace

std::vector<ChannelSpec> defaultMvpChannelSpecs()
{
    return {makeControlSpec(), makeSmallDataSpec(), makeMainScreenSpec()};
}

ChannelSpec defaultLargeDataChannelSpec()
{
    return makeLargeDataSpec();
}

SendOptions defaultSendOptions(const protocol::PacketEnvelope& packet)
{
    SendOptions options;
    options.priority = packet.priority;

    if (packet.packetType == protocol::PacketType::Heartbeat ||
        packet.packetType == protocol::PacketType::ChannelInit ||
        packet.packetType == protocol::PacketType::Exchange) {
        options.priority = protocol::PacketPriority::Critical;
        options.dropPolicy = DropPolicy::None;
        options.maxQueueDepth = 256;
        return options;
    }

    if (packet.packetType == protocol::PacketType::Video) {
        options.priority = protocol::PacketPriority::Realtime;
        options.dropPolicy = DropPolicy::KeepLatestNonKeyFrame;
        options.maxQueueDepth = 2;
        return options;
    }

    if (packet.packetType == protocol::PacketType::Clipboard &&
        packet.priority == protocol::PacketPriority::Bulk) {
        options.priority = protocol::PacketPriority::Bulk;
        options.dropPolicy = DropPolicy::None;
        options.maxQueueDepth = 512;
        return options;
    }

    if (packet.packetType == protocol::PacketType::PayloadAck ||
        packet.packetType == protocol::PacketType::Mouse ||
        packet.packetType == protocol::PacketType::Keyboard ||
        packet.packetType == protocol::PacketType::Clipboard ||
        packet.packetType == protocol::PacketType::Touchscreen ||
        packet.packetType == protocol::PacketType::Gamepad) {
        options.priority = protocol::PacketPriority::Interactive;
        options.dropPolicy = DropPolicy::None;
        options.maxQueueDepth = 1024;
        return options;
    }

    if (isBulkPacket(packet.packetType)) {
        options.priority = protocol::PacketPriority::Bulk;
        options.dropPolicy = DropPolicy::None;
        options.maxQueueDepth = 512;
        return options;
    }

    options.priority = protocol::PacketPriority::Normal;
    options.dropPolicy = DropPolicy::None;
    options.maxQueueDepth = 1024;
    return options;
}

} // namespace network
} // namespace fusiondesk
