#include "fusiondesk/modules/clipboard/clipboard_modules.h"

#include <chrono>

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

bool validTransferAction(TransferAction value)
{
    return value == TransferAction::Copy ||
           value == TransferAction::Move ||
           value == TransferAction::Link ||
           value == TransferAction::None;
}

bool dragStartMatchesBundle(const TransferSourceBundle& bundle,
                            const DragSessionStart& start)
{
    return start.dragSessionId != 0 &&
           bundle.bundleId == start.bundleId &&
           bundle.offerId == start.offerId &&
           bundle.ownerEpoch == start.ownerEpoch;
}

} // namespace

bool ClipboardModuleBase::sendRemoteDragStart(const FdclDragStart& start)
{
    if (state_ != module::ModuleState::Running)
        return false;
    if (!dependencies_.policy.allowAnnounce ||
        !dependencies_.policy.allowDrag) {
        ++snapshot_.policyDenials;
        return false;
    }
    if (!dragStartMatchesBundle(snapshot_.localBundle, start.start) ||
        !validTransferAction(start.start.preferredAction)) {
        ++snapshot_.staleOfferFailures;
        return false;
    }
    if (sendDragEvent(encodeFdclDragStart(start))) {
        snapshot_.activeLocalDragSessionId = start.start.dragSessionId;
        ++snapshot_.dragStartsSent;
        return true;
    }
    return false;
}

bool ClipboardModuleBase::sendRemoteDragMove(const FdclDragMove& move)
{
    if (state_ != module::ModuleState::Running)
        return false;
    if (!dependencies_.policy.allowAnnounce ||
        !dependencies_.policy.allowDrag) {
        ++snapshot_.policyDenials;
        return false;
    }
    if (move.dragSessionId == 0 || !validTransferAction(move.proposedAction))
        return false;
    if (snapshot_.activeLocalDragSessionId != move.dragSessionId) {
        ++snapshot_.staleOfferFailures;
        return false;
    }
    if (sendDragEvent(encodeFdclDragMove(move))) {
        ++snapshot_.dragMovesSent;
        return true;
    }
    return false;
}

bool ClipboardModuleBase::sendRemoteDragDrop(const FdclDragDrop& drop)
{
    if (state_ != module::ModuleState::Running)
        return false;
    if (!dependencies_.policy.allowAnnounce ||
        !dependencies_.policy.allowDrag) {
        ++snapshot_.policyDenials;
        return false;
    }
    if (drop.dragSessionId == 0 || !validTransferAction(drop.proposedAction))
        return false;
    if (snapshot_.activeLocalDragSessionId != drop.dragSessionId) {
        ++snapshot_.staleOfferFailures;
        return false;
    }
    if (sendDragEvent(encodeFdclDragDrop(drop))) {
        snapshot_.activeLocalDragSessionId = 0;
        ++snapshot_.dragDropsSent;
        return true;
    }
    return false;
}

bool ClipboardModuleBase::sendRemoteDragCancel(const FdclDragCancel& cancel)
{
    if (state_ != module::ModuleState::Running)
        return false;
    if (!dependencies_.policy.allowAnnounce ||
        !dependencies_.policy.allowDrag) {
        ++snapshot_.policyDenials;
        return false;
    }
    if (cancel.dragSessionId == 0)
        return false;
    if (snapshot_.activeLocalDragSessionId != cancel.dragSessionId) {
        ++snapshot_.staleOfferFailures;
        return false;
    }
    if (sendDragEvent(encodeFdclDragCancel(cancel))) {
        snapshot_.activeLocalDragSessionId = 0;
        ++snapshot_.dragCancelsSent;
        return true;
    }
    return false;
}

bool ClipboardModuleBase::sendDragEvent(const protocol::ByteBuffer& payload)
{
    protocol::PacketEnvelope packet = makePacket(protocol::MessageKind::Event);
    packet.priority = protocol::PacketPriority::Normal;
    packet.flags = protocol::PacketFlagNoResponseRequired;
    packet.monotonicTimestampUsec = monotonicNowUsec();
    packet.payload = payload;
    return sendPacket(packet);
}

