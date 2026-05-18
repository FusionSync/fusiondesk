#include "fusiondesk/modules/clipboard/clipboard_modules.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "fusiondesk/core/network/network_manager.h"
#include "fusiondesk/modules/clipboard/clipboard_factory.h"
#include "fusiondesk/modules/clipboard/clipboard_large_data_scheduler.h"
#include "clipboard_module_policy_internal.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

const char* stateName(module::ModuleState state)
{
    switch (state) {
    case module::ModuleState::Created:
        return "created";
    case module::ModuleState::Attached:
        return "attached";
    case module::ModuleState::Starting:
        return "starting";
    case module::ModuleState::Running:
        return "running";
    case module::ModuleState::Stopping:
        return "stopping";
    case module::ModuleState::Stopped:
        return "stopped";
    case module::ModuleState::Detached:
        return "detached";
    case module::ModuleState::Failed:
        return "failed";
    }
    return "unknown";
}

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

bool affectsClipboardChannel(const module::ModuleReconnectOptions& options)
{
    if (options.affectedChannels.empty())
        return true;

    const network::ChannelKey clipboard = clipboardSmallDataChannelKey();
    const network::ChannelKey clipboardLargeData = clipboardLargeDataChannelKey();
    for (const network::ChannelKey& key : options.affectedChannels) {
        if (key == clipboard || key == clipboardLargeData)
            return true;
    }
    return false;
}

void publish(const module::ModuleRuntime& runtime,
             const std::string& moduleId,
             const std::string& code,
             const std::string& message)
{
    if (runtime.diagnostics == nullptr)
        return;

    diagnostics::DiagnosticEvent event;
    event.sessionId = runtime.session.sessionId;
    event.traceId = runtime.session.traceId;
    event.moduleId = moduleId;
    event.channel = clipboardSmallDataChannelKey();
    event.severity = diagnostics::DiagnosticSeverity::Info;
    event.code = code;
    event.message = message;
    event.policyVersion = runtime.session.policyVersion;
    runtime.diagnostics->publish(event);
}

protocol::ResponseStatus sendStatusToResponseStatus(network::SendStatus status)
{
    switch (status) {
    case network::SendStatus::Sent:
        return protocol::ResponseStatus::Ok;
    case network::SendStatus::ChannelClosed:
    case network::SendStatus::ChannelNotFound:
        return protocol::ResponseStatus::ChannelUnavailable;
    case network::SendStatus::BackPressure:
        return protocol::ResponseStatus::BackPressure;
    case network::SendStatus::Failed:
        return protocol::ResponseStatus::Failed;
    }
    return protocol::ResponseStatus::Failed;
}

void filterFormatsByPolicy(const ClipboardPolicy& policy,
                           FdclFormatList& list)
{
    list.formats.erase(
        std::remove_if(list.formats.begin(),
                       list.formats.end(),
                       [&policy](const FdclFormatRecord& record) {
                           return !policyAllowsCanonicalFormat(
                               policy,
                               record.canonicalFormat);
                       }),
        list.formats.end());

    if (!policy.allowFileContents) {
        for (FdclFormatRecord& record : list.formats) {
            if (record.canonicalFormat == FdclFileListFormat)
                record.canStream = false;
        }
    }
}

bool transferOriginRequiresDragPolicy(TransferOrigin origin)
{
    return origin == TransferOrigin::Drag ||
           origin == TransferOrigin::Drop;
}

bool isClipboardLargeDataPacket(const protocol::PacketEnvelope& packet)
{
    const network::ChannelKey largeData = clipboardLargeDataChannelKey();
    return packet.packetType == protocol::PacketType::Clipboard &&
           packet.channelId == largeData.channelId &&
           packet.channelType == largeData.channelType;
}

} // namespace

