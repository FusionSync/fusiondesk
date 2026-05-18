#ifndef FUSIONDESK_MODULES_DISPLAY_DISPLAY_FACTORY_H
#define FUSIONDESK_MODULES_DISPLAY_DISPLAY_FACTORY_H

#include <memory>

#include "fusiondesk/core/module/module_factory.h"
#include "fusiondesk/modules/display/display_interfaces.h"

namespace fusiondesk {
namespace modules {
namespace display {

struct DisplayModuleDependencies
{
    std::shared_ptr<IDisplayCapture> capture;
    std::shared_ptr<IVideoEncoder> encoder;
    std::shared_ptr<IVideoDecoder> decoder;
    std::shared_ptr<IDisplayRenderer> renderer;
    DisplayCaptureOpenOptions captureOptions;
    DisplayCodecRuntimeInfo encoderCodec;
    DisplayCodecRuntimeInfo decoderCodec;
};

class DisplayModuleFactory : public module::IModuleFactory
{
public:
    explicit DisplayModuleFactory(DisplayModuleDependencies dependencies);

    bool supports(const std::string& requestedModuleId,
                  const module::ModuleCreateOptions& options) const override;
    module::ModuleManifest manifest(const module::ModuleCreateOptions& options) const override;
    std::shared_ptr<module::IModule> create(const module::ModuleCreateOptions& options) const override;

private:
    DisplayModuleDependencies dependencies_;
};

} // namespace display
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_DISPLAY_DISPLAY_FACTORY_H
