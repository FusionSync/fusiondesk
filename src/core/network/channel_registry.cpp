#include "fusiondesk/core/network/channel_registry.h"

#include <algorithm>
#include <utility>

namespace fusiondesk {
namespace network {

namespace {

ChannelKey keyOf(const protocol::PacketEnvelope& packet)
{
    return ChannelKey{packet.channelId, packet.channelType};
}

} // namespace

ChannelRegistry::ChannelRegistry(protocol::NegotiatedCapabilities capabilities)
    : allowlist_(std::move(capabilities))
{
}

ChannelRegistryResult ChannelRegistry::registerSpec(const ChannelSpec& spec)
{
    if (spec.key.channelId == 0) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::InvalidArgument,
                                             protocol::ResponseStatus::InvalidArgument,
                                             "channel id is required");
    }

    if (entries_.find(spec.key) != entries_.end()) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::AlreadyRegistered,
                                             protocol::ResponseStatus::Conflict,
                                             "channel spec already registered");
    }

    Entry entry;
    entry.snapshot.spec = spec;
    entry.snapshot.state = ChannelLifecycleState::Registered;
    entries_[spec.key] = std::move(entry);
    return ChannelRegistryResult::success();
}

ChannelRegistryResult ChannelRegistry::bind(ChannelKey key, std::shared_ptr<IChannel> channel)
{
    Entry* entry = find(key);
    if (!entry) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::NotFound,
                                             protocol::ResponseStatus::ChannelUnavailable,
                                             "channel spec not found");
    }

    if (!channel) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::InvalidArgument,
                                             protocol::ResponseStatus::InvalidArgument,
                                             "channel instance is required");
    }

    entry->channel = std::move(channel);
    entry->snapshot.bound = true;
    entry->snapshot.state = ChannelLifecycleState::Bound;
    entry->snapshot.message.clear();
    return ChannelRegistryResult::success();
}

void ChannelRegistry::unbind(ChannelKey key, const std::string& reason)
{
    Entry* entry = find(key);
    if (!entry)
        return;

    entry->channel.reset();
    entry->snapshot.bound = false;
    entry->snapshot.ready = false;
    entry->snapshot.state = ChannelLifecycleState::Closed;
    entry->snapshot.message = reason;
}

ChannelRegistryResult ChannelRegistry::markReady(ChannelKey key, const ChannelReadyInfo& ready)
{
    Entry* entry = find(key);
    if (!entry) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::NotFound,
                                             protocol::ResponseStatus::ChannelUnavailable,
                                             "channel spec not found");
    }

    if (!entry->channel || !entry->channel->isOpen()) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::NotReady,
                                             protocol::ResponseStatus::ChannelUnavailable,
                                             "channel is not bound or open");
    }

    entry->snapshot.ready = true;
    entry->snapshot.readyInfo = ready;
    entry->snapshot.state = ChannelLifecycleState::Ready;
    entry->snapshot.message.clear();
    return ChannelRegistryResult::success();
}

ChannelRegistryResult ChannelRegistry::markDegraded(ChannelKey key, const std::string& reason)
{
    Entry* entry = find(key);
    if (!entry) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::NotFound,
                                             protocol::ResponseStatus::ChannelUnavailable,
                                             "channel spec not found");
    }

    entry->snapshot.ready = false;
    entry->snapshot.state = ChannelLifecycleState::Degraded;
    entry->snapshot.message = reason;
    return ChannelRegistryResult::success();
}

ChannelRegistryResult ChannelRegistry::markFailed(ChannelKey key, const std::string& reason)
{
    Entry* entry = find(key);
    if (!entry) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::NotFound,
                                             protocol::ResponseStatus::ChannelUnavailable,
                                             "channel spec not found");
    }

    entry->snapshot.ready = false;
    entry->snapshot.state = ChannelLifecycleState::Failed;
    entry->snapshot.message = reason;
    return ChannelRegistryResult::success();
}

ChannelRegistryResult ChannelRegistry::updatePressure(ChannelKey key, ChannelPressure pressure)
{
    Entry* entry = find(key);
    if (!entry) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::NotFound,
                                             protocol::ResponseStatus::ChannelUnavailable,
                                             "channel spec not found");
    }

    entry->snapshot.pressure = pressure;
    return ChannelRegistryResult::success();
}

bool ChannelRegistry::isReady(ChannelKey key) const
{
    const Entry* entry = find(key);
    return entry && entry->snapshot.ready && entry->snapshot.state == ChannelLifecycleState::Ready;
}

ChannelSnapshot ChannelRegistry::snapshot(ChannelKey key) const
{
    const Entry* entry = find(key);
    if (!entry)
        return {};
    return entry->snapshot;
}

std::vector<ChannelSnapshot> ChannelRegistry::snapshots() const
{
    std::vector<ChannelSnapshot> result;
    result.reserve(entries_.size());
    for (const auto& item : entries_)
        result.push_back(item.second.snapshot);
    return result;
}

ChannelRegistryResult ChannelRegistry::validatePacket(const protocol::PacketEnvelope& packet) const
{
    const Entry* entry = find(keyOf(packet));
    if (!entry) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::NotFound,
                                             protocol::ResponseStatus::ChannelUnavailable,
                                             "channel is not registered");
    }

    ChannelAllowlistResult negotiated = allowlist_.validate(packet);
    if (!negotiated.allowed) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::NotNegotiated,
                                             negotiated.responseStatus,
                                             negotiated.message);
    }

    if (!packetTypeAllowed(entry->snapshot.spec, packet.packetType)) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::PacketTypeNotAllowed,
                                             protocol::ResponseStatus::Unsupported,
                                             "packet type is not allowed on channel");
    }

    if (!isReady(keyOf(packet))) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::NotReady,
                                             protocol::ResponseStatus::ChannelUnavailable,
                                             "channel is not ready");
    }

    return ChannelRegistryResult::success();
}

ChannelRegistry::Entry* ChannelRegistry::find(ChannelKey key)
{
    auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
}

const ChannelRegistry::Entry* ChannelRegistry::find(ChannelKey key) const
{
    auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
}

bool ChannelRegistry::packetTypeAllowed(const ChannelSpec& spec, protocol::PacketType packetType) const
{
    if (spec.allowlist.empty())
        return true;

    return std::find(spec.allowlist.begin(), spec.allowlist.end(), packetType) != spec.allowlist.end();
}

} // namespace network
} // namespace fusiondesk