void ClipboardModuleBase::handleDragStart(const FdclDecodeResult& decoded)
{
    ++snapshot_.dragStartsReceived;
    if (!dependencies_.policy.allowReceive ||
        !dependencies_.policy.allowDrag ||
        !dependencies_.policy.allowWriteLocal) {
        ++snapshot_.policyDenials;
        return;
    }
    if (!dragStartMatchesBundle(snapshot_.remoteBundle,
                                decoded.dragStart.start)) {
        ++snapshot_.staleOfferFailures;
        return;
    }
    if (dependencies_.dragSink == nullptr) {
        ++snapshot_.dragNativePublicationFailures;
        return;
    }
    const protocol::ResponseStatus status =
        dependencies_.dragSink->dragStart(decoded.dragStart.start);
    if (status != protocol::ResponseStatus::Ok) {
        ++snapshot_.dragNativePublicationFailures;
        return;
    }
    snapshot_.activeRemoteDragSessionId =
        decoded.dragStart.start.dragSessionId;
}

void ClipboardModuleBase::handleDragMove(const FdclDecodeResult& decoded)
{
    ++snapshot_.dragMovesReceived;
    if (!dependencies_.policy.allowReceive ||
        !dependencies_.policy.allowDrag ||
        !dependencies_.policy.allowWriteLocal) {
        ++snapshot_.policyDenials;
        return;
    }
    if (snapshot_.activeRemoteDragSessionId !=
        decoded.dragMove.dragSessionId) {
        ++snapshot_.staleOfferFailures;
        return;
    }
    if (dependencies_.dragSink == nullptr) {
        ++snapshot_.dragNativePublicationFailures;
        return;
    }
    const protocol::ResponseStatus status =
        dependencies_.dragSink->dragMove(decoded.dragMove.dragSessionId,
                                         decoded.dragMove.point,
                                         decoded.dragMove.proposedAction);
    if (status != protocol::ResponseStatus::Ok)
        ++snapshot_.dragNativePublicationFailures;
}

void ClipboardModuleBase::handleDragDrop(const FdclDecodeResult& decoded)
{
    ++snapshot_.dragDropsReceived;
    if (!dependencies_.policy.allowReceive ||
        !dependencies_.policy.allowDrag ||
        !dependencies_.policy.allowWriteLocal) {
        ++snapshot_.policyDenials;
        return;
    }
    if (snapshot_.activeRemoteDragSessionId !=
        decoded.dragDrop.dragSessionId) {
        ++snapshot_.staleOfferFailures;
        return;
    }
    if (dependencies_.dragSink == nullptr) {
        ++snapshot_.dragNativePublicationFailures;
        return;
    }
    const protocol::ResponseStatus status =
        dependencies_.dragSink->dragDrop(decoded.dragDrop.dragSessionId,
                                         decoded.dragDrop.point,
                                         decoded.dragDrop.proposedAction);
    if (status != protocol::ResponseStatus::Ok) {
        ++snapshot_.dragNativePublicationFailures;
        return;
    }
    snapshot_.activeRemoteDragSessionId = 0;
}

void ClipboardModuleBase::handleDragCancel(const FdclDecodeResult& decoded)
{
    ++snapshot_.dragCancelsReceived;
    if (!dependencies_.policy.allowDrag) {
        ++snapshot_.policyDenials;
        return;
    }
    if (snapshot_.activeRemoteDragSessionId !=
        decoded.dragCancel.dragSessionId) {
        ++snapshot_.staleOfferFailures;
        return;
    }
    if (dependencies_.dragSink == nullptr)
        return;
    const protocol::ResponseStatus status =
        dependencies_.dragSink->dragCancel(decoded.dragCancel.dragSessionId,
                                           decoded.dragCancel.reason);
    if (status != protocol::ResponseStatus::Ok) {
        ++snapshot_.dragNativePublicationFailures;
        return;
    }
    snapshot_.activeRemoteDragSessionId = 0;
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
