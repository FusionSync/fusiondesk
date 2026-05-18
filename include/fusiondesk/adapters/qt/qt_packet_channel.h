#ifndef FUSIONDESK_ADAPTERS_QT_QT_PACKET_CHANNEL_H
#define FUSIONDESK_ADAPTERS_QT_QT_PACKET_CHANNEL_H

#include <memory>

#include "fusiondesk/adapters/qt/qt_tcp_transport_socket.h"
#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/protocol/packet_codec.h"

namespace fusiondesk {
namespace adapters {
namespace qt {

class QtPacketChannel : public network::IChannel
{
public:
    QtPacketChannel(network::ChannelKey key,
                    std::shared_ptr<QtTcpTransportSocket> transport,
                    network::NetworkRouter* ingressRouter,
                    protocol::PacketCodec codec = protocol::PacketCodec{});
    ~QtPacketChannel() override;

    protocol::ChannelId id() const override;
    protocol::ChannelType type() const override;
    bool isOpen() const override;
    network::SendResult send(const protocol::PacketEnvelope& packet) override;

    int decodedPackets() const;
    int decodeFailures() const;

private:
    void onBytes(protocol::ByteBuffer bytes);

private:
    network::ChannelKey key_;
    std::shared_ptr<QtTcpTransportSocket> transport_;
    network::NetworkRouter* ingressRouter_ = nullptr;
    protocol::PacketCodec codec_;
    protocol::ByteBuffer inbound_;
    int decodedPackets_ = 0;
    int decodeFailures_ = 0;
};

} // namespace qt
} // namespace adapters
} // namespace fusiondesk

#endif // FUSIONDESK_ADAPTERS_QT_QT_PACKET_CHANNEL_H