ClipboardModuleBase::ClipboardModuleBase(module::ModuleManifest manifest,
                                         ClipboardModuleDependencies dependencies)
    : manifest_(std::move(manifest)),
      dependencies_(std::move(dependencies))
{
    if (dependencies_.sourceRegistry == nullptr)
        dependencies_.sourceRegistry = std::make_shared<InMemoryTransferSourceRegistry>();
    if (dependencies_.formatMapper == nullptr)
        dependencies_.formatMapper = std::make_shared<DefaultTransferFormatMapper>();
    if (dependencies_.transcoder == nullptr)
        dependencies_.transcoder = std::make_shared<IdentityTransferTranscoder>();

    snapshot_.moduleId = manifest_.moduleId;
}

const module::ModuleManifest& ClipboardModuleBase::manifest() const
{
    return manifest_;
}

module::ModuleState ClipboardModuleBase::state() const
{
    return state_;
}

bool ClipboardModuleBase::attach(const module::ModuleRuntime& runtime)
{
    runtime_ = runtime;
    state_ = module::ModuleState::Attached;
    snapshot_.state = state_;
    return !manifest_.moduleId.empty();
}

bool ClipboardModuleBase::start(const module::ModuleStartOptions&)
{
    if (runtime_.network == nullptr && runtime_.networkManager == nullptr) {
        state_ = module::ModuleState::Failed;
        snapshot_.state = state_;
        return false;
    }

    state_ = module::ModuleState::Running;
    snapshot_.state = state_;
    publish(runtime_, manifest_.moduleId, "clipboard.started", "clipboard module started");
    return true;
}

void ClipboardModuleBase::stop(const module::ModuleStopOptions&)
{
    releaseAllLargeDataReservations();
    if (dependencies_.sourceRegistry != nullptr) {
        snapshot_.objectLocksReleased += static_cast<int>(
            dependencies_.sourceRegistry->releaseAllLocks());
        dependencies_.sourceRegistry->clearAll();
    }
    snapshot_.activeLocalDragSessionId = 0;
    snapshot_.activeRemoteDragSessionId = 0;
    state_ = module::ModuleState::Stopped;
    snapshot_.state = state_;
    publish(runtime_, manifest_.moduleId, "clipboard.stopped", "clipboard module stopped");
}

void ClipboardModuleBase::detach()
{
    releaseAllLargeDataReservations();
    if (dependencies_.sourceRegistry != nullptr) {
        snapshot_.objectLocksReleased += static_cast<int>(
            dependencies_.sourceRegistry->releaseAllLocks());
        dependencies_.sourceRegistry->clearAll();
    }
    snapshot_.activeLocalDragSessionId = 0;
    snapshot_.activeRemoteDragSessionId = 0;
    state_ = module::ModuleState::Detached;
    snapshot_.state = state_;
}

void ClipboardModuleBase::pauseForReconnect(const module::ModuleReconnectOptions& options)
{
    if (!affectsClipboardChannel(options))
        return;

    releaseAllLargeDataReservations();
    const std::size_t cancelled = readRequests_.cancelByChannel(
        clipboardSmallDataChannelKey().channelId,
        clipboardSmallDataChannelKey().channelType,
        protocol::ResponseStatus::ChannelUnavailable);
    if (dependencies_.sourceRegistry != nullptr) {
        snapshot_.objectLocksReleased += static_cast<int>(
            dependencies_.sourceRegistry->releaseAllLocks());
        dependencies_.sourceRegistry->clearAll();
    }
    snapshot_.activeLocalDragSessionId = 0;
    snapshot_.activeRemoteDragSessionId = 0;
    snapshot_.timeoutFailures += static_cast<int>(cancelled);
    publish(runtime_, manifest_.moduleId, "clipboard.reconnect_paused", "clipboard module paused");
}

