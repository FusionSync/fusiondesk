#include "fusiondesk/adapters/qt/qt_packet_channel.h"

#include <cstddef>
#include <utility>

namespace fusiondesk {
namespace adapters {
namespace qt {

QtPacketChannel::QtPacketChannel(network::ChannelKey key,
                                 std::shared_ptr<QtTcpTransportSocket> transport,
                                 network::NetworkRouter* ingressRouter,
                                 protocol::PacketCodec codec)
    : key_(key),
      transport_(std::move(transport)),
      ingressRouter_(ingressRouter),
      codec_(std::move(codec))
{
    if (transport_) {
        transport_->setBytesReceivedHandler([this](protocol::ByteBuffer bytes) {
            onBytes(std::move(bytes));
        });
    }
}

QtPacketChannel::~QtPacketChannel()
{
    if (transport_)
        transport_->setBytesReceivedHandler({});
}

protocol::ChannelId QtPacketChannel::id() const
{
    return key_.channelId;
}

protocol::ChannelType QtPacketChannel::type() const
{
    return key_.channelType;
}

bool QtPacketChannel::isOpen() const
{
    return transport_ && transport_->state() == network::SocketState::Open;
}

network::SendResult QtPacketChannel::send(const protocol::PacketEnvelope& packet)
{
    if (!isOpen())
        return {network::SendStatus::ChannelClosed, "qt packet channel transport is not open"};

    return transport_->write(codec_.encode(packet));
}

int QtPacketChannel::decodedPackets() const
{
    return decodedPackets_;
}

int QtPacketChannel::decodeFailures() const
{
    return decodeFailures_;
}

void QtPacketChannel::onBytes(protocol::ByteBuffer bytes)
{
    inbound_.insert(inbound_.end(), bytes.begin(), bytes.end());

    while (!inbound_.empty()) {
        const protocol::PacketFrameInspection frame = codec_.inspectFrame(inbound_);
        if (frame.status == protocol::PacketDecodeStatus::Incomplete)
            return;

        if (!frame.ok() || frame.frameSize == 0 || frame.frameSize > inbound_.size()) {
            ++decodeFailures_;
            inbound_.clear();
            return;
        }

        protocol::ByteBuffer packetBytes(inbound_.begin(),
                                         inbound_.begin() + static_cast<std::ptrdiff_t>(frame.frameSize));
        protocol::PacketDecodeResult decoded = codec_.decode(packetBytes);
        if (!decoded.ok()) {
            ++decodeFailures_;
            inbound_.erase(inbound_.begin(),
                           inbound_.begin() + static_cast<std::ptrdiff_t>(frame.frameSize));
            continue;
        }

        ++decodedPackets_;
        inbound_.erase(inbound_.begin(),
                       inbound_.begin() +
                           static_cast<std::ptrdiff_t>(frame.frameSize));
        if (ingressRouter_ != nullptr)
            ingressRouter_->submitIncoming(decoded.packet);
    }
}

} // namespace qt
} // namespace adapters
} // namespace fusiondesk
