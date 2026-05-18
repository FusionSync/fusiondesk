#ifndef FUSIONDESK_MODULE_MODULE_CATALOG_H
#define FUSIONDESK_MODULE_MODULE_CATALOG_H

#include <cstdint>
#include <vector>

#include "fusiondesk/core/module/module_manifest.h"

namespace fusiondesk {
namespace module {
namespace catalog {

ModuleManifest displayScreen();
ModuleManifest displayScreenAgent();
ModuleManifest displayScreenClient();
std::vector<ModuleManifest> displayScreenRoleManifests();
ModuleManifest clipboardRedirect();
ModuleManifest clipboardRedirectAgent();
ModuleManifest clipboardRedirectClient();
std::vector<ModuleManifest> clipboardRedirectRoleManifests();
ModuleManifest desktopAudio();
ModuleManifest microphone();
ModuleManifest camera();
ModuleManifest filesystem();
ModuleManifest printer();
ModuleManifest keyboard();
ModuleManifest mouse();
ModuleManifest touch();
ModuleManifest gamepad();
ModuleManifest peripheralUsb();

std::vector<ModuleManifest> remoteDesktopSuite();
std::vector<ModuleManifest> remoteDesktopSuiteForRole(std::uint32_t roleFlag);
std::vector<ModuleManifest> redirectionSuite();
std::vector<ModuleManifest> filesystemOnly();
std::vector<ModuleManifest> peripheralSuite();

} // namespace catalog
} // namespace module
} // namespace fusiondesk

#endif // FUSIONDESK_MODULE_MODULE_CATALOG_H