void ClipboardModuleBase::resumeAfterReconnect(const module::ModuleReconnectOptions& options)
{
    if (state_ == module::ModuleState::Running &&
        options.requestFreshState &&
        affectsClipboardChannel(options) &&
        snapshot_.localBundle.offerId != 0) {
        announceLocalBundle(snapshot_.localBundle);
    }
    publish(runtime_, manifest_.moduleId, "clipboard.reconnect_resumed", "clipboard module resumed");
}

void ClipboardModuleBase::handlePacket(const protocol::PacketEnvelope& packet)
{
    if (state_ != module::ModuleState::Running ||
        packet.packetType != protocol::PacketType::Clipboard)
        return;

    if (packet.messageKind == protocol::MessageKind::Ack) {
        handleLargeDataAck(packet);
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Response ||
        packet.messageKind == protocol::MessageKind::Error) {
        handleReadResponse(packet);
        return;
    }

    const FdclDecodeResult decoded = decodeFdclPayload(packet.payload);
    if (!decoded.ok) {
        ++snapshot_.decodeFailures;
        if (packet.messageKind == protocol::MessageKind::Request)
            sendErrorResponse(packet, protocol::ResponseStatus::ProtocolError, decoded.error);
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Event &&
        decoded.operation == FdclOperation::FormatList) {
        handleFormatList(packet, decoded);
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Event &&
        decoded.operation == FdclOperation::DragStart) {
        handleDragStart(decoded);
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Event &&
        decoded.operation == FdclOperation::DragMove) {
        handleDragMove(decoded);
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Event &&
        decoded.operation == FdclOperation::DragDrop) {
        handleDragDrop(decoded);
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Event &&
        decoded.operation == FdclOperation::DragCancel) {
        handleDragCancel(decoded);
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Cancel &&
        decoded.operation == FdclOperation::Cancel) {
        handleCancel(decoded);
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Request &&
        decoded.operation == FdclOperation::ReadFormatRequest) {
        handleReadRequest(packet, decoded);
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Request &&
        decoded.operation == FdclOperation::FileRangeRequest) {
        handleFileRangeRequest(packet, decoded);
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Request &&
        decoded.operation == FdclOperation::LockObject) {
        handleObjectLockRequest(packet, decoded);
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Request &&
        decoded.operation == FdclOperation::UnlockObject) {
        handleObjectUnlockRequest(packet, decoded);
        return;
    }

    ++snapshot_.decodeFailures;
    if (packet.messageKind == protocol::MessageKind::Request)
        sendErrorResponse(packet, protocol::ResponseStatus::Unsupported, "FDCL operation is unsupported");
}

