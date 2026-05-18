#ifndef FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_SHELL_H
#define FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_SHELL_H

#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/protocol/protocol_types.h"
#include "fusiondesk/modules/clipboard/clipboard_drag_coordinates.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"
#include "fusiondesk/runtime/feature/clipboard_product_policy.h"
#include "fusiondesk/runtime/feature/clipboard_runtime_service.h"

class QTimer;

namespace fusiondesk {
namespace session {
class Session;
} // namespace session

namespace runtime {
namespace qt {
class QtRuntimeTransportManager;
} // namespace qt
} // namespace runtime

namespace apps {
namespace pc {

bool clipboardProfileRequested(int argc, char** argv);

std::string clipboardEndpointKindOptionValue(int argc, char** argv);

bool qtClipboardEndpointRequested(int argc, char** argv);

modules::clipboard::ClipboardPolicy clipboardPolicyOptionValue(int argc,
                                                               char** argv);

runtime::ProductClipboardPolicy productClipboardPolicyOptionValue(
    int argc,
    char** argv,
    runtime::ProductClipboardPolicy policy = {});

modules::clipboard::ClipboardPolicy clipboardPolicyOptionValue(
    int argc,
    char** argv,
    runtime::ProductClipboardPolicy policy);

std::shared_ptr<runtime::feature::IClipboardRuntimePolicy>
makeClipboardRuntimePolicy(
    int argc,
    char** argv,
    const modules::clipboard::ClipboardPolicy& clipboardPolicy);

std::shared_ptr<runtime::feature::IClipboardRuntimePolicy>
makeClipboardRuntimePolicy(const runtime::ProductClipboardPolicy& policy);

std::shared_ptr<modules::clipboard::IClipboardEndpoint>
makeClipboardEndpoint(
    int argc,
    char** argv,
    const modules::clipboard::ClipboardPolicy& policy,
    std::shared_ptr<modules::clipboard::IClipboardRemoteReader> remoteReader,
    std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper>
        dragCoordinateMapper = {});

std::shared_ptr<runtime::feature::IClipboardRuntimeReadPump>
makeClipboardRuntimeReadPump(
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId);

struct PcClipboardRuntimeContext final {
    PcClipboardRuntimeContext();
    ~PcClipboardRuntimeContext();
    PcClipboardRuntimeContext(const PcClipboardRuntimeContext&) = delete;
    PcClipboardRuntimeContext& operator=(const PcClipboardRuntimeContext&) = delete;

    std::unique_ptr<runtime::feature::ClipboardRuntimeService> service;
    std::shared_ptr<runtime::feature::IClipboardRuntimePolicy> policy;
    std::unique_ptr<QTimer> pumpTimer;
};

struct PcClipboardRuntimeStartResult final {
    bool ok = false;
    std::vector<std::string> messages;
};

bool clipboardRuntimeRequested(int argc, char** argv);

bool clipboardTextRequirementRequested(int argc, char** argv);

bool clipboardImagePngRequirementRequested(int argc, char** argv);

bool clipboardFormattedTextRequirementRequested(int argc, char** argv);

bool clipboardFileTextRequirementRequested(int argc, char** argv);

bool clipboardEndpointFileTextRequirementRequested(int argc, char** argv);

bool clipboardTextRequirementSatisfied(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    std::string* errorMessage = nullptr);

bool clipboardImagePngRequirementSatisfied(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    std::string* errorMessage = nullptr);

bool clipboardFormattedTextRequirementSatisfied(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    std::string* errorMessage = nullptr);

bool clipboardFileTextRequirementSatisfied(
    int argc,
    char** argv,
    session::Session& session,
    const std::shared_ptr<modules::clipboard::IClipboardRemoteReader>&
        remoteReader,
    std::string* errorMessage = nullptr);

bool clipboardEndpointFileTextRequirementSatisfied(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    std::string* errorMessage = nullptr);

bool verifyClipboardTextRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint);

bool verifyClipboardImagePngRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint);

bool verifyClipboardFormattedTextRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint);

bool verifyClipboardFileTextRequirement(
    int argc,
    char** argv,
    session::Session& session,
    const std::shared_ptr<modules::clipboard::IClipboardRemoteReader>&
        remoteReader);

bool verifyClipboardEndpointFileTextRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint);

bool seedClipboardTextIfRequested(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint);

bool seedClipboardImagePngIfRequested(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint);

bool seedClipboardFormattedTextIfRequested(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint);

bool seedClipboardFilesIfRequested(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint);

void writeClipboardDiagnosticsIfRequested(
    int argc,
    char** argv,
    session::Session* session,
    runtime::feature::ClipboardRuntimeService* service,
    const std::shared_ptr<runtime::feature::IClipboardRuntimePolicy>& policy,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    const char* phase);

void writeClipboardProductPolicyDiagnosticsIfRequested(
    int argc,
    char** argv,
    const runtime::ProductClipboardPolicy& policy,
    const char* phase);

PcClipboardRuntimeStartResult startClipboardRuntime(
    int argc,
    char** argv,
    session::Session& session,
    std::shared_ptr<modules::clipboard::IClipboardEndpoint> endpoint,
    std::shared_ptr<runtime::feature::IClipboardRuntimePolicy> policy,
    PcClipboardRuntimeContext& context);

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_SHELL_H
