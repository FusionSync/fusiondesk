#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

#include "windows_clipboard_descriptors.h"
#include "windows_clipboard_ole_data_object.h"

#include <algorithm>
#include <memory>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#endif

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

bool validDragAction(TransferAction action)
{
    return action == TransferAction::None ||
           action == TransferAction::Copy ||
           action == TransferAction::Move ||
           action == TransferAction::Link;
}

#if defined(_WIN32)
DWORD dropEffectsFromTransferActions(TransferActionSet actions)
{
    DWORD effects = 0;
    if ((actions & transfer_action::Copy) != 0)
        effects |= DROPEFFECT_COPY;
    if ((actions & transfer_action::Move) != 0)
        effects |= DROPEFFECT_MOVE;
    if ((actions & transfer_action::Link) != 0)
        effects |= DROPEFFECT_LINK;
    return effects;
}

TransferAction transferActionFromDropEffect(DWORD effect)
{
    if ((effect & DROPEFFECT_MOVE) != 0)
        return TransferAction::Move;
    if ((effect & DROPEFFECT_LINK) != 0)
        return TransferAction::Link;
    if ((effect & DROPEFFECT_COPY) != 0)
        return TransferAction::Copy;
    return TransferAction::None;
}
#endif

} // namespace

protocol::ResponseStatus WindowsClipboardEndpoint::dragStart(
    const DragSessionStart& start)
{
    if (start.dragSessionId == 0 ||
        start.bundleId == 0 ||
        start.offerId == 0 ||
        start.ownerEpoch == 0 ||
        !validDragAction(start.preferredAction)) {
        return protocol::ResponseStatus::InvalidArgument;
    }
    if (publishedBundle_.bundleId != start.bundleId ||
        publishedBundle_.offerId != start.offerId ||
        publishedBundle_.ownerEpoch != start.ownerEpoch) {
        diagnostics_.lastMessage = "windows drag start offer is stale";
        return protocol::ResponseStatus::Conflict;
    }
    if (activeDragSessionId_ != 0)
        return protocol::ResponseStatus::Conflict;

    const DragCoordinateMapResult mapped = mapDragPoint(start.start);
    if (!mapped.ok()) {
        diagnostics_.lastMessage = mapped.message;
        return mapped.status;
    }

    activeDragSessionId_ = start.dragSessionId;
    diagnostics_.lastDragSessionId = start.dragSessionId;
    diagnostics_.activeDragSessionId = activeDragSessionId_;
    diagnostics_.lastDragX = mapped.point.x;
    diagnostics_.lastDragY = mapped.point.y;
    ++diagnostics_.dragStarts;
    if (options_.dryRun && !options_.nativeDragPreflightOnly)
        return protocol::ResponseStatus::Ok;

    if (!options_.enableNativeDragLoop) {
        activeDragSessionId_ = 0;
        diagnostics_.activeDragSessionId = 0;
        diagnostics_.lastMessage =
            "windows native drag loop is disabled until display surface integration";
        return protocol::ResponseStatus::Unsupported;
    }

    const protocol::ResponseStatus status = nativeStartDrag(start);
    activeDragSessionId_ = 0;
    diagnostics_.activeDragSessionId = 0;
    return status;
}

protocol::ResponseStatus WindowsClipboardEndpoint::dragMove(
    DragSessionId dragSessionId,
    const DragSurfaceCoordinate& point,
    TransferAction proposedAction)
{
    if (dragSessionId == 0 || !validDragAction(proposedAction))
        return protocol::ResponseStatus::InvalidArgument;
    if (activeDragSessionId_ != dragSessionId)
        return protocol::ResponseStatus::Conflict;

    const DragCoordinateMapResult mapped = mapDragPoint(point);
    if (!mapped.ok()) {
        diagnostics_.lastMessage = mapped.message;
        return mapped.status;
    }

    diagnostics_.lastDragX = mapped.point.x;
    diagnostics_.lastDragY = mapped.point.y;
    ++diagnostics_.dragMoves;
    return options_.dryRun ? protocol::ResponseStatus::Ok
                           : protocol::ResponseStatus::Unsupported;
}

