#include "fusiondesk/adapters/qt/qt_channel_binder.h"

#include <utility>

namespace fusiondesk {
namespace adapters {
namespace qt {

namespace {

QtChannelBindResult failed(network::ChannelKey key, std::string message)
{
    QtChannelBindResult result;
    result.key = key;
    result.message = std::move(message);
    return result;
}

QtChannelBindResult success(network::ChannelKey key)
{
    QtChannelBindResult result;
    result.ok = true;
    result.key = key;
    return result;
}

} // namespace

QtChannelBinder::QtChannelBinder(network::NetworkManager& manager)
    : manager_(manager)
{
}

QtChannelBindResult QtChannelBinder::bindChannel(const QtChannelBindOptions& options)
{
    if (!options.transport)
        return failed(options.spec.key, "qt transport is required");

    network::SocketGroupResult socketResult = manager_.registerTransportSocket(options.transport);
    if (!socketResult.ok && socketResult.status != network::SocketGroupStatus::AlreadyRegistered)
        return failed(options.spec.key, socketResult.message);

    network::ChannelRegistryResult specResult = manager_.registerSpec(options.spec);
    if (!specResult.ok && specResult.status != network::ChannelRegistryStatus::AlreadyRegistered)
        return failed(options.spec.key, specResult.message);

    auto channel = std::make_shared<QtPacketChannel>(options.spec.key,
                                                    options.transport,
                                                    &manager_.router());
    network::ChannelRegistryResult bindResult = manager_.bindChannel(channel);
    if (!bindResult.ok)
        return failed(options.spec.key, bindResult.message);

    if (options.markReady) {
        network::ChannelRegistryResult readyResult = manager_.markReady(options.spec.key, options.ready);
        if (!readyResult.ok)
            return failed(options.spec.key, readyResult.message);
    }

    channels_.push_back(std::move(channel));
    return success(options.spec.key);
}

const std::vector<std::shared_ptr<QtPacketChannel>>& QtChannelBinder::channels() const
{
    return channels_;
}

} // namespace qt
} // namespace adapters
} // namespace fusiondesk
