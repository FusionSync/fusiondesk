#ifndef FUSIONDESK_RUNTIME_RUNTIME_HOST_H
#define FUSIONDESK_RUNTIME_RUNTIME_HOST_H

#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/module/module_manifest.h"
#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/session/session_manager.h"
#include "fusiondesk/modules/display/display_types.h"
#include "fusiondesk/runtime/display/display_codec_selection.h"
#include "fusiondesk/runtime/feature/clipboard_product_policy.h"

namespace fusiondesk {

namespace modules {
namespace clipboard {
class ClipboardLargeDataWindow;
class IClipboardEndpoint;
class IRemoteDragCoordinateSink;
struct ClipboardPolicy;
class ITransferFormatMapper;
class ITransferSourceRegistry;
class ITransferTranscoder;
} // namespace clipboard
namespace display {
class IDisplayCapture;
class IVideoEncoder;
class IVideoDecoder;
class IDisplayRenderer;
} // namespace display
namespace input {
class IInputCapture;
class IInputInjector;
} // namespace input
} // namespace modules

namespace runtime {

struct ProductDisplayCodecPolicy
{
    std::vector<display::DisplayCodecId> codecPreference = {
        display::DisplayCodecId::H264,
        display::DisplayCodecId::H265,
        display::DisplayCodecId::RawBgra};
    bool allowHardware = true;
    bool allowSoftware = true;
    bool preferHardware = true;
    bool preferZeroCopy = true;
    bool enableWindowsMediaFoundationH264 = false;
    bool selectWindowsMediaFoundationH264 = false;
    bool enableH264DeltaFrames = false;
    std::string selectionMode = "default";
};

struct ProductProfile
{
    std::string profileId = "remote-desktop-display-mvp";
    protocol::FeatureSet defaultFeatures;
    std::vector<std::string> requiredModules;
    std::vector<module::ModuleVersionConstraint> moduleVersionConstraints;
    std::vector<network::ChannelSpec> minimumChannels;
    ProductDisplayCodecPolicy displayCodecPolicy;
    ProductClipboardPolicy clipboardPolicy;
};

struct RuntimeOptions
{
    ProductProfile profile;
};

struct DisplayMvpDependencies
{
    std::shared_ptr<modules::display::IDisplayCapture> capture;
    std::shared_ptr<modules::display::IVideoEncoder> encoder;
    std::shared_ptr<modules::display::IVideoDecoder> decoder;
    std::shared_ptr<modules::display::IDisplayRenderer> renderer;
    modules::display::DisplayCaptureOpenOptions captureOptions;
    modules::display::DisplayCodecRuntimeInfo encoderCodec;
    modules::display::DisplayCodecRuntimeInfo decoderCodec;
    std::shared_ptr<modules::input::IInputCapture> inputCapture;
    std::shared_ptr<modules::input::IInputInjector> inputInjector;
    std::shared_ptr<modules::clipboard::IClipboardEndpoint> clipboardEndpoint;
    std::shared_ptr<modules::clipboard::IRemoteDragCoordinateSink> clipboardDragSink;
    std::shared_ptr<modules::clipboard::ClipboardPolicy> clipboardPolicy;
    std::shared_ptr<modules::clipboard::ITransferSourceRegistry> clipboardSourceRegistry;
    std::shared_ptr<modules::clipboard::ITransferFormatMapper> clipboardFormatMapper;
    std::shared_ptr<modules::clipboard::ITransferTranscoder> clipboardTranscoder;
    std::shared_ptr<modules::clipboard::ClipboardLargeDataWindow> clipboardLargeDataWindow;
};

struct ProfileMountReport
{
    std::vector<std::string> mountedModules;
    std::vector<std::string> missingModules;
    std::vector<std::string> dependencyFailures;
    std::vector<std::string> versionFailures;
    std::vector<std::string> deniedModules;

    bool ok() const
    {
        return missingModules.empty() &&
               dependencyFailures.empty() &&
               versionFailures.empty() &&
               deniedModules.empty();
    }
};

enum class RuntimeState
{
    Created,
    Initialized,
    ShuttingDown,
    Stopped
};

class RuntimeHost
{
public:
    RuntimeHost();

    bool initialize(const RuntimeOptions& options = {});
    RuntimeState state() const;
    session::SessionManager& sessions();
    const session::SessionManager& sessions() const;
    diagnostics::DiagnosticsSink& diagnostics();
    const ProductProfile& profile() const;
    ProfileMountReport mountProfileModules(session::Session& session,
                                           const DisplayMvpDependencies& displayDependencies);
    void shutdown(const std::string& reason);

private:
    void publish(const std::string& code, const std::string& message);

private:
    RuntimeState state_ = RuntimeState::Created;
    ProductProfile profile_;
    session::SessionManager sessions_;
};

} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_RUNTIME_HOST_H