std::string ClipboardModuleBase::diagnostics() const
{
    return std::string("clipboard.") + roleName() +
           " id=" + manifest_.moduleId +
           " state=" + stateName(state_) +
           " formatListsSent=" + std::to_string(snapshot_.formatListsSent) +
           " formatListsReceived=" + std::to_string(snapshot_.formatListsReceived) +
           " readRequestsSent=" + std::to_string(snapshot_.readRequestsSent) +
           " readRequestsReceived=" + std::to_string(snapshot_.readRequestsReceived) +
           " inlineResponsesSent=" + std::to_string(snapshot_.inlineResponsesSent) +
           " inlineResponsesReceived=" + std::to_string(snapshot_.inlineResponsesReceived) +
           " inlineBytesSent=" + std::to_string(snapshot_.inlineBytesSent) +
           " inlineBytesReceived=" + std::to_string(snapshot_.inlineBytesReceived) +
           " fileRangeRequestsSent=" + std::to_string(snapshot_.fileRangeRequestsSent) +
           " fileRangeRequestsReceived=" + std::to_string(snapshot_.fileRangeRequestsReceived) +
           " fileRangeResponsesSent=" + std::to_string(snapshot_.fileRangeResponsesSent) +
           " fileRangeResponsesReceived=" + std::to_string(snapshot_.fileRangeResponsesReceived) +
           " fileRangeSmallDataResponsesSent=" + std::to_string(snapshot_.fileRangeSmallDataResponsesSent) +
           " fileRangeLargeDataResponsesSent=" + std::to_string(snapshot_.fileRangeLargeDataResponsesSent) +
           " objectLockRequestsSent=" + std::to_string(snapshot_.objectLockRequestsSent) +
           " objectLockRequestsReceived=" + std::to_string(snapshot_.objectLockRequestsReceived) +
           " objectLockResponsesSent=" + std::to_string(snapshot_.objectLockResponsesSent) +
           " objectLockResponsesReceived=" + std::to_string(snapshot_.objectLockResponsesReceived) +
           " objectUnlockRequestsSent=" + std::to_string(snapshot_.objectUnlockRequestsSent) +
           " objectUnlockRequestsReceived=" + std::to_string(snapshot_.objectUnlockRequestsReceived) +
           " objectUnlockResponsesSent=" + std::to_string(snapshot_.objectUnlockResponsesSent) +
           " objectUnlockResponsesReceived=" + std::to_string(snapshot_.objectUnlockResponsesReceived) +
           " objectLocksReleased=" + std::to_string(snapshot_.objectLocksReleased) +
           " fileRangeBytesSent=" + std::to_string(snapshot_.fileRangeBytesSent) +
           " fileRangeBytesReceived=" + std::to_string(snapshot_.fileRangeBytesReceived) +
           " dragStartsSent=" + std::to_string(snapshot_.dragStartsSent) +
           " dragStartsReceived=" + std::to_string(snapshot_.dragStartsReceived) +
           " dragMovesReceived=" + std::to_string(snapshot_.dragMovesReceived) +
           " dragDropsReceived=" + std::to_string(snapshot_.dragDropsReceived) +
           " activeLocalDragSessionId=" + std::to_string(snapshot_.activeLocalDragSessionId) +
           " activeRemoteDragSessionId=" + std::to_string(snapshot_.activeRemoteDragSessionId) +
           " cancelsSent=" + std::to_string(snapshot_.cancelsSent) +
           " cancelsReceived=" + std::to_string(snapshot_.cancelsReceived) +
           " cancelMisses=" + std::to_string(snapshot_.cancelMisses) +
           " policyDenials=" + std::to_string(snapshot_.policyDenials) +
           " backPressureFailures=" + std::to_string(snapshot_.backPressureFailures) +
           " largeDataAcksSent=" + std::to_string(snapshot_.largeDataAcksSent) +
           " largeDataAcksReceived=" + std::to_string(snapshot_.largeDataAcksReceived) +
           " largeDataAckMisses=" + std::to_string(snapshot_.largeDataAckMisses) +
           " pendingLargeDataResponses=" + std::to_string(snapshot_.pendingLargeDataResponses) +
           " staleOfferFailures=" + std::to_string(snapshot_.staleOfferFailures) +
           " timeoutFailures=" + std::to_string(snapshot_.timeoutFailures) +
           " decodeFailures=" + std::to_string(snapshot_.decodeFailures);
}

bool ClipboardModuleBase::announceLocalSnapshot()
{
    if (dependencies_.endpoint == nullptr)
        return false;

    const ClipboardSnapshot snapshot = dependencies_.endpoint->snapshot();
    return announceLocalBundle(snapshot.bundle);
}

