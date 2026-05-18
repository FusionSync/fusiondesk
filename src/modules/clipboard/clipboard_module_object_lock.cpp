#include "fusiondesk/modules/clipboard/clipboard_modules.h"

#include <chrono>
#include <utility>

#include "clipboard_module_policy_internal.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

TransferObjectLockRequest makeTransferObjectLockRequest(
    const FdclObjectLock& request)
{
    TransferObjectLockRequest result;
    result.bundleId = request.bundleId;
    result.offerId = request.offerId;
    result.ownerEpoch = request.ownerEpoch;
    result.sourceId = request.sourceId;
    result.objectId = request.objectId;
    result.fileIndex = request.fileIndex;
    result.lockId = request.lockId;
    result.leaseUsec = request.leaseUsec;
    return result;
}

TransferObjectLockResult trackedObjectLockResult(
    const protocol::PacketEnvelope& packet,
    FdclOperation expected,
    ClipboardModuleSnapshot& snapshot)
{
    TransferObjectLockResult result;
    result.status = packet.responseStatus;

    if (packet.messageKind == protocol::MessageKind::Response &&
        packet.responseStatus == protocol::ResponseStatus::Ok) {
        const FdclDecodeResult decoded = decodeFdclPayload(packet.payload);
        if (!decoded.ok || decoded.operation != expected) {
            ++snapshot.decodeFailures;
            result.status = protocol::ResponseStatus::ProtocolError;
            result.message = decoded.error.empty()
                                 ? "clipboard object lock response invalid"
                                 : decoded.error;
            return result;
        }

        result.status = protocol::ResponseStatus::Ok;
        result.lockId = decoded.objectLock.lockId;
        result.leaseUsec = decoded.objectLock.leaseUsec;
        return result;
    }

    if (packet.messageKind == protocol::MessageKind::Error) {
        const FdclDecodeResult decoded = decodeFdclPayload(packet.payload);
        if (decoded.ok && decoded.operation == FdclOperation::ErrorDetail) {
            result.status = decoded.errorDetail.status;
            result.message = decoded.errorDetail.message;
        } else if (!decoded.error.empty()) {
            result.message = decoded.error;
        }
        if (result.status == protocol::ResponseStatus::Timeout)
            ++snapshot.timeoutFailures;
        if (result.status == protocol::ResponseStatus::Conflict ||
            result.status == protocol::ResponseStatus::NotFound)
            ++snapshot.staleOfferFailures;
        return result;
    }

    ++snapshot.decodeFailures;
    result.status = protocol::ResponseStatus::ProtocolError;
    result.message = "clipboard object lock completed with invalid message kind";
    return result;
}

} // namespace

ClipboardRemoteReadDispatchResult
ClipboardModuleBase::requestRemoteObjectLockTracked(
    const FdclObjectLock& request,
    std::uint32_t timeoutMs,
    std::uint64_t nowUsec)
{
    ClipboardRemoteReadDispatchResult result;
    if (state_ != module::ModuleState::Running)
        return result;
    if (!dependencies_.policy.allowReceive ||
        !dependencies_.policy.allowFileContents) {
        ++snapshot_.policyDenials;
        result.status = protocol::ResponseStatus::DeniedByPolicy;
        result.message = "clipboard file object lock denied by policy";
        return result;
    }

    std::string validationMessage;
    const protocol::ResponseStatus validation =
        validateObjectLockAgainstBundle(snapshot_.remoteBundle,
                                        request,
                                        validationMessage);
    if (validation != protocol::ResponseStatus::Ok) {
        if (validation == protocol::ResponseStatus::Conflict ||
            validation == protocol::ResponseStatus::NotFound)
            ++snapshot_.staleOfferFailures;
        result.status = validation;
        result.message = std::move(validationMessage);
        return result;
    }

    protocol::PacketEnvelope packet = makePacket(protocol::MessageKind::Request);
    packet.priority = protocol::PacketPriority::Normal;
    packet.flags = protocol::PacketFlagResponseRequired;
    packet.messageId = readRequests_.nextMessageId();
    packet.correlationId = packet.messageId;
    packet.timeoutMs = timeoutMs == 0 ? 1000 : timeoutMs;
    packet.monotonicTimestampUsec = nowUsec == 0 ? monotonicNowUsec() : nowUsec;
    packet.payload = encodeFdclLockObject(request);

    const network::TrackResult tracked = readRequests_.track(
        packet,
        [this](const protocol::PacketEnvelope& response) {
            handleTrackedObjectLockResponse(response);
        });
    if (!tracked.tracked) {
        ++snapshot_.sendFailures;
        result.status = tracked.status;
        result.message = tracked.message;
        return result;
    }

    result.messageId = packet.messageId;
    if (sendPacket(packet)) {
        ++snapshot_.objectLockRequestsSent;
        snapshot_.pendingReads = readRequests_.pendingCount();
        result.dispatched = true;
        result.status = protocol::ResponseStatus::Ok;
        return result;
    }

    protocol::PacketEnvelope failed = packet;
    failed.messageKind = protocol::MessageKind::Error;
    failed.responseStatus = protocol::ResponseStatus::ChannelUnavailable;
    failed.responseTo = packet.messageId;
    readRequests_.complete(failed);
    result.status = protocol::ResponseStatus::ChannelUnavailable;
    result.message = "clipboard object lock request channel is unavailable";
    return result;
}

