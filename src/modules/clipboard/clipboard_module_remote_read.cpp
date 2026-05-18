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

bool isClipboardLargeDataPacket(const protocol::PacketEnvelope& packet)
{
    const network::ChannelKey largeData = clipboardLargeDataChannelKey();
    return packet.packetType == protocol::PacketType::Clipboard &&
           packet.channelId == largeData.channelId &&
           packet.channelType == largeData.channelType;
}

} // namespace

bool ClipboardModuleBase::requestRemoteFormat(
    const FdclReadFormatRequest& request,
    std::uint32_t timeoutMs,
    std::uint64_t nowUsec)
{
    return requestRemoteFormatTracked(request, timeoutMs, nowUsec).dispatched;
}

ClipboardRemoteReadDispatchResult
ClipboardModuleBase::requestRemoteFormatTracked(
    const FdclReadFormatRequest& request,
    std::uint32_t timeoutMs,
    std::uint64_t nowUsec)
{
    ClipboardRemoteReadDispatchResult result;
    if (state_ != module::ModuleState::Running)
        return result;

    if (!dependencies_.policy.allowReceive) {
        ++snapshot_.policyDenials;
        result.status = protocol::ResponseStatus::DeniedByPolicy;
        result.message = "clipboard receive denied by policy";
        return result;
    }
    if (!policyAllowsReadRequest(dependencies_.policy,
                                 snapshot_.remoteBundle,
                                 request)) {
        ++snapshot_.policyDenials;
        result.status = protocol::ResponseStatus::DeniedByPolicy;
        result.message = "clipboard format denied by policy";
        return result;
    }

    protocol::PacketEnvelope packet = makePacket(protocol::MessageKind::Request);
    packet.priority = protocol::PacketPriority::Normal;
    packet.flags = protocol::PacketFlagResponseRequired;
    packet.messageId = readRequests_.nextMessageId();
    packet.correlationId = packet.messageId;
    packet.timeoutMs = timeoutMs == 0 ? 1000 : timeoutMs;
    packet.monotonicTimestampUsec = nowUsec == 0 ? monotonicNowUsec() : nowUsec;
    packet.payload = encodeFdclReadFormatRequest(request);

    const network::TrackResult tracked = readRequests_.track(
        packet,
        [this](const protocol::PacketEnvelope& response) {
            handleTrackedReadResponse(response);
        });
    if (!tracked.tracked) {
        ++snapshot_.sendFailures;
        result.status = tracked.status;
        result.message = tracked.message;
        return result;
    }

    result.messageId = packet.messageId;
    if (sendPacket(packet)) {
        ++snapshot_.readRequestsSent;
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
    result.message = "clipboard read request channel is unavailable";
    return result;
}

ClipboardRemoteReadDispatchResult
ClipboardModuleBase::requestRemoteFileRangeTracked(
    const FdclFileRangeRequest& request,
    std::uint32_t timeoutMs,
    std::uint64_t nowUsec)
{
    ClipboardRemoteReadDispatchResult result;
    if (state_ != module::ModuleState::Running)
        return result;

    if (!dependencies_.policy.allowReceive) {
        ++snapshot_.policyDenials;
        result.status = protocol::ResponseStatus::DeniedByPolicy;
        result.message = "clipboard file range receive denied by policy";
        return result;
    }
    if (!dependencies_.policy.allowFileContents) {
        ++snapshot_.policyDenials;
        result.status = protocol::ResponseStatus::DeniedByPolicy;
        result.message = "clipboard file contents denied by policy";
        return result;
    }

    std::string validationMessage;
    const protocol::ResponseStatus validation =
        validateFileRangeRequestAgainstBundle(snapshot_.remoteBundle,
                                              request,
                                              validationMessage);
    if (validation != protocol::ResponseStatus::Ok) {
        if (validation == protocol::ResponseStatus::Conflict ||
            validation == protocol::ResponseStatus::NotFound) {
            ++snapshot_.staleOfferFailures;
        }
        result.status = validation;
        result.message = std::move(validationMessage);
        return result;
    }

    FdclFileRangeRequest effectiveRequest = request;
    const std::uint64_t maxRangeBytes =
        effectiveMaxFileRangeBytes(dependencies_.policy);
    if (effectiveRequest.requestedBytes == 0 ||
        effectiveRequest.requestedBytes > maxRangeBytes) {
        effectiveRequest.requestedBytes = maxRangeBytes;
    }

    protocol::PacketEnvelope packet = makePacket(protocol::MessageKind::Request);
    packet.priority = protocol::PacketPriority::Normal;
    packet.flags = protocol::PacketFlagResponseRequired;
    packet.messageId = readRequests_.nextMessageId();
    packet.correlationId = packet.messageId;
    packet.timeoutMs = timeoutMs == 0 ? 1000 : timeoutMs;
    packet.monotonicTimestampUsec = nowUsec == 0 ? monotonicNowUsec() : nowUsec;
    packet.payload = encodeFdclFileRangeRequest(effectiveRequest);

    const network::TrackResult tracked = readRequests_.track(
        packet,
        [this](const protocol::PacketEnvelope& response) {
            handleTrackedFileRangeResponse(response);
        });
    if (!tracked.tracked) {
        ++snapshot_.sendFailures;
        result.status = tracked.status;
        result.message = tracked.message;
        return result;
    }

    result.messageId = packet.messageId;
    if (sendPacket(packet)) {
        ++snapshot_.fileRangeRequestsSent;
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
    result.message = "clipboard file range request channel is unavailable";
    return result;
}

std::size_t ClipboardModuleBase::expirePendingReads(std::uint64_t nowUsec)
{
    const std::size_t expired = readRequests_.expire(nowUsec);
    snapshot_.timeoutFailures += static_cast<int>(expired);
    snapshot_.pendingReads = readRequests_.pendingCount();
    return expired;
}

bool ClipboardModuleBase::sendCancel(const FdclCancel& cancel,
                                     std::uint32_t timeoutMs,
                                     std::uint64_t nowUsec)
{
    if (state_ != module::ModuleState::Running)
        return false;

    if (cancel.correlationId == 0 ||
        cancel.bundleId == 0 ||
        cancel.offerId == 0 ||
        cancel.ownerEpoch == 0) {
        ++snapshot_.decodeFailures;
        return false;
    }

    protocol::PacketEnvelope packet = makePacket(protocol::MessageKind::Cancel);
    packet.priority = protocol::PacketPriority::Normal;
    packet.flags = protocol::PacketFlagNoResponseRequired;
    packet.messageId = nextResponseMessageId_++;
    packet.correlationId = cancel.correlationId;
    packet.responseTo = cancel.correlationId;
    packet.timeoutMs = timeoutMs == 0 ? 1000 : timeoutMs;
    packet.monotonicTimestampUsec = nowUsec == 0 ? monotonicNowUsec() : nowUsec;
    packet.payload = encodeFdclCancel(cancel);

    if (!sendPacket(packet))
        return false;

    ++snapshot_.cancelsSent;
    return true;
}

void ClipboardModuleBase::handleReadRequest(
    const protocol::PacketEnvelope& packet,
    const FdclDecodeResult& decoded)
{
    ++snapshot_.readRequestsReceived;
    if (!dependencies_.policy.allowSendContent) {
        ++snapshot_.policyDenials;
        sendErrorResponse(packet,
                          protocol::ResponseStatus::DeniedByPolicy,
                          "clipboard send denied by policy");
        return;
    }
    if (!policyAllowsReadRequest(dependencies_.policy,
                                 snapshot_.localBundle,
                                 decoded.readRequest)) {
        ++snapshot_.policyDenials;
        sendErrorResponse(packet,
                          protocol::ResponseStatus::DeniedByPolicy,
                          "clipboard format denied by policy");
        return;
    }

    TransferReadResult result = readLocal(decoded.readRequest);
    sendReadResponse(packet, decoded.readRequest, result);
}

void ClipboardModuleBase::handleFileRangeRequest(
    const protocol::PacketEnvelope& packet,
    const FdclDecodeResult& decoded)
{
    ++snapshot_.fileRangeRequestsReceived;
    if (!dependencies_.policy.allowSendContent) {
        ++snapshot_.policyDenials;
        sendErrorResponse(packet,
                          protocol::ResponseStatus::DeniedByPolicy,
                          "clipboard file range send denied by policy");
        return;
    }
    if (!dependencies_.policy.allowFileContents) {
        ++snapshot_.policyDenials;
        sendErrorResponse(packet,
                          protocol::ResponseStatus::DeniedByPolicy,
                          "clipboard file contents denied by policy");
        return;
    }

    TransferFileRangeResult result = readLocalFileRange(decoded.fileRangeRequest);
    sendFileRangeResponse(packet, decoded.fileRangeRequest, result);
}

void ClipboardModuleBase::handleReadResponse(
    const protocol::PacketEnvelope& packet)
{
    if (!readRequests_.complete(packet))
        ++snapshot_.decodeFailures;
    snapshot_.pendingReads = readRequests_.pendingCount();
}

void ClipboardModuleBase::handleCancel(const FdclDecodeResult& decoded)
{
    ++snapshot_.cancelsReceived;

    FdclErrorDetail detail;
    detail.status = protocol::ResponseStatus::Cancelled;
    detail.message = decoded.cancel.message.empty()
                         ? "clipboard request cancelled"
                         : decoded.cancel.message;

    protocol::PacketEnvelope cancelled = makePacket(protocol::MessageKind::Error);
    cancelled.responseStatus = protocol::ResponseStatus::Cancelled;
    cancelled.responseTo = decoded.cancel.correlationId;
    cancelled.correlationId = decoded.cancel.correlationId;
    cancelled.payload = encodeFdclErrorDetail(detail);

    if (!readRequests_.complete(cancelled))
        ++snapshot_.cancelMisses;
    snapshot_.pendingReads = readRequests_.pendingCount();
}

void ClipboardModuleBase::handleTrackedReadResponse(
    const protocol::PacketEnvelope& packet)
{
    TransferReadResult result;
    result.status = packet.responseStatus;
    snapshot_.lastReadResponseTo = packet.responseTo;

    if (packet.messageKind == protocol::MessageKind::Response &&
        packet.responseStatus == protocol::ResponseStatus::Ok) {
        const FdclDecodeResult decoded = decodeFdclPayload(packet.payload);
        if (!decoded.ok ||
            decoded.operation != FdclOperation::ReadFormatResponse) {
            ++snapshot_.decodeFailures;
            result.status = protocol::ResponseStatus::ProtocolError;
            result.message = decoded.error.empty()
                                 ? "clipboard read response invalid"
                                 : decoded.error;
        } else {
            result.status = protocol::ResponseStatus::Ok;
            result.canonicalFormat = decoded.readResponse.canonicalFormat;
            result.encoding = decoded.readResponse.encoding;
            result.bytes = decoded.readResponse.bytes;
            ++snapshot_.inlineResponsesReceived;
            snapshot_.inlineBytesReceived += result.bytes.size();
        }
    } else if (packet.messageKind == protocol::MessageKind::Error) {
        const FdclDecodeResult decoded = decodeFdclPayload(packet.payload);
        if (decoded.ok && decoded.operation == FdclOperation::ErrorDetail) {
            result.status = decoded.errorDetail.status;
            result.message = decoded.errorDetail.message;
        } else if (!decoded.error.empty()) {
            result.message = decoded.error;
        }
        if (result.status == protocol::ResponseStatus::Timeout)
            ++snapshot_.timeoutFailures;
        if (result.status == protocol::ResponseStatus::TooLarge)
            ++snapshot_.tooLargeFailures;
        if (result.status == protocol::ResponseStatus::Conflict ||
            result.status == protocol::ResponseStatus::NotFound)
            ++snapshot_.staleOfferFailures;
    } else {
        result.status = protocol::ResponseStatus::ProtocolError;
        result.message = "clipboard read completed with invalid message kind";
        ++snapshot_.decodeFailures;
    }

    const protocol::ResponseStatus beforePolicy = result.status;
    enforceReadResultPolicy(dependencies_.policy, result);
    if (beforePolicy != result.status) {
        if (result.status == protocol::ResponseStatus::DeniedByPolicy)
            ++snapshot_.policyDenials;
        else if (result.status == protocol::ResponseStatus::TooLarge)
            ++snapshot_.tooLargeFailures;
        else if (result.status == protocol::ResponseStatus::ProtocolError)
            ++snapshot_.decodeFailures;
    }

    snapshot_.lastReadResult = result;
    snapshot_.pendingReads = readRequests_.pendingCount();
}

void ClipboardModuleBase::handleTrackedFileRangeResponse(
    const protocol::PacketEnvelope& packet)
{
    TransferFileRangeResult result;
    result.status = packet.responseStatus;
    snapshot_.lastFileRangeResponseTo = packet.responseTo;

    if (packet.messageKind == protocol::MessageKind::Response &&
        packet.responseStatus == protocol::ResponseStatus::Ok) {
        const FdclDecodeResult decoded = decodeFdclPayload(packet.payload);
        if (!decoded.ok ||
            decoded.operation != FdclOperation::FileRangeResponse) {
            ++snapshot_.decodeFailures;
            result.status = protocol::ResponseStatus::ProtocolError;
            result.message = decoded.error.empty()
                                 ? "clipboard file range response invalid"
                                 : decoded.error;
        } else {
            result.status = protocol::ResponseStatus::Ok;
            result.bytes = decoded.fileRangeResponse.bytes;
            result.endOfFile = decoded.fileRangeResponse.endOfFile;
            ++snapshot_.fileRangeResponsesReceived;
            snapshot_.fileRangeBytesReceived += result.bytes.size();
        }
    } else if (packet.messageKind == protocol::MessageKind::Error) {
        const FdclDecodeResult decoded = decodeFdclPayload(packet.payload);
        if (decoded.ok && decoded.operation == FdclOperation::ErrorDetail) {
            result.status = decoded.errorDetail.status;
            result.message = decoded.errorDetail.message;
        } else if (!decoded.error.empty()) {
            result.message = decoded.error;
        }
        if (result.status == protocol::ResponseStatus::Timeout)
            ++snapshot_.timeoutFailures;
        if (result.status == protocol::ResponseStatus::TooLarge)
            ++snapshot_.tooLargeFailures;
        if (result.status == protocol::ResponseStatus::Conflict ||
            result.status == protocol::ResponseStatus::NotFound)
            ++snapshot_.staleOfferFailures;
    } else {
        result.status = protocol::ResponseStatus::ProtocolError;
        result.message =
            "clipboard file range completed with invalid message kind";
        ++snapshot_.decodeFailures;
    }

    if (result.ok() &&
        result.bytes.size() > effectiveMaxFileRangeBytes(dependencies_.policy)) {
        result.status = protocol::ResponseStatus::TooLarge;
        result.bytes.clear();
        result.message = "clipboard file range exceeds policy max bytes";
        ++snapshot_.tooLargeFailures;
    }

    if (isClipboardLargeDataPacket(packet))
        sendLargeDataAck(packet);

    snapshot_.lastFileRangeResult = result;
    snapshot_.pendingReads = readRequests_.pendingCount();
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