bool ClipboardModuleBase::announceLocalBundle(TransferSourceBundle bundle)
{
    if (state_ != module::ModuleState::Running)
        return false;

    if (!dependencies_.policy.allowAnnounce ||
        (transferOriginRequiresDragPolicy(bundle.origin) &&
         !dependencies_.policy.allowDrag)) {
        ++snapshot_.policyDenials;
        return false;
    }

    if (bundle.originSessionId == 0)
        bundle.originSessionId = runtime_.session.sessionId;

    if (!dependencies_.policy.allowPresentationMetadata)
        bundle.presentation.reset();

    FdclFormatList list = makeFormatListFromBundle(bundle);
    if (list.formats.empty()) {
        ++snapshot_.decodeFailures;
        return false;
    }
    filterFormatsByPolicy(dependencies_.policy, list);
    if (list.formats.empty()) {
        ++snapshot_.policyDenials;
        return false;
    }

    protocol::PacketEnvelope packet = makePacket(protocol::MessageKind::Event);
    packet.priority = protocol::PacketPriority::Normal;
    packet.flags = protocol::PacketFlagNoResponseRequired;
    packet.sequence = bundle.sequence;
    packet.monotonicTimestampUsec = monotonicNowUsec();
    packet.payload = encodeFdclFormatList(list);

    if (dependencies_.sourceRegistry != nullptr) {
        const protocol::ResponseStatus stored =
            dependencies_.sourceRegistry->store(bundle);
        if (stored != protocol::ResponseStatus::Ok) {
            ++snapshot_.decodeFailures;
            return false;
        }
    }

    if (!sendPacket(packet)) {
        if (dependencies_.sourceRegistry != nullptr)
            dependencies_.sourceRegistry->clearOffer(bundle.offerId);
        return false;
    }

    snapshot_.localBundle = std::move(bundle);
    snapshot_.activeLocalDragSessionId = 0;
    ++snapshot_.formatListsSent;
    publish(runtime_, manifest_.moduleId, "clipboard.format_list_sent", "clipboard format list sent");
    return true;
}

ClipboardModuleSnapshot ClipboardModuleBase::snapshot() const
{
    ClipboardModuleSnapshot copy = snapshot_;
    copy.state = state_;
    copy.pendingReads = readRequests_.pendingCount();
    copy.pendingLargeDataResponses = largeDataReservations_.size();
    if (dependencies_.largeDataWindow != nullptr) {
        const ClipboardLargeDataWindowSnapshot window =
            dependencies_.largeDataWindow->snapshot();
        copy.largeDataWindowBytes = window.maxInFlightBytes;
        copy.largeDataInFlightBytes = window.inFlightBytes;
    }
    return copy;
}

protocol::PacketEnvelope ClipboardModuleBase::makePacket(protocol::MessageKind kind) const
{
    protocol::PacketEnvelope packet;
    packet.sessionId = runtime_.session.sessionId;
    packet.traceId = runtime_.session.traceId;
    packet.channelId = clipboardSmallDataChannelKey().channelId;
    packet.channelType = clipboardSmallDataChannelKey().channelType;
    packet.packetType = protocol::PacketType::Clipboard;
    packet.messageKind = kind;
    packet.priority = protocol::PacketPriority::Normal;
    return packet;
}

bool ClipboardModuleBase::sendPacket(const protocol::PacketEnvelope& packet)
{
    if (runtime_.networkManager != nullptr) {
        const network::EnqueueResult queued = runtime_.networkManager->enqueue(packet);
        if (!queued.queued()) {
            ++snapshot_.sendFailures;
            return false;
        }
        const network::SendResult sent = runtime_.networkManager->flushPacket(packet);
        if (sent.status == network::SendStatus::Sent)
            return true;
        ++snapshot_.sendFailures;
        return false;
    }

    if (runtime_.network == nullptr) {
        ++snapshot_.sendFailures;
        return false;
    }

    const network::SendResult sent = runtime_.network->send(packet);
    if (sent.status == network::SendStatus::Sent)
        return true;

    ++snapshot_.sendFailures;
    return false;
}

bool ClipboardModuleBase::sendLargeDataAck(
    const protocol::PacketEnvelope& response)
{
    if (!isClipboardLargeDataPacket(response) || response.messageId == 0)
        return false;

    protocol::PacketEnvelope ack = makePacket(protocol::MessageKind::Ack);
    ack.priority = protocol::PacketPriority::Interactive;
    ack.responseTo = response.messageId;
    ack.correlationId = response.correlationId != 0 ? response.correlationId
                                                     : response.messageId;
    ack.responseStatus = protocol::ResponseStatus::Ok;
    ack.timeoutMs = response.timeoutMs;
    ack.monotonicTimestampUsec = monotonicNowUsec();

    if (!sendPacket(ack))
        return false;

    ++snapshot_.largeDataAcksSent;
    return true;
}

