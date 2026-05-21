#ifndef FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_MODULES_H
#define FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_MODULES_H

#include <memory>
#include <map>
#include <optional>
#include <string>

#include "fusiondesk/core/module/module.h"
#include "fusiondesk/core/network/request_tracker.h"
#include "fusiondesk/modules/clipboard/clipboard_large_data_scheduler.h"
#include "fusiondesk/modules/clipboard/clipboard_transfer.h"
#include "fusiondesk/modules/clipboard/clipboard_types.h"
#include "fusiondesk/modules/clipboard/fdcl_codec.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

struct ClipboardPolicy
{
    bool allowAnnounce = true;
    bool allowReceive = true;
    bool allowSendContent = true;
    bool allowWriteLocal = true;
    bool allowPresentationMetadata = true;
    bool allowPlainText = true;
    bool allowHtml = true;
    bool allowRtf = true;
    bool allowImage = true;
    bool allowFileList = true;
    bool allowFileContents = true;
    bool allowDrag = true;
    bool allowCustomFormats = false;
    std::uint64_t maxInlineBytes = 1024 * 1024;
    std::uint64_t maxFileRangeBytes = 4ULL * 1024ULL * 1024ULL;
    std::uint64_t maxFileCount = 1024;
    std::uint64_t maxSingleFileBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
};

struct ClipboardModuleDependencies
{
    std::shared_ptr<IClipboardEndpoint> endpoint;
    ClipboardPolicy policy;
    std::shared_ptr<ITransferSourceRegistry> sourceRegistry;
    std::shared_ptr<ITransferFormatMapper> formatMapper;
    std::shared_ptr<ITransferTranscoder> transcoder;
    std::shared_ptr<IRemoteDragCoordinateSink> dragSink;
    std::shared_ptr<ClipboardLargeDataWindow> largeDataWindow;
};

struct ClipboardModuleSnapshot
{
    std::string moduleId;
    module::ModuleState state = module::ModuleState::Created;
    int formatListsSent = 0;
    int formatListsReceived = 0;
    int readRequestsSent = 0;
    int readRequestsReceived = 0;
    int inlineResponsesSent = 0;
    int inlineResponsesReceived = 0;
    int fileRangeRequestsSent = 0;
    int fileRangeRequestsReceived = 0;
    int fileRangeResponsesSent = 0;
    int fileRangeResponsesReceived = 0;
    int fileRangeSmallDataResponsesSent = 0;
    int fileRangeLargeDataResponsesSent = 0;
    int objectLockRequestsSent = 0;
    int objectLockRequestsReceived = 0;
    int objectLockResponsesSent = 0;
    int objectLockResponsesReceived = 0;
    int objectUnlockRequestsSent = 0;
    int objectUnlockRequestsReceived = 0;
    int objectUnlockResponsesSent = 0;
    int objectUnlockResponsesReceived = 0;
    int objectLocksReleased = 0;
    int dragStartsSent = 0;
    int dragStartsReceived = 0;
    int dragMovesSent = 0;
    int dragMovesReceived = 0;
    int dragDropsSent = 0;
    int dragDropsReceived = 0;
    int dragCancelsSent = 0;
    int dragCancelsReceived = 0;
    int cancelsSent = 0;
    int cancelsReceived = 0;
    int cancelMisses = 0;
    int dragNativePublicationFailures = 0;
    int policyDenials = 0;
    int tooLargeFailures = 0;
    int staleOfferFailures = 0;
    int decodeFailures = 0;
    int loopSuppressions = 0;
    int timeoutFailures = 0;
    int backPressureFailures = 0;
    int sendFailures = 0;
    int largeDataAcksSent = 0;
    int largeDataAcksReceived = 0;
    int largeDataAckMisses = 0;
    std::uint64_t inlineBytesSent = 0;
    std::uint64_t inlineBytesReceived = 0;
    std::uint64_t fileRangeBytesSent = 0;
    std::uint64_t fileRangeBytesReceived = 0;
    std::uint64_t largeDataWindowBytes = 0;
    std::uint64_t largeDataInFlightBytes = 0;
    std::size_t pendingLargeDataResponses = 0;
    DragSessionId activeLocalDragSessionId = 0;
    DragSessionId activeRemoteDragSessionId = 0;
    std::size_t pendingReads = 0;
    protocol::MessageId lastReadRequestMessageId = 0;
    protocol::MessageId lastReadResponseMessageId = 0;
    protocol::MessageId lastReadResponseCorrelationId = 0;
    protocol::MessageId lastReadResponseTo = 0;
    int readResponseMisses = 0;
    int lastReadResponseKind = 0;
    int lastReadResponseStatus = 0;
    std::size_t lastReadResponsePayloadBytes = 0;
    protocol::MessageId lastFileRangeResponseTo = 0;
    protocol::MessageId lastObjectLockResponseTo = 0;
    protocol::MessageId lastObjectUnlockResponseTo = 0;
    TransferSourceBundle localBundle;
    TransferSourceBundle remoteBundle;
    std::optional<TransferReadResult> lastReadResult;
    std::optional<TransferFileRangeResult> lastFileRangeResult;
    std::optional<TransferObjectLockResult> lastObjectLockResult;
    std::optional<TransferObjectLockResult> lastObjectUnlockResult;
};

