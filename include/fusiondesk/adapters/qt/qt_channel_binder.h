#ifndef FUSIONDESK_ADAPTERS_QT_QT_CHANNEL_BINDER_H
#define FUSIONDESK_ADAPTERS_QT_QT_CHANNEL_BINDER_H

#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/adapters/qt/qt_packet_channel.h"
#include "fusiondesk/core/network/network_manager.h"

namespace fusiondesk {
namespace adapters {
namespace qt {

struct QtChannelBindOptions
{
    network::ChannelSpec spec;
    std::shared_ptr<QtTcpTransportSocket> transport;
    network::ChannelReadyInfo ready;
    bool markReady = true;
};

struct QtChannelBindResult
{
    bool ok = false;
    network::ChannelKey key;
    std::string message;
};

class QtChannelBinder
{
public:
    explicit QtChannelBinder(network::NetworkManager& manager);

    QtChannelBindResult bindChannel(const QtChannelBindOptions& options);
    const std::vector<std::shared_ptr<QtPacketChannel>>& channels() const;

private:
    network::NetworkManager& manager_;
    std::vector<std::shared_ptr<QtPacketChannel>> channels_;
};

} // namespace qt
} // namespace adapters
} // namespace fusiondesk

#endif // FUSIONDESK_ADAPTERS_QT_QT_CHANNEL_BINDER_H