void ClipboardModuleBase::handleLargeDataAck(
    const protocol::PacketEnvelope& ack)
{
    if (ack.responseTo == 0) {
        ++snapshot_.decodeFailures;
        return;
    }

    if (!releaseLargeDataReservation(ack.responseTo)) {
        ++snapshot_.largeDataAckMisses;
        return;
    }

    ++snapshot_.largeDataAcksReceived;
}

void ClipboardModuleBase::trackLargeDataReservation(
    const protocol::PacketEnvelope& response,
    std::uint64_t bytes)
{
    if (response.messageId == 0 || bytes == 0)
        return;

    largeDataReservations_[response.messageId] = bytes;
    snapshot_.pendingLargeDataResponses = largeDataReservations_.size();
}

bool ClipboardModuleBase::releaseLargeDataReservation(
    protocol::MessageId responseMessageId)
{
    auto it = largeDataReservations_.find(responseMessageId);
    if (it == largeDataReservations_.end())
        return false;

    if (dependencies_.largeDataWindow != nullptr)
        dependencies_.largeDataWindow->release(it->second);
    largeDataReservations_.erase(it);
    snapshot_.pendingLargeDataResponses = largeDataReservations_.size();
    if (dependencies_.largeDataWindow != nullptr) {
        const ClipboardLargeDataWindowSnapshot window =
            dependencies_.largeDataWindow->snapshot();
        snapshot_.largeDataWindowBytes = window.maxInFlightBytes;
        snapshot_.largeDataInFlightBytes = window.inFlightBytes;
    }
    return true;
}

void ClipboardModuleBase::releaseAllLargeDataReservations()
{
    if (dependencies_.largeDataWindow != nullptr) {
        for (const auto& item : largeDataReservations_)
            dependencies_.largeDataWindow->release(item.second);
        const ClipboardLargeDataWindowSnapshot window =
            dependencies_.largeDataWindow->snapshot();
        snapshot_.largeDataWindowBytes = window.maxInFlightBytes;
        snapshot_.largeDataInFlightBytes = window.inFlightBytes;
    }
    largeDataReservations_.clear();
    snapshot_.pendingLargeDataResponses = 0;
}

bool ClipboardModuleBase::sendErrorResponse(const protocol::PacketEnvelope& request,
                                            protocol::ResponseStatus status,
                                            const std::string& message)
{
    protocol::PacketEnvelope response = makePacket(protocol::MessageKind::Error);
    response.priority = protocol::PacketPriority::Normal;
    response.messageId = nextResponseMessageId_++;
    response.correlationId = request.correlationId != 0 ? request.correlationId : request.messageId;
    response.responseTo = request.messageId;
    response.timeoutMs = request.timeoutMs;
    response.responseStatus = status;
    response.payload = encodeFdclErrorDetail(FdclErrorDetail{status, message});
    return sendPacket(response);
}