protocol::ResponseStatus WindowsClipboardEndpoint::dragDrop(
    DragSessionId dragSessionId,
    const DragSurfaceCoordinate& point,
    TransferAction proposedAction)
{
    if (dragSessionId == 0 || !validDragAction(proposedAction))
        return protocol::ResponseStatus::InvalidArgument;
    if (activeDragSessionId_ != dragSessionId)
        return protocol::ResponseStatus::Conflict;

    const DragCoordinateMapResult mapped = mapDragPoint(point);
    if (!mapped.ok()) {
        diagnostics_.lastMessage = mapped.message;
        return mapped.status;
    }

    diagnostics_.lastDragSessionId = dragSessionId;
    diagnostics_.lastDragX = mapped.point.x;
    diagnostics_.lastDragY = mapped.point.y;
    ++diagnostics_.dragDrops;
    if (options_.dryRun) {
        activeDragSessionId_ = 0;
        diagnostics_.activeDragSessionId = 0;
        return protocol::ResponseStatus::Ok;
    }
    return protocol::ResponseStatus::Unsupported;
}

protocol::ResponseStatus WindowsClipboardEndpoint::dragCancel(
    DragSessionId dragSessionId,
    DragCancelReason)
{
    if (dragSessionId == 0)
        return protocol::ResponseStatus::InvalidArgument;
    if (activeDragSessionId_ != dragSessionId) {
        return protocol::ResponseStatus::Conflict;
    }

    diagnostics_.lastDragSessionId = dragSessionId;
    ++diagnostics_.dragCancels;
    activeDragSessionId_ = 0;
    diagnostics_.activeDragSessionId = 0;
    return protocol::ResponseStatus::Ok;
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativeStartDrag(
    const DragSessionStart& start)
{
#if defined(_WIN32)
    ++diagnostics_.nativeDragLoops;
    diagnostics_.lastDragAction = TransferAction::None;

    const DWORD allowedEffects =
        dropEffectsFromTransferActions(start.allowedActions);
    if (allowedEffects == 0) {
        diagnostics_.lastMessage = "windows native drag has no allowed action";
        return protocol::ResponseStatus::InvalidArgument;
    }
    if (!hasFileListFormat(publishedBundle_)) {
        diagnostics_.lastMessage =
            "windows native drag currently supports file-list bundles only";
        return protocol::ResponseStatus::Unsupported;
    }
    if (remoteFileReader_ == nullptr) {
        diagnostics_.lastMessage =
            "windows native drag requires remote file reader";
        return protocol::ResponseStatus::InvalidArgument;
    }

    const TransferReadResult fileListResult =
        readBestFileList(publishedBundle_);
    if (!fileListResult.ok()) {
        ++diagnostics_.readFailures;
        diagnostics_.lastMessage = fileListResult.message;
        return fileListResult.status;
    }

    const TransferFileListDecodeResult decoded =
        decodeTransferFileList(fileListResult.bytes,
                               options_.maxFileCount,
                               255);
    if (!decoded.ok) {
        diagnostics_.lastMessage = decoded.message;
        return decoded.status;
    }
    const protocol::ByteBuffer groupDescriptor =
        windowsFileGroupDescriptorFromTransferFileList(decoded.fileList);
    if (groupDescriptor.empty()) {
        diagnostics_.lastMessage =
            "windows native drag file group descriptor is empty";
        return protocol::ResponseStatus::InvalidArgument;
    }

    TransferSourceId sourceId = 0;
    for (const std::shared_ptr<TransferSource>& source : publishedBundle_.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(),
                        formats.end(),
                        windowsFileListDescriptorMatches)) {
            sourceId = source->id();
            break;
        }
    }
    if (sourceId == 0) {
        diagnostics_.lastMessage = "windows native drag file source is missing";
        return protocol::ResponseStatus::NotFound;
    }

    const HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole) && ole != RPC_E_CHANGED_MODE) {
        recordNativeFailure(static_cast<std::uint32_t>(ole),
                            "OleInitialize failed for native drag");
        return protocol::ResponseStatus::Failed;
    }

    auto* dataObject = createRemoteFileDataObject(
        publishedBundle_,
        decoded.fileList,
        groupDescriptor,
        sourceId,
        options_.maxFileRangeBytes,
        options_.delayedReadTimeoutMs,
        remoteFileReader_,
        remoteObjectLocker_);
    auto* dropSource = createRemoteFileDropSource();

    if (dataObject == nullptr || dropSource == nullptr) {
        if (dropSource != nullptr)
            dropSource->Release();
        if (dataObject != nullptr)
            dataObject->Release();
        diagnostics_.lastMessage =
            "windows native drag data object creation failed";
        return protocol::ResponseStatus::Failed;
    }

    if (options_.nativeDragPreflightOnly) {
        FORMATETC descriptorFormat = {};
        descriptorFormat.cfFormat =
            static_cast<CLIPFORMAT>(windowsFileGroupDescriptorFormatToken());
        descriptorFormat.dwAspect = DVASPECT_CONTENT;
        descriptorFormat.lindex = -1;
        descriptorFormat.tymed = TYMED_HGLOBAL;

        STGMEDIUM descriptorMedium = {};
        const HRESULT descriptorHr =
            dataObject->GetData(&descriptorFormat, &descriptorMedium);
        if (FAILED(descriptorHr)) {
            dropSource->Release();
            dataObject->Release();
            recordNativeFailure(static_cast<std::uint32_t>(descriptorHr),
                                "native drag preflight FileGroupDescriptor failed");
            return protocol::ResponseStatus::Failed;
        }
        ReleaseStgMedium(&descriptorMedium);

        FORMATETC contentsFormat = {};
        contentsFormat.cfFormat =
            static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"FileContents"));
        contentsFormat.dwAspect = DVASPECT_CONTENT;
        contentsFormat.lindex = 0;
        contentsFormat.tymed = TYMED_ISTREAM;
        const HRESULT contentsHr = dataObject->QueryGetData(&contentsFormat);
        if (FAILED(contentsHr)) {
            dropSource->Release();
            dataObject->Release();
            recordNativeFailure(static_cast<std::uint32_t>(contentsHr),
                                "native drag preflight FileContents failed");
            return protocol::ResponseStatus::Failed;
        }

        if (!decoded.fileList.files.empty() &&
            decoded.fileList.files.front().sizeBytes > 0) {
            STGMEDIUM contentsMedium = {};
            const HRESULT getContentsHr =
                dataObject->GetData(&contentsFormat, &contentsMedium);
            if (FAILED(getContentsHr) ||
                contentsMedium.tymed != TYMED_ISTREAM ||
                contentsMedium.pstm == nullptr) {
                if (contentsMedium.pUnkForRelease != nullptr ||
                    contentsMedium.hGlobal != nullptr ||
                    contentsMedium.pstm != nullptr)
                    ReleaseStgMedium(&contentsMedium);
                dropSource->Release();
                dataObject->Release();
                recordNativeFailure(
                    static_cast<std::uint32_t>(FAILED(getContentsHr)
                                                   ? getContentsHr
                                                   : E_POINTER),
                    "native drag preflight FileContents stream failed");
                return protocol::ResponseStatus::Failed;
            }

            char buffer[16] = {};
            ULONG readBytes = 0;
            const ULONG requestedBytes = static_cast<ULONG>(
                std::min<std::uint64_t>(sizeof(buffer),
                                        decoded.fileList.files.front().sizeBytes));
            const HRESULT readHr =
                contentsMedium.pstm->Read(buffer, requestedBytes, &readBytes);
            ReleaseStgMedium(&contentsMedium);
            if (FAILED(readHr) || readBytes == 0) {
                dropSource->Release();
                dataObject->Release();
                recordNativeFailure(static_cast<std::uint32_t>(
                                        FAILED(readHr) ? readHr : E_FAIL),
                                    "native drag preflight FileContents read failed");
                return protocol::ResponseStatus::Failed;
            }
            ++diagnostics_.nativeDragPreflightReads;
            diagnostics_.nativeDragPreflightBytes += readBytes;
        }

        dropSource->Release();
        dataObject->Release();
        ++diagnostics_.nativeDragPreflights;
        diagnostics_.lastMessage = "windows native drag preflight completed";
        return protocol::ResponseStatus::Ok;
    }

    DWORD effect = DROPEFFECT_NONE;
    const HRESULT hr =
        DoDragDrop(dataObject, dropSource, allowedEffects, &effect);
    dropSource->Release();
    dataObject->Release();

    diagnostics_.lastDragAction = transferActionFromDropEffect(effect);
    if (hr == DRAGDROP_S_DROP) {
        ++diagnostics_.nativeDragDrops;
        diagnostics_.lastMessage = "windows native drag completed";
        return protocol::ResponseStatus::Ok;
    }
    if (hr == DRAGDROP_S_CANCEL) {
        ++diagnostics_.nativeDragCancels;
        diagnostics_.lastMessage = "windows native drag cancelled";
        return protocol::ResponseStatus::Cancelled;
    }

    recordNativeFailure(static_cast<std::uint32_t>(hr),
                        "DoDragDrop failed for native drag");
    return protocol::ResponseStatus::Failed;
#else
    (void)start;
    diagnostics_.lastMessage =
        "windows native drag is unsupported on this platform";
    return protocol::ResponseStatus::Unsupported;
#endif
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk
