#include "fusiondesk/core/module/module_factory.h"

#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <utility>

#include "fusiondesk/core/module/module_compatibility.h"

namespace fusiondesk {
namespace module {

namespace {

struct PendingModuleSelection
{
    std::string requestedModuleId;
    const IModuleFactory* factory = nullptr;
    ModuleManifest manifest;
};

std::string versionToString(const ModuleVersion& version)
{
    std::ostringstream out;
    out << version.major << "." << version.minor << "." << version.patch;
    return out.str();
}

bool matchesConstraintTarget(const ModuleVersionConstraint& constraint,
                             const std::string& requestedModuleId,
                             const ModuleManifest& manifest)
{
    return constraint.moduleId == requestedModuleId ||
           constraint.moduleId == manifest.moduleId;
}

void appendVersionFailures(const std::string& requestedModuleId,
                           const ModuleManifest& manifest,
                           const std::vector<ModuleVersionConstraint>& constraints,
                           std::vector<std::string>& failures)
{
    for (const ModuleVersionConstraint& constraint : constraints) {
        if (!matchesConstraintTarget(constraint, requestedModuleId, manifest))
            continue;

        if (moduleVersionInRange(manifest.version,
                                 constraint.minVersion,
                                 constraint.maxVersion))
            continue;

        failures.push_back(manifest.moduleId + " version " +
                           versionToString(manifest.version) +
                           " outside profile range " +
                           versionToString(constraint.minVersion) + ".." +
                           versionToString(constraint.maxVersion));
    }
}

} // namespace

void ModuleComposer::addFactory(std::shared_ptr<IModuleFactory> factory)
{
    if (factory)
        factories_.push_back(std::move(factory));
}

ModuleCompositionResult ModuleComposer::compose(const ModuleCompositionRequest& request) const
{
    ModuleCompositionResult result;
    std::vector<PendingModuleSelection> pending;
    std::set<std::string> versionBlockedModuleIds;
    std::set<std::string> selectedModuleIds;

    for (const std::string& requested : request.requiredModules) {
        const IModuleFactory* factory = findFactory(requested, request.createOptions);
        if (factory == nullptr) {
            result.missingModules.push_back(requested);
            continue;
        }

        ModuleManifest manifest = factory->manifest(request.createOptions);
        if (manifest.moduleId.empty()) {
            result.missingModules.push_back(requested);
            continue;
        }

        PendingModuleSelection selection;
        selection.requestedModuleId = requested;
        selection.factory = factory;
        selection.manifest = std::move(manifest);
        pending.push_back(std::move(selection));
    }

    for (const PendingModuleSelection& selection : pending) {
        if (versionBlockedModuleIds.find(selection.manifest.moduleId) != versionBlockedModuleIds.end())
            continue;

        const std::size_t versionFailureCount = result.versionFailures.size();
        appendVersionFailures(selection.requestedModuleId,
                              selection.manifest,
                              request.versionConstraints,
                              result.versionFailures);
        if (result.versionFailures.size() != versionFailureCount)
            versionBlockedModuleIds.insert(selection.manifest.moduleId);
    }

    for (const PendingModuleSelection& selection : pending) {
        if (versionBlockedModuleIds.find(selection.manifest.moduleId) != versionBlockedModuleIds.end())
            continue;

        if (selectedModuleIds.find(selection.manifest.moduleId) != selectedModuleIds.end())
            continue;

        std::shared_ptr<IModule> module = selection.factory->create(request.createOptions);
        if (!module) {
            result.missingModules.push_back(selection.manifest.moduleId);
            continue;
        }

        selectedModuleIds.insert(selection.manifest.moduleId);
        result.manifests.push_back(selection.manifest);
        result.modules.push_back(std::move(module));
    }

    for (const ModuleManifest& manifest : result.manifests) {
        for (const std::string& dependency : manifest.requiredModules) {
            if (selectedModuleIds.find(dependency) == selectedModuleIds.end())
                result.dependencyFailures.push_back(manifest.moduleId + " requires " + dependency);
        }
    }

    std::map<std::string, std::size_t> indexByModuleId;
    for (std::size_t i = 0; i < result.manifests.size(); ++i)
        indexByModuleId[result.manifests[i].moduleId] = i;

    std::vector<int> visitState(result.manifests.size(), 0);
    std::vector<std::size_t> order;
    order.reserve(result.manifests.size());

    std::function<void(std::size_t)> visit = [&](std::size_t index) {
        if (visitState[index] == 2)
            return;

        if (visitState[index] == 1) {
            result.dependencyFailures.push_back(result.manifests[index].moduleId + " has cyclic dependency");
            return;
        }

        visitState[index] = 1;
        const ModuleManifest& manifest = result.manifests[index];
        for (const std::string& dependency : manifest.requiredModules) {
            auto dependencyIndex = indexByModuleId.find(dependency);
            if (dependencyIndex != indexByModuleId.end())
                visit(dependencyIndex->second);
        }

        visitState[index] = 2;
        order.push_back(index);
    };

    for (std::size_t i = 0; i < result.manifests.size(); ++i)
        visit(i);

    if (order.size() == result.manifests.size()) {
        std::vector<ModuleManifest> sortedManifests;
        std::vector<std::shared_ptr<IModule>> sortedModules;
        sortedManifests.reserve(result.manifests.size());
        sortedModules.reserve(result.modules.size());
        for (std::size_t index : order) {
            sortedManifests.push_back(std::move(result.manifests[index]));
            sortedModules.push_back(std::move(result.modules[index]));
        }
        result.manifests = std::move(sortedManifests);
        result.modules = std::move(sortedModules);
    }

    return result;
}

const IModuleFactory* ModuleComposer::findFactory(const std::string& requestedModuleId,
                                                  const ModuleCreateOptions& options) const
{
    for (const std::shared_ptr<IModuleFactory>& factory : factories_) {
        if (factory && factory->supports(requestedModuleId, options))
            return factory.get();
    }
    return nullptr;
}

} // namespace module
} // namespace fusiondesk