bool ClipboardModuleBase::sendReadResponse(const protocol::PacketEnvelope& request,
                                           const FdclReadFormatRequest& readRequest,
                                           const TransferReadResult& readResult)
{
    if (!readResult.ok())
        return sendErrorResponse(request, readResult.status, readResult.message);

    FdclReadFormatResponse responsePayload;
    responsePayload.bundleId = readRequest.bundleId;
    responsePayload.offerId = readRequest.offerId;
    responsePayload.ownerEpoch = readRequest.ownerEpoch;
    responsePayload.sourceId = readRequest.sourceId;
    responsePayload.itemIndex = readRequest.itemIndex;
    responsePayload.formatId = readRequest.formatId;
    responsePayload.encoding = readResult.encoding;
    responsePayload.canonicalFormat = readResult.canonicalFormat;
    responsePayload.bytes = readResult.bytes;

    protocol::PacketEnvelope response = makePacket(protocol::MessageKind::Response);
    response.priority = protocol::PacketPriority::Normal;
    response.messageId = nextResponseMessageId_++;
    response.correlationId = request.correlationId != 0 ? request.correlationId : request.messageId;
    response.responseTo = request.messageId;
    response.timeoutMs = request.timeoutMs;
    response.responseStatus = protocol::ResponseStatus::Ok;
    response.payload = encodeFdclReadFormatResponse(responsePayload);

    if (!sendPacket(response))
        return false;

    ++snapshot_.inlineResponsesSent;
    snapshot_.inlineBytesSent += readResult.bytes.size();
    return true;
}

bool ClipboardModuleBase::sendFileRangeResponse(
    const protocol::PacketEnvelope& request,
    const FdclFileRangeRequest& rangeRequest,
    const TransferFileRangeResult& rangeResult)
{
    if (!rangeResult.ok())
        return sendErrorResponse(request, rangeResult.status, rangeResult.message);
    if (rangeResult.bytes.size() >
        effectiveMaxFileRangeBytes(dependencies_.policy)) {
        return sendErrorResponse(request,
                                 protocol::ResponseStatus::TooLarge,
                                 "clipboard file range exceeds policy max bytes");
    }

    FdclFileRangeResponse responsePayload;
    responsePayload.bundleId = rangeRequest.bundleId;
    responsePayload.offerId = rangeRequest.offerId;
    responsePayload.ownerEpoch = rangeRequest.ownerEpoch;
    responsePayload.sourceId = rangeRequest.sourceId;
    responsePayload.objectId = rangeRequest.objectId;
    responsePayload.fileIndex = rangeRequest.fileIndex;
    responsePayload.offset = rangeRequest.offset;
    responsePayload.endOfFile = rangeResult.endOfFile;
    responsePayload.bytes = rangeResult.bytes;

    protocol::PacketEnvelope response = makePacket(protocol::MessageKind::Response);
    response.priority = protocol::PacketPriority::Bulk;
    response.messageId = nextResponseMessageId_++;
    response.correlationId = request.correlationId != 0 ? request.correlationId : request.messageId;
    response.responseTo = request.messageId;
    response.timeoutMs = request.timeoutMs;
    response.responseStatus = protocol::ResponseStatus::Ok;
    response.payload = encodeFdclFileRangeResponse(responsePayload);

    ClipboardLargeDataScheduleOptions scheduleOptions;
    scheduleOptions.smallDataFallbackBytes = dependencies_.policy.maxInlineBytes;
    if (dependencies_.largeDataWindow != nullptr) {
        const ClipboardLargeDataWindowSnapshot window =
            dependencies_.largeDataWindow->snapshot();
        scheduleOptions.largeDataWindowBytes = window.maxInFlightBytes;
        scheduleOptions.largeDataInFlightBytes = window.inFlightBytes;
        snapshot_.largeDataWindowBytes = window.maxInFlightBytes;
        snapshot_.largeDataInFlightBytes = window.inFlightBytes;
    }
    const ClipboardLargeDataScheduleResult schedule =
        scheduleClipboardBulkResponse(runtime_.channels,
                                      response,
                                      response.payload.size(),
                                      scheduleOptions);
    if (!schedule.ok()) {
        if (schedule.status == protocol::ResponseStatus::BackPressure)
            ++snapshot_.backPressureFailures;
        return sendErrorResponse(request, schedule.status, schedule.message);
    }
    applyClipboardLargeDataSchedule(schedule, response);

    bool largeDataWindowReserved = false;
    const std::uint64_t largeDataReservationBytes = response.payload.size();
    if (schedule.usesLargeData && dependencies_.largeDataWindow != nullptr) {
        const ClipboardLargeDataWindowReserveResult reserved =
            dependencies_.largeDataWindow->reserve(largeDataReservationBytes);
        if (!reserved.ok()) {
            ++snapshot_.backPressureFailures;
            snapshot_.largeDataInFlightBytes = reserved.inFlightBytes;
            return sendErrorResponse(request,
                                     reserved.status,
                                     reserved.message);
        }
        largeDataWindowReserved = true;
        snapshot_.largeDataInFlightBytes = reserved.inFlightBytes;
    }

    if (!sendPacket(response)) {
        if (largeDataWindowReserved)
            dependencies_.largeDataWindow->release(largeDataReservationBytes);
        return false;
    }
    if (largeDataWindowReserved)
        trackLargeDataReservation(response, largeDataReservationBytes);

    ++snapshot_.fileRangeResponsesSent;
    snapshot_.fileRangeBytesSent += rangeResult.bytes.size();
    if (response.channelId ==
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::LargeData)) {
        ++snapshot_.fileRangeLargeDataResponsesSent;
    } else {
        ++snapshot_.fileRangeSmallDataResponsesSent;
    }
    return true;
}

