#include "fusiondesk/runtime/session/link_channel_binding_report.h"

#include <algorithm>
#include <map>
#include <utility>

namespace fusiondesk {
namespace runtime {

namespace {

bool containsKey(const std::vector<network::ChannelKey>& keys,
                 network::ChannelKey key)
{
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

LinkChannelBindingItem itemFromSnapshot(const network::ChannelSnapshot& snapshot)
{
    LinkChannelBindingItem item;
    item.key = snapshot.spec.key;
    item.name = snapshot.spec.name;
    item.ownerModuleId = snapshot.spec.ownerModuleId;
    item.profileMinimum = snapshot.spec.required;
    item.registered = snapshot.spec.key.channelId != 0;
    item.bound = snapshot.bound;
    item.ready = snapshot.ready;
    item.lifecycle = snapshot.state;
    item.pressure = snapshot.pressure;
    item.readyInfo = snapshot.readyInfo;
    item.message = snapshot.message;
    item.degraded = snapshot.state == network::ChannelLifecycleState::Degraded;
    item.closed = snapshot.state == network::ChannelLifecycleState::Closed;
    item.failed = snapshot.state == network::ChannelLifecycleState::Failed;
    return item;
}

std::string blockedMessage(const LinkChannelBindingItem& item)
{
    if (!item.registered)
        return "required module channel spec is missing";
    if (item.failed)
        return "required module channel failed";
    if (item.closed)
        return "required module channel is closed";
    if (item.degraded)
        return "required module channel is degraded";
    if (item.listening && !item.bound)
        return "channel is listening but no accepted or connected transport is ready";
    if (!item.bound)
        return "required module channel is not bound";
    return "required module channel is not ready";
}

void mergeRequiredBinding(LinkChannelBindingItem& item,
                          const module::ModuleManifest& manifest,
                          const module::ChannelBinding& binding)
{
    item.moduleRequired = true;
    if (item.name.empty())
        item.name = binding.name;

    if (std::find(item.requiredByModules.begin(),
                  item.requiredByModules.end(),
                  manifest.moduleId) == item.requiredByModules.end()) {
        item.requiredByModules.push_back(manifest.moduleId);
    }
}

} // namespace

LinkChannelBindingReport buildLinkChannelBindingReport(
    session::Session& session,
    const LinkChannelBindingReportOptions& options)
{
    LinkChannelBindingReport report;
    report.sessionId = session.id();

    network::NetworkManager* network = session.network();
    if (network == nullptr) {
        report.messages.push_back("session network is missing");
        return report;
    }

    std::map<network::ChannelKey, LinkChannelBindingItem> items;
    for (const network::ChannelSnapshot& snapshot : network->registry().snapshots())
        items[snapshot.spec.key] = itemFromSnapshot(snapshot);

    module::ModuleHost* moduleHost = session.moduleHost();
    if (moduleHost != nullptr) {
        for (const module::ModuleManifest& manifest : moduleHost->manifests()) {
            for (const module::ChannelBinding& binding : manifest.channels) {
                if (!binding.required)
                    continue;

                const network::ChannelKey key{binding.channelId, binding.channelType};
                LinkChannelBindingItem& item = items[key];
                item.key = key;
                mergeRequiredBinding(item, manifest, binding);
            }
        }
    }

    for (auto& entry : items) {
        LinkChannelBindingItem& item = entry.second;
        item.listening = containsKey(options.listeningChannels, item.key);
        item.blocked = item.moduleRequired && !item.ready;
        if (item.blocked && item.message.empty())
            item.message = blockedMessage(item);
    }

    report.channels.reserve(items.size());
    for (auto& entry : items)
        report.channels.push_back(std::move(entry.second));

    report.ok = report.messages.empty();
    for (const LinkChannelBindingItem& item : report.channels) {
        if (item.blocked) {
            report.ok = false;
            break;
        }
    }

    return report;
}

} // namespace runtime
} // namespace fusiondesk
