#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/module/module_factory.h"
#include "fusiondesk/modules/feature/feature_module_factory.h"

using namespace fusiondesk;

namespace {

class FakeModule : public module::IModule
{
public:
    explicit FakeModule(module::ModuleManifest manifest)
        : manifest_(std::move(manifest))
    {
    }

    const module::ModuleManifest& manifest() const override
    {
        return manifest_;
    }

    module::ModuleState state() const override
    {
        return state_;
    }

    bool attach(const module::ModuleRuntime&) override
    {
        state_ = module::ModuleState::Attached;
        return true;
    }

    bool start(const module::ModuleStartOptions&) override
    {
        state_ = module::ModuleState::Running;
        return true;
    }

    void stop(const module::ModuleStopOptions&) override
    {
        state_ = module::ModuleState::Stopped;
    }

    void detach() override
    {
        state_ = module::ModuleState::Detached;
    }

    std::string diagnostics() const override
    {
        return manifest_.moduleId;
    }

private:
    module::ModuleManifest manifest_;
    module::ModuleState state_ = module::ModuleState::Created;
};

class FakeFactory : public module::IModuleFactory
{
public:
    FakeFactory(std::string requestId, module::ModuleManifest manifest, bool canCreate = true)
        : requestId_(std::move(requestId))
        , manifest_(std::move(manifest))
        , canCreate_(canCreate)
    {
    }

    bool supports(const std::string& requestedModuleId,
                  const module::ModuleCreateOptions&) const override
    {
        return requestedModuleId == requestId_ || requestedModuleId == manifest_.moduleId;
    }

    module::ModuleManifest manifest(const module::ModuleCreateOptions&) const override
    {
        return manifest_;
    }