struct ClipboardRemoteReadDispatchResult
{
    bool dispatched = false;
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    protocol::MessageId messageId = 0;
    std::string message;
};

class ClipboardModuleBase : public module::IModule, public module::IReconnectAwareModule
{
public:
    ClipboardModuleBase(module::ModuleManifest manifest,
                        ClipboardModuleDependencies dependencies);

    const module::ModuleManifest& manifest() const override;
    module::ModuleState state() const override;
    bool attach(const module::ModuleRuntime& runtime) override;
    bool start(const module::ModuleStartOptions& options) override;
    void stop(const module::ModuleStopOptions& options) override;
    void detach() override;
    void handlePacket(const protocol::PacketEnvelope& packet) override;
    std::string diagnostics() const override;
    void pauseForReconnect(const module::ModuleReconnectOptions& options) override;
    void resumeAfterReconnect(const module::ModuleReconnectOptions& options) override;

    bool announceLocalSnapshot();
    bool announceLocalBundle(TransferSourceBundle bundle);
    bool requestRemoteFormat(const FdclReadFormatRequest& request,
                             std::uint32_t timeoutMs = 1000,
                             std::uint64_t nowUsec = 0);
    ClipboardRemoteReadDispatchResult requestRemoteFormatTracked(
        const FdclReadFormatRequest& request,
        std::uint32_t timeoutMs = 1000,
        std::uint64_t nowUsec = 0);
    ClipboardRemoteReadDispatchResult requestRemoteFileRangeTracked(
        const FdclFileRangeRequest& request,
        std::uint32_t timeoutMs = 1000,
        std::uint64_t nowUsec = 0);
    ClipboardRemoteReadDispatchResult requestRemoteObjectLockTracked(
        const FdclObjectLock& request,
        std::uint32_t timeoutMs = 1000,
        std::uint64_t nowUsec = 0);
    ClipboardRemoteReadDispatchResult requestRemoteObjectUnlockTracked(
        const FdclObjectLock& request,
        std::uint32_t timeoutMs = 1000,
        std::uint64_t nowUsec = 0);
    bool sendCancel(const FdclCancel& cancel,
                    std::uint32_t timeoutMs = 1000,
                    std::uint64_t nowUsec = 0);
    bool sendRemoteDragStart(const FdclDragStart& start);
    bool sendRemoteDragMove(const FdclDragMove& move);
    bool sendRemoteDragDrop(const FdclDragDrop& drop);
    bool sendRemoteDragCancel(const FdclDragCancel& cancel);
    std::size_t expirePendingReads(std::uint64_t nowUsec);
    ClipboardModuleSnapshot snapshot() const;

protected:
    virtual const char* roleName() const = 0;

private:
    protocol::PacketEnvelope makePacket(protocol::MessageKind kind) const;
    bool sendPacket(const protocol::PacketEnvelope& packet);
    bool sendLargeDataAck(const protocol::PacketEnvelope& response);
    void handleLargeDataAck(const protocol::PacketEnvelope& ack);
    void trackLargeDataReservation(const protocol::PacketEnvelope& response,
                                   std::uint64_t bytes);
    bool releaseLargeDataReservation(protocol::MessageId responseMessageId);
    void releaseAllLargeDataReservations();
    bool sendErrorResponse(const protocol::PacketEnvelope& request,
                           protocol::ResponseStatus status,
                           const std::string& message);
    bool sendReadResponse(const protocol::PacketEnvelope& request,
                          const FdclReadFormatRequest& readRequest,
                          const TransferReadResult& readResult);
    bool sendFileRangeResponse(const protocol::PacketEnvelope& request,
                               const FdclFileRangeRequest& rangeRequest,
                               const TransferFileRangeResult& rangeResult);
    bool sendObjectLockResponse(const protocol::PacketEnvelope& request,
                                FdclOperation operation,
                                const FdclObjectLock& lockRequest,
                                const TransferObjectLockResult& lockResult);
    bool sendDragEvent(const protocol::ByteBuffer& payload);
    void handleFormatList(const protocol::PacketEnvelope& packet,
                          const FdclDecodeResult& decoded);
    void handleReadRequest(const protocol::PacketEnvelope& packet,
                           const FdclDecodeResult& decoded);
    void handleFileRangeRequest(const protocol::PacketEnvelope& packet,
                                const FdclDecodeResult& decoded);
    void handleObjectLockRequest(const protocol::PacketEnvelope& packet,
                                 const FdclDecodeResult& decoded);
    void handleObjectUnlockRequest(const protocol::PacketEnvelope& packet,
                                   const FdclDecodeResult& decoded);
    void handleDragStart(const FdclDecodeResult& decoded);
    void handleDragMove(const FdclDecodeResult& decoded);
    void handleDragDrop(const FdclDecodeResult& decoded);
    void handleDragCancel(const FdclDecodeResult& decoded);
    void handleCancel(const FdclDecodeResult& decoded);
    void handleReadResponse(const protocol::PacketEnvelope& packet);
    void handleTrackedReadResponse(const protocol::PacketEnvelope& packet);
    void handleTrackedFileRangeResponse(const protocol::PacketEnvelope& packet);
    void handleTrackedObjectLockResponse(const protocol::PacketEnvelope& packet);
    void handleTrackedObjectUnlockResponse(const protocol::PacketEnvelope& packet);
    TransferReadResult readLocal(const FdclReadFormatRequest& request);
    TransferFileRangeResult readLocalFileRange(
        const FdclFileRangeRequest& request);
    TransferObjectLockResult lockLocalObject(const FdclObjectLock& request);
    TransferObjectLockResult unlockLocalObject(const FdclObjectLock& request);

private:
    module::ModuleManifest manifest_;
    ClipboardModuleDependencies dependencies_;
    module::ModuleRuntime runtime_;
    module::ModuleState state_ = module::ModuleState::Created;
    network::RequestTracker readRequests_;
    std::map<protocol::MessageId, std::uint64_t> largeDataReservations_;
    protocol::MessageId nextResponseMessageId_ = 100000;
    ClipboardModuleSnapshot snapshot_;
};

class ClipboardClientModule : public ClipboardModuleBase
{
public:
    explicit ClipboardClientModule(ClipboardModuleDependencies dependencies = {});

private:
    const char* roleName() const override;
};

class ClipboardAgentModule : public ClipboardModuleBase
{
public:
    explicit ClipboardAgentModule(ClipboardModuleDependencies dependencies = {});

private:
    const char* roleName() const override;
};

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_MODULES_H