ClipboardRemoteReadDispatchResult
ClipboardModuleBase::requestRemoteObjectUnlockTracked(
    const FdclObjectLock& request,
    std::uint32_t timeoutMs,
    std::uint64_t nowUsec)
{
    ClipboardRemoteReadDispatchResult result;
    if (state_ != module::ModuleState::Running)
        return result;
    if (request.lockId == 0) {
        result.status = protocol::ResponseStatus::InvalidArgument;
        result.message = "clipboard object unlock lock id is invalid";
        return result;
    }
    if (!dependencies_.policy.allowReceive ||
        !dependencies_.policy.allowFileContents) {
        ++snapshot_.policyDenials;
        result.status = protocol::ResponseStatus::DeniedByPolicy;
        result.message = "clipboard file object unlock denied by policy";
        return result;
    }

    protocol::PacketEnvelope packet = makePacket(protocol::MessageKind::Request);
    packet.priority = protocol::PacketPriority::Normal;
    packet.flags = protocol::PacketFlagResponseRequired;
    packet.messageId = readRequests_.nextMessageId();
    packet.correlationId = packet.messageId;
    packet.timeoutMs = timeoutMs == 0 ? 1000 : timeoutMs;
    packet.monotonicTimestampUsec = nowUsec == 0 ? monotonicNowUsec() : nowUsec;
    packet.payload = encodeFdclUnlockObject(request);

    const network::TrackResult tracked = readRequests_.track(
        packet,
        [this](const protocol::PacketEnvelope& response) {
            handleTrackedObjectUnlockResponse(response);
        });
    if (!tracked.tracked) {
        ++snapshot_.sendFailures;
        result.status = tracked.status;
        result.message = tracked.message;
        return result;
    }

    result.messageId = packet.messageId;
    if (sendPacket(packet)) {
        ++snapshot_.objectUnlockRequestsSent;
        snapshot_.pendingReads = readRequests_.pendingCount();
        result.dispatched = true;
        result.status = protocol::ResponseStatus::Ok;
        return result;
    }

    protocol::PacketEnvelope failed = packet;
    failed.messageKind = protocol::MessageKind::Error;
    failed.responseStatus = protocol::ResponseStatus::ChannelUnavailable;
    failed.responseTo = packet.messageId;
    readRequests_.complete(failed);
    result.status = protocol::ResponseStatus::ChannelUnavailable;
    result.message = "clipboard object unlock request channel is unavailable";
    return result;
}

bool ClipboardModuleBase::sendObjectLockResponse(
    const protocol::PacketEnvelope& request,
    FdclOperation operation,
    const FdclObjectLock& lockRequest,
    const TransferObjectLockResult& lockResult)
{
    if (!lockResult.ok())
        return sendErrorResponse(request, lockResult.status, lockResult.message);

    FdclObjectLock responsePayload = lockRequest;
    responsePayload.lockId = lockResult.lockId;
    responsePayload.leaseUsec = lockResult.leaseUsec;

    protocol::PacketEnvelope response = makePacket(protocol::MessageKind::Response);
    response.priority = protocol::PacketPriority::Normal;
    response.messageId = nextResponseMessageId_++;
    response.correlationId =
        request.correlationId != 0 ? request.correlationId : request.messageId;
    response.responseTo = request.messageId;
    response.timeoutMs = request.timeoutMs;
    response.responseStatus = protocol::ResponseStatus::Ok;
    response.payload = operation == FdclOperation::UnlockObject
                           ? encodeFdclUnlockObject(responsePayload)
                           : encodeFdclLockObject(responsePayload);

    if (!sendPacket(response))
        return false;

    if (operation == FdclOperation::UnlockObject)
        ++snapshot_.objectUnlockResponsesSent;
    else
        ++snapshot_.objectLockResponsesSent;
    return true;
}