void ClipboardModuleBase::handleFormatList(const protocol::PacketEnvelope&,
                                           const FdclDecodeResult& decoded)
{
    if (!dependencies_.policy.allowReceive ||
        !dependencies_.policy.allowWriteLocal ||
        (transferOriginRequiresDragPolicy(decoded.formatList.origin) &&
         !dependencies_.policy.allowDrag)) {
        ++snapshot_.policyDenials;
        return;
    }

    if (decoded.formatList.originSessionId == runtime_.session.sessionId &&
        decoded.formatList.originSessionId != 0) {
        ++snapshot_.loopSuppressions;
        return;
    }

    FdclFormatList filteredList = decoded.formatList;
    filterFormatsByPolicy(dependencies_.policy, filteredList);
    if (filteredList.formats.empty()) {
        ++snapshot_.policyDenials;
        return;
    }

    TransferSourceBundle bundle = makeRemoteBundleFromFormatList(filteredList);
    if (!dependencies_.policy.allowPresentationMetadata)
        bundle.presentation.reset();

    if (dependencies_.sourceRegistry != nullptr) {
        const protocol::ResponseStatus stored =
            dependencies_.sourceRegistry->store(bundle);
        if (stored != protocol::ResponseStatus::Ok) {
            ++snapshot_.decodeFailures;
            return;
        }
    }

    snapshot_.remoteBundle = bundle;
    snapshot_.activeRemoteDragSessionId = 0;

    if (dependencies_.endpoint != nullptr) {
        const protocol::ResponseStatus status =
            dependencies_.endpoint->publishBundle(ClipboardPublishRequest{bundle});
        if (status != protocol::ResponseStatus::Ok) {
            if (dependencies_.sourceRegistry != nullptr)
                dependencies_.sourceRegistry->clearOffer(bundle.offerId);
            snapshot_.remoteBundle = {};
            ++snapshot_.policyDenials;
            return;
        }
    }

    ++snapshot_.formatListsReceived;
    publish(runtime_, manifest_.moduleId, "clipboard.format_list_received", "clipboard format list received");
}

ClipboardClientModule::ClipboardClientModule(ClipboardModuleDependencies dependencies)
    : ClipboardModuleBase(clipboardClientManifest(), std::move(dependencies))
{
}

const char* ClipboardClientModule::roleName() const
{
    return "client";
}

ClipboardAgentModule::ClipboardAgentModule(ClipboardModuleDependencies dependencies)
    : ClipboardModuleBase(clipboardAgentManifest(), std::move(dependencies))
{
}

const char* ClipboardAgentModule::roleName() const
{
    return "agent";
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
