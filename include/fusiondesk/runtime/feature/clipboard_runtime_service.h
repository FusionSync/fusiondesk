#ifndef FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_RUNTIME_SERVICE_H
#define FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_RUNTIME_SERVICE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_transfer.h"
#include "fusiondesk/runtime/feature/clipboard_runtime_policy.h"

namespace fusiondesk {
namespace session {
class Session;
} // namespace session

namespace modules {
namespace clipboard {
class ClipboardModuleBase;
} // namespace clipboard
} // namespace modules

namespace runtime {
namespace feature {

struct ClipboardRuntimeServiceOptions
{
    session::Session* session = nullptr;
    std::shared_ptr<modules::clipboard::IClipboardEndpoint> endpoint;
    std::shared_ptr<IClipboardRuntimePolicy> policy;
};

struct ClipboardRuntimeServiceStartResult
{
    bool ok = false;
    std::vector<std::string> messages;
};

struct ClipboardRuntimePumpResult
{
    bool active = false;
    int announcementsSent = 0;
    int idlePolls = 0;
    int duplicateSnapshots = 0;
    int missingEndpoints = 0;
    int missingModules = 0;
    int policyDenials = 0;
    int sendFailures = 0;
};

struct ClipboardRuntimeExpiryResult
{
    bool active = false;
    std::size_t expiredReads = 0;
    int missingModules = 0;
    int policyDenials = 0;
};

struct ClipboardRuntimeServiceSnapshot
{
    bool active = false;
    bool endpointAttached = false;
    std::uint64_t pumpCount = 0;
    int announcementsSent = 0;
    int idlePolls = 0;
    int duplicateSnapshots = 0;
    int missingEndpoints = 0;
    int missingModules = 0;
    int policyDenials = 0;
    int sendFailures = 0;
    int auditEvents = 0;
    std::uint64_t lastBundleId = 0;
    std::uint64_t lastOfferId = 0;
    std::uint64_t lastOwnerEpoch = 0;
    std::uint64_t lastSequence = 0;
    std::size_t expiredReads = 0;
};

class IClipboardRuntimeReadPump
{
public:
    virtual ~IClipboardRuntimeReadPump() = default;

    virtual void pumpOnce() = 0;
    virtual std::uint64_t monotonicNowUsec() const = 0;
};

struct ClipboardRuntimeRemoteReaderOptions
{
    session::Session* session = nullptr;
    IClipboardRuntimeReadPump* pump = nullptr;
    std::shared_ptr<IClipboardRuntimePolicy> policy;
    std::uint32_t defaultTimeoutMs = 1000;
};

class ClipboardRuntimeRemoteReader final
    : public modules::clipboard::IClipboardRemoteReader,
      public modules::clipboard::IClipboardRemoteFileReader,
      public modules::clipboard::IClipboardRemoteObjectLocker
{
public:
    explicit ClipboardRuntimeRemoteReader(
        ClipboardRuntimeRemoteReaderOptions options);

    modules::clipboard::TransferReadResult readRemoteFormat(
        const modules::clipboard::TransferReadRequest& request,
        std::uint32_t timeoutMs) override;
    modules::clipboard::TransferFileRangeResult readRemoteFileRange(
        const modules::clipboard::TransferFileRangeRequest& request,
        std::uint32_t timeoutMs) override;
    modules::clipboard::TransferObjectLockResult lockRemoteObject(
        const modules::clipboard::TransferObjectLockRequest& request,
        std::uint32_t timeoutMs) override;
    modules::clipboard::TransferObjectLockResult unlockRemoteObject(
        const modules::clipboard::TransferObjectLockRequest& request,
        std::uint32_t timeoutMs) override;

private:
    modules::clipboard::ClipboardModuleBase* clipboardModule() const;
    std::string moduleId() const;
    ClipboardRuntimePolicyContext makePolicyContext(
        const modules::clipboard::TransferReadRequest& request) const;
    ClipboardRuntimePolicyContext makePolicyContext(
        const modules::clipboard::TransferFileRangeRequest& request) const;
    ClipboardRuntimePolicyContext makePolicyContext(
        ClipboardRuntimeOperation operation,
        const modules::clipboard::TransferObjectLockRequest& request) const;

private:
    ClipboardRuntimeRemoteReaderOptions options_;
};

class ClipboardRuntimeService
{
public:
    explicit ClipboardRuntimeService(ClipboardRuntimeServiceOptions options);
    ~ClipboardRuntimeService();

    ClipboardRuntimeServiceStartResult start();
    void stop();

    ClipboardRuntimePumpResult pumpOnce();
    ClipboardRuntimeExpiryResult expirePendingReads(std::uint64_t nowUsec);

    ClipboardRuntimeServiceSnapshot snapshot() const;

private:
    modules::clipboard::ClipboardModuleBase* clipboardModule() const;
    std::string moduleId() const;
    ClipboardRuntimePolicyContext makePolicyContext(
        ClipboardRuntimeOperation operation,
        const modules::clipboard::TransferSourceBundle* bundle = nullptr) const;
    bool authorizeAndAudit(const ClipboardRuntimePolicyContext& context,
                           const ClipboardRuntimePolicyDecision& decision,
                           ClipboardRuntimePumpResult* pumpResult);
    bool authorizeAndAudit(const ClipboardRuntimePolicyContext& context,
                           ClipboardRuntimePumpResult* pumpResult = nullptr);

private:
    ClipboardRuntimeServiceOptions options_;
    ClipboardRuntimeServiceSnapshot snapshot_;
};

} // namespace feature
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_RUNTIME_SERVICE_H
