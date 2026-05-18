#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/network/network_manager.h"

using namespace fusiondesk;

namespace {

class FakeChannel : public network::IChannel
{
public:
    FakeChannel(protocol::ChannelId id, protocol::ChannelType type)
        : id_(id)
        , type_(type)
    {
    }

    protocol::ChannelId id() const override
    {
        return id_;
    }

    protocol::ChannelType type() const override
    {
        return type_;
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
    protocol::ChannelId id_ = 0;
    protocol::ChannelType type_ = protocol::ChannelType::Standard;
};

class FakeTransportSocket : public network::ITransportSocket
{
public:
    explicit FakeTransportSocket(network::SocketClass socketClass)
        : socketClass_(socketClass)
    {
    }

    network::SocketClass socketClass() const override
    {
        return socketClass_;
    }

    bool open(const network::SocketOpenOptions&) override
    {
        state_ = network::SocketState::Open;
        return true;
    }

    void close(const network::CloseReason&) override
    {
        state_ = network::SocketState::Closed;
    }

    network::SendResult write(const protocol::ByteBuffer& bytes) override
    {
        if (state_ != network::SocketState::Open)
            return {network::SendStatus::ChannelClosed, "socket is closed"};

        writes_.push_back(bytes);
        return network::SendResult::sent();
    }

    network::SocketState state() const override
    {
        return state_;
    }

    const std::vector<protocol::ByteBuffer>& writes() const
    {
        return writes_;
    }

private:
    network::SocketClass socketClass_ = network::SocketClass::Control;
    network::SocketState state_ = network::SocketState::Closed;
    std::vector<protocol::ByteBuffer> writes_;
};

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control,
                                 protocol::ChannelType::Standard,
                                 protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::ChannelInit,
                                protocol::PacketType::Heartbeat,
                                protocol::PacketType::Exchange,
                                protocol::PacketType::PayloadAck,
                                protocol::PacketType::Video};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event};
    return capabilities;
}

network::ChannelKey controlKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
                               protocol::ChannelType::Control};
}

network::ChannelKey screenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                               protocol::ChannelType::Video};
}

protocol::PacketEnvelope makePacket(network::ChannelKey key, protocol::PacketType packetType)
{
    protocol::PacketEnvelope packet;
    packet.sessionId = 7;
    packet.channelId = key.channelId;
    packet.channelType = key.channelType;
    packet.packetType = packetType;
    packet.messageKind = protocol::MessageKind::Event;
    return packet;
}

void setupDefaultChannels(network::NetworkManager& manager)
{
    manager.registerDefaultMvpChannels();
    assert(manager.bindChannel(std::make_shared<FakeChannel>(controlKey().channelId, controlKey().channelType)).ok);
    assert(manager.bindChannel(std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);
    assert(manager.markReady(controlKey(), {}).ok);
    assert(manager.markReady(screenKey(), {}).ok);
}

void writesPacketsToTheirSocketClass()
{
    network::NetworkManager manager(makeNegotiated());
    setupDefaultChannels(manager);

    auto control = std::make_shared<FakeTransportSocket>(network::SocketClass::Control);
    auto realtime = std::make_shared<FakeTransportSocket>(network::SocketClass::Realtime);
    assert(manager.registerTransportSocket(control).ok);
    assert(manager.registerTransportSocket(realtime).ok);
    assert(manager.openTransportSocket(network::SocketClass::Control, {}).ok);
    assert(manager.openTransportSocket(network::SocketClass::Realtime, {}).ok);

    assert(manager.writeTransportBytes(makePacket(screenKey(), protocol::PacketType::Video), {1}).ok);
    assert(manager.writeTransportBytes(makePacket(controlKey(), protocol::PacketType::Heartbeat), {2}).ok);

    assert(realtime->writes().size() == 1);
    assert(realtime->writes().front() == protocol::ByteBuffer{1});
    assert(control->writes().size() == 1);
    assert(control->writes().front() == protocol::ByteBuffer{2});
    assert(manager.socketSnapshotForChannel(screenKey()).socketClass == network::SocketClass::Realtime);
}

void realtimeFailureDoesNotBlockControlSocket()
{
    network::NetworkManager manager(makeNegotiated());
    setupDefaultChannels(manager);

    auto control = std::make_shared<FakeTransportSocket>(network::SocketClass::Control);
    auto realtime = std::make_shared<FakeTransportSocket>(network::SocketClass::Realtime);
    assert(manager.registerTransportSocket(control).ok);
    assert(manager.registerTransportSocket(realtime).ok);
    assert(manager.openTransportSocket(network::SocketClass::Control, {}).ok);
    assert(manager.openTransportSocket(network::SocketClass::Realtime, {}).ok);

    manager.closeTransportSocket(network::SocketClass::Realtime, {"realtime failed"});

    assert(!manager.writeTransportBytes(makePacket(screenKey(), protocol::PacketType::Video), {3}).ok);
    assert(manager.writeTransportBytes(makePacket(controlKey(), protocol::PacketType::Heartbeat), {4}).ok);
    assert(control->writes().size() == 1);
}

} // namespace

int main()
{
    writesPacketsToTheirSocketClass();
    realtimeFailureDoesNotBlockControlSocket();
    return 0;
}