    std::shared_ptr<module::IModule> create(const module::ModuleCreateOptions&) const override
    {
        if (!canCreate_)
            return nullptr;

        return std::make_shared<FakeModule>(manifest_);
    }

private:
    std::string requestId_;
    module::ModuleManifest manifest_;
    bool canCreate_ = true;
};

module::ModuleManifest makeManifest(const std::string& moduleId)
{
    module::ModuleManifest manifest;
    manifest.moduleId = moduleId;
    manifest.displayName = moduleId;
    manifest.version = module::ModuleVersion{1, 2, 0};
    manifest.roleFlags = module::ModuleRoleClient | module::ModuleRoleAgent;
    return manifest;
}

bool containsString(const std::vector<std::string>& values, const std::string& expected)
{
    for (const std::string& value : values) {
        if (value == expected)
            return true;
    }
    return false;
}

void resolvesAliasToConcreteModuleInProfileOrder()
{
    module::ModuleManifest display = makeManifest("display.screen.client");
    module::ModuleManifest input = makeManifest("input.mouse");

    module::ModuleComposer composer;
    composer.addFactory(std::make_shared<FakeFactory>("display.screen", display));
    composer.addFactory(std::make_shared<FakeFactory>("input.mouse", input));

    module::ModuleCompositionRequest request;
    request.requiredModules = {"display.screen", "input.mouse"};
    request.createOptions.role = session::SessionRole::Client;
    request.createOptions.localPlatform = "windows";

    const module::ModuleCompositionResult result = composer.compose(request);
    assert(result.ok());
    assert(result.modules.size() == 2);
    assert(result.manifests.size() == 2);
    assert(result.manifests[0].moduleId == "display.screen.client");
    assert(result.manifests[1].moduleId == "input.mouse");
    assert(result.modules[0]->manifest().moduleId == "display.screen.client");
    assert(result.modules[1]->manifest().moduleId == "input.mouse");
}

void deduplicatesConcreteModuleIds()
{
    module::ModuleManifest display = makeManifest("display.screen.client");

    module::ModuleComposer composer;
    composer.addFactory(std::make_shared<FakeFactory>("display.screen", display));

    module::ModuleCompositionRequest request;
    request.requiredModules = {"display.screen", "display.screen.client"};
    request.createOptions.role = session::SessionRole::Client;

    const module::ModuleCompositionResult result = composer.compose(request);
    assert(result.ok());
    assert(result.modules.size() == 1);
    assert(result.manifests.size() == 1);
    assert(result.manifests.front().moduleId == "display.screen.client");
}

void reportsMissingFactoryAndCreateFailure()
{
    module::ModuleComposer composer;
    composer.addFactory(std::make_shared<FakeFactory>("display.screen",
                                                     makeManifest("display.screen.client"),
                                                     false));

    module::ModuleCompositionRequest request;
    request.requiredModules = {"display.screen", "unknown.redirect"};
    request.createOptions.role = session::SessionRole::Client;

    const module::ModuleCompositionResult result = composer.compose(request);
    assert(!result.ok());
    assert(result.modules.empty());
    assert(containsString(result.missingModules, "display.screen.client"));
    assert(containsString(result.missingModules, "unknown.redirect"));
}

void reportsMissingModuleDependencies()
{
    module::ModuleManifest feature = makeManifest("peripheral.usb");
    feature.requiredModules = {"peripheral.bridge"};

    module::ModuleComposer composer;
    composer.addFactory(std::make_shared<FakeFactory>("peripheral.usb", feature));

    module::ModuleCompositionRequest request;
    request.requiredModules = {"peripheral.usb"};
    request.createOptions.role = session::SessionRole::Client;

    const module::ModuleCompositionResult result = composer.compose(request);
    assert(!result.ok());
    assert(result.modules.size() == 1);
    assert(result.dependencyFailures.size() == 1);
    assert(result.dependencyFailures.front() == "peripheral.usb requires peripheral.bridge");
}

void ordersDependenciesBeforeDependents()
{
    module::ModuleManifest feature = makeManifest("feature.main");
    feature.requiredModules = {"feature.base"};
    module::ModuleManifest base = makeManifest("feature.base");

    module::ModuleComposer composer;
    composer.addFactory(std::make_shared<FakeFactory>("feature.main", feature));
    composer.addFactory(std::make_shared<FakeFactory>("feature.base", base));

    module::ModuleCompositionRequest request;
    request.requiredModules = {"feature.main", "feature.base"};
    request.createOptions.role = session::SessionRole::Client;

    const module::ModuleCompositionResult result = composer.compose(request);
    assert(result.ok());
    assert(result.modules.size() == 2);
    assert(result.manifests[0].moduleId == "feature.base");
    assert(result.manifests[1].moduleId == "feature.main");
    assert(result.modules[0]->manifest().moduleId == "feature.base");
    assert(result.modules[1]->manifest().moduleId == "feature.main");
}

void reportsDependencyCycles()
{
    module::ModuleManifest first = makeManifest("feature.first");
    first.requiredModules = {"feature.second"};
    module::ModuleManifest second = makeManifest("feature.second");
    second.requiredModules = {"feature.first"};

    module::ModuleComposer composer;
    composer.addFactory(std::make_shared<FakeFactory>("feature.first", first));
    composer.addFactory(std::make_shared<FakeFactory>("feature.second", second));

    module::ModuleCompositionRequest request;
    request.requiredModules = {"feature.first", "feature.second"};
    request.createOptions.role = session::SessionRole::Client;

    const module::ModuleCompositionResult result = composer.compose(request);
    assert(!result.ok());
    assert(result.modules.size() == 2);
    assert(!result.dependencyFailures.empty());
    assert(containsString(result.dependencyFailures, "feature.first has cyclic dependency"));
}

void rejectsModulesOutsideProfileVersionConstraints()
{
    module::ModuleManifest display = makeManifest("display.screen.client");

    module::ModuleComposer composer;
    composer.addFactory(std::make_shared<FakeFactory>("display.screen", display));

    module::ModuleCompositionRequest request;
    request.requiredModules = {"display.screen"};
    request.createOptions.role = session::SessionRole::Client;
    request.versionConstraints.push_back(module::ModuleVersionConstraint{
        "display.screen",
        module::ModuleVersion{2, 0, 0},
        module::ModuleVersion{2, 9, 99}});

    const module::ModuleCompositionResult result = composer.compose(request);
    assert(!result.ok());
    assert(result.modules.empty());
    assert(result.manifests.empty());
    assert(result.versionFailures.size() == 1);
    assert(result.versionFailures.front().find("display.screen.client version 1.2.0") != std::string::npos);
}

void acceptsConcreteModuleVersionConstraints()
{
    module::ModuleManifest display = makeManifest("display.screen.client");

    module::ModuleComposer composer;
    composer.addFactory(std::make_shared<FakeFactory>("display.screen", display));

    module::ModuleCompositionRequest request;
    request.requiredModules = {"display.screen"};
    request.createOptions.role = session::SessionRole::Client;
    request.versionConstraints.push_back(module::ModuleVersionConstraint{
        "display.screen.client",
        module::ModuleVersion{1, 0, 0},
        module::ModuleVersion{1, 9, 99}});

    const module::ModuleCompositionResult result = composer.compose(request);
    assert(result.ok());
    assert(result.modules.size() == 1);
    assert(result.versionFailures.empty());
}

void rejectsDuplicateConcreteWhenAliasConstraintFails()
{
    module::ModuleManifest display = makeManifest("display.screen.client");

    module::ModuleComposer composer;
    composer.addFactory(std::make_shared<FakeFactory>("display.screen", display));

    for (const std::vector<std::string>& requiredModules : {
             std::vector<std::string>{"display.screen", "display.screen.client"},
             std::vector<std::string>{"display.screen.client", "display.screen"}}) {
        module::ModuleCompositionRequest request;
        request.requiredModules = requiredModules;
        request.createOptions.role = session::SessionRole::Client;
        request.versionConstraints.push_back(module::ModuleVersionConstraint{
            "display.screen",
            module::ModuleVersion{2, 0, 0},
            module::ModuleVersion{2, 9, 99}});

        const module::ModuleCompositionResult result = composer.compose(request);
        assert(!result.ok());
        assert(result.modules.empty());
        assert(result.manifests.empty());
        assert(result.versionFailures.size() == 1);
        assert(result.versionFailures.front().find("display.screen.client version 1.2.0") != std::string::npos);
    }
}

void composesFeatureFamilyFactoriesBeyondDisplay()
{
    module::ModuleComposer composer;
    for (std::shared_ptr<module::IModuleFactory> factory :
         modules::feature::makeDefaultFeatureModuleFactories()) {
        composer.addFactory(std::move(factory));
    }

    module::ModuleCompositionRequest request;
    request.requiredModules = {
        "audio.desktop",
        "filesystem.redirect",
        "input.mouse",
        "camera.redirect",
    };
    request.createOptions.role = session::SessionRole::Client;
    request.createOptions.localPlatform = "windows";

    const module::ModuleCompositionResult result = composer.compose(request);
    assert(result.ok());
    assert(result.modules.size() == request.requiredModules.size());
    assert(result.manifests.size() == request.requiredModules.size());
    assert(result.manifests[0].moduleId == "audio.desktop");
    assert(result.manifests[1].moduleId == "filesystem.redirect");
    assert(result.manifests[2].moduleId == "input.mouse");
    assert(result.manifests[3].moduleId == "camera.redirect");
    assert(result.modules[0]->diagnostics().find("audio.desktop") != std::string::npos);
    assert(result.modules[3]->diagnostics().find("camera.redirect") != std::string::npos);
}

} // namespace

int main()
{
    resolvesAliasToConcreteModuleInProfileOrder();
    deduplicatesConcreteModuleIds();
    reportsMissingFactoryAndCreateFailure();
    reportsMissingModuleDependencies();
    ordersDependenciesBeforeDependents();
    reportsDependencyCycles();
    rejectsModulesOutsideProfileVersionConstraints();
    acceptsConcreteModuleVersionConstraints();
    rejectsDuplicateConcreteWhenAliasConstraintFails();
    composesFeatureFamilyFactoriesBeyondDisplay();
    return 0;
}
