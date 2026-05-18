#include "fusiondesk/runtime/runtime_host.h"

#include <utility>

#include "fusiondesk/core/module/module_factory.h"
#include "fusiondesk/modules/clipboard/clipboard_factory.h"
#include "fusiondesk/modules/display/display_factory.h"
#include "fusiondesk/modules/feature/feature_module_factory.h"
#include "fusiondesk/modules/input/input_factory.h"
#include "fusiondesk/modules/test/test_echo_factory.h"

namespace fusiondesk {
namespace runtime {

namespace {

ProductProfile defaultProfile()
{
    ProductProfile profile;
    profile.profileId = "remote-desktop-display-mvp";
    profile.defaultFeatures.bits = protocol::feature::Display | protocol::feature::Screen;
    profile.requiredModules = {"display.screen"};
    profile.minimumChannels = network::defaultMvpChannelSpecs();
    return profile;
}

bool isClipboardModuleId(const std::string& moduleId)
{
    return moduleId == "clipboard.redirect" ||
           moduleId == "clipboard.redirect.client" ||
           moduleId == "clipboard.redirect.agent";
}

bool profileRequestsClipboard(const ProductProfile& profile)
{
    if (profile.defaultFeatures.has(protocol::feature::Clipboard))
        return true;

    for (const std::string& moduleId : profile.requiredModules) {
        if (isClipboardModuleId(moduleId))
            return true;
    }
    return false;
}

bool hasChannelSpec(const std::vector<network::ChannelSpec>& specs,
                    network::ChannelKey key)
{
    for (const network::ChannelSpec& spec : specs) {
        if (spec.key == key)
            return true;
    }
    return false;
}

void ensureClipboardDataChannels(ProductProfile& profile)
{
    if (!profileRequestsClipboard(profile))
        return;

    const network::ChannelSpec largeData = network::defaultLargeDataChannelSpec();
    if (!hasChannelSpec(profile.minimumChannels, largeData.key))
        profile.minimumChannels.push_back(largeData);
}

} // namespace

RuntimeHost::RuntimeHost()
    : profile_(defaultProfile())
{
}

bool RuntimeHost::initialize(const RuntimeOptions& options)
{
    if (state_ != RuntimeState::Created && state_ != RuntimeState::Stopped)
        return false;

    profile_ = options.profile.profileId.empty() ? defaultProfile() : options.profile;
    if (profile_.minimumChannels.empty())
        profile_.minimumChannels = network::defaultMvpChannelSpecs();
    if (profile_.defaultFeatures.bits == 0)
        profile_.defaultFeatures = defaultProfile().defaultFeatures;
    if (profile_.requiredModules.empty())
        profile_.requiredModules = defaultProfile().requiredModules;
    if (profile_.displayCodecPolicy.codecPreference.empty())
        profile_.displayCodecPolicy.codecPreference =
            defaultProfile().displayCodecPolicy.codecPreference;
    if (profile_.displayCodecPolicy.selectionMode.empty())
        profile_.displayCodecPolicy.selectionMode = "default";
    ensureClipboardDataChannels(profile_);

    state_ = RuntimeState::Initialized;
    publish("runtime.initialized", "runtime initialized");
    return true;
}

RuntimeState RuntimeHost::state() const
{
    return state_;
}

session::SessionManager& RuntimeHost::sessions()
{
    return sessions_;
}

const session::SessionManager& RuntimeHost::sessions() const
{
    return sessions_;
}

diagnostics::DiagnosticsSink& RuntimeHost::diagnostics()
{
    return sessions_.diagnostics();
}

const ProductProfile& RuntimeHost::profile() const
{
    return profile_;
}

ProfileMountReport RuntimeHost::mountProfileModules(session::Session& session,
                                                    const DisplayMvpDependencies& displayDependencies)
{
    ProfileMountReport report;
    module::ModuleHost* host = session.moduleHost();
    if (host == nullptr) {
        report.missingModules.push_back("module.host");
        return report;
    }

    modules::display::DisplayModuleDependencies display;
    display.capture = displayDependencies.capture;
    display.encoder = displayDependencies.encoder;
    display.decoder = displayDependencies.decoder;
    display.renderer = displayDependencies.renderer;
    display.captureOptions = displayDependencies.captureOptions;
    display.encoderCodec = displayDependencies.encoderCodec;
    display.decoderCodec = displayDependencies.decoderCodec;

    module::ModuleCreateOptions createOptions;
    createOptions.role = session.role();
    createOptions.localPlatform = session.context().localPlatform;

    module::ModuleComposer composer;
    composer.addFactory(std::make_shared<modules::display::DisplayModuleFactory>(std::move(display)));
    modules::clipboard::ClipboardModuleDependencies clipboard;
    clipboard.endpoint = displayDependencies.clipboardEndpoint;
    clipboard.dragSink = displayDependencies.clipboardDragSink;
    if (displayDependencies.clipboardPolicy != nullptr)
        clipboard.policy = *displayDependencies.clipboardPolicy;
    else
        clipboard.policy =
            feature::clipboardModulePolicyFromProductPolicy(
                profile_.clipboardPolicy);
    if (clipboard.dragSink == nullptr) {
        clipboard.dragSink =
            std::dynamic_pointer_cast<modules::clipboard::IRemoteDragCoordinateSink>(
                displayDependencies.clipboardEndpoint);
    }
    clipboard.sourceRegistry = displayDependencies.clipboardSourceRegistry;
    clipboard.formatMapper = displayDependencies.clipboardFormatMapper;
    clipboard.transcoder = displayDependencies.clipboardTranscoder;
    clipboard.largeDataWindow = displayDependencies.clipboardLargeDataWindow;
    composer.addFactory(modules::clipboard::makeClipboardModuleFactory(std::move(clipboard)));
    modules::input::InputModuleDependencies input;
    input.capture = displayDependencies.inputCapture;
    input.injector = displayDependencies.inputInjector;
    for (std::shared_ptr<module::IModuleFactory> factory :
         modules::input::makeDefaultInputModuleFactories(std::move(input))) {
        composer.addFactory(std::move(factory));
    }
    for (std::shared_ptr<module::IModuleFactory> factory :
         modules::feature::makeDefaultFeatureModuleFactories()) {
        composer.addFactory(std::move(factory));
    }
    composer.addFactory(modules::test::makeTestEchoModuleFactory());

    module::ModuleCompositionRequest request;
    request.requiredModules = profile_.requiredModules;
    request.versionConstraints = profile_.moduleVersionConstraints;
    request.createOptions = std::move(createOptions);

    module::ModuleCompositionResult composition = composer.compose(request);
    report.missingModules = std::move(composition.missingModules);
    report.dependencyFailures = std::move(composition.dependencyFailures);
    report.versionFailures = std::move(composition.versionFailures);

    for (std::shared_ptr<module::IModule>& module : composition.modules) {
        const std::string moduleId = module->manifest().moduleId;
        if (host->addModule(std::move(module)))
            report.mountedModules.push_back(moduleId);
        else
            report.deniedModules.push_back(moduleId);
    }

    return report;
}

void RuntimeHost::shutdown(const std::string& reason)
{
    if (state_ == RuntimeState::Stopped)
        return;

    state_ = RuntimeState::ShuttingDown;
    publish("runtime.shutdown_started", reason);
    state_ = RuntimeState::Stopped;
    publish("runtime.stopped", "runtime stopped");
}

void RuntimeHost::publish(const std::string& code, const std::string& message)
{
    diagnostics::DiagnosticEvent event;
    event.code = code;
    event.message = message;
    sessions_.diagnostics().publish(event);
}

} // namespace runtime
} // namespace fusiondesk