void ClipboardModuleBase::handleObjectLockRequest(
    const protocol::PacketEnvelope& packet,
    const FdclDecodeResult& decoded)
{
    ++snapshot_.objectLockRequestsReceived;
    if (!dependencies_.policy.allowSendContent ||
        !dependencies_.policy.allowFileContents) {
        ++snapshot_.policyDenials;
        sendErrorResponse(packet,
                          protocol::ResponseStatus::DeniedByPolicy,
                          "clipboard object lock denied by policy");
        return;
    }

    const TransferObjectLockResult result =
        lockLocalObject(decoded.objectLock);
    sendObjectLockResponse(packet,
                           FdclOperation::LockObject,
                           decoded.objectLock,
                           result);
}

void ClipboardModuleBase::handleObjectUnlockRequest(
    const protocol::PacketEnvelope& packet,
    const FdclDecodeResult& decoded)
{
    ++snapshot_.objectUnlockRequestsReceived;
    if (!dependencies_.policy.allowSendContent ||
        !dependencies_.policy.allowFileContents) {
        ++snapshot_.policyDenials;
        sendErrorResponse(packet,
                          protocol::ResponseStatus::DeniedByPolicy,
                          "clipboard object unlock denied by policy");
        return;
    }

    const TransferObjectLockResult result =
        unlockLocalObject(decoded.objectLock);
    sendObjectLockResponse(packet,
                           FdclOperation::UnlockObject,
                           decoded.objectLock,
                           result);
}

void ClipboardModuleBase::handleTrackedObjectLockResponse(
    const protocol::PacketEnvelope& packet)
{
    snapshot_.lastObjectLockResponseTo = packet.responseTo;
    TransferObjectLockResult result =
        trackedObjectLockResult(packet, FdclOperation::LockObject, snapshot_);
    if (result.ok())
        ++snapshot_.objectLockResponsesReceived;
    snapshot_.lastObjectLockResult = result;
    snapshot_.pendingReads = readRequests_.pendingCount();
}

void ClipboardModuleBase::handleTrackedObjectUnlockResponse(
    const protocol::PacketEnvelope& packet)
{
    snapshot_.lastObjectUnlockResponseTo = packet.responseTo;
    TransferObjectLockResult result =
        trackedObjectLockResult(packet, FdclOperation::UnlockObject, snapshot_);
    if (result.ok())
        ++snapshot_.objectUnlockResponsesReceived;
    snapshot_.lastObjectUnlockResult = result;
    snapshot_.pendingReads = readRequests_.pendingCount();
}

TransferObjectLockResult ClipboardModuleBase::lockLocalObject(
    const FdclObjectLock& request)
{
    if (dependencies_.sourceRegistry == nullptr) {
        TransferObjectLockResult result;
        result.status = protocol::ResponseStatus::NotFound;
        result.message = "clipboard source registry is unavailable";
        return result;
    }

    TransferObjectLockResult result =
        dependencies_.sourceRegistry->lockObject(
            makeTransferObjectLockRequest(request));
    if (result.status == protocol::ResponseStatus::Conflict ||
        result.status == protocol::ResponseStatus::NotFound)
        ++snapshot_.staleOfferFailures;
    return result;
}

TransferObjectLockResult ClipboardModuleBase::unlockLocalObject(
    const FdclObjectLock& request)
{
    if (dependencies_.sourceRegistry == nullptr) {
        TransferObjectLockResult result;
        result.status = protocol::ResponseStatus::NotFound;
        result.message = "clipboard source registry is unavailable";
        return result;
    }

    TransferObjectLockResult result =
        dependencies_.sourceRegistry->unlockObject(
            makeTransferObjectLockRequest(request));
    if (result.status == protocol::ResponseStatus::Conflict ||
        result.status == protocol::ResponseStatus::NotFound)
        ++snapshot_.staleOfferFailures;
    return result;
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
