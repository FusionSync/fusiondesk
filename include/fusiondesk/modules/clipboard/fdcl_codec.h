#ifndef FUSIONDESK_MODULES_CLIPBOARD_FDCL_CODEC_H
#define FUSIONDESK_MODULES_CLIPBOARD_FDCL_CODEC_H

#include <string>

#include "fusiondesk/modules/clipboard/protocol.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

struct FdclDecodeResult
{
    bool ok = false;
    FdclOperation operation = FdclOperation::Unknown;
    std::string error;
    FdclCapabilities capabilities;
    FdclFormatList formatList;
    FdclReadFormatRequest readRequest;
    FdclReadFormatResponse readResponse;
    FdclErrorDetail errorDetail;
    FdclFileRangeRequest fileRangeRequest;
    FdclFileRangeResponse fileRangeResponse;
    FdclObjectLock objectLock;
    FdclDragStart dragStart;
    FdclDragMove dragMove;
    FdclDragDrop dragDrop;
    FdclDragCancel dragCancel;
    FdclCancel cancel;
};

protocol::ByteBuffer encodeFdclCapabilities(const FdclCapabilities& payload);
protocol::ByteBuffer encodeFdclFormatList(const FdclFormatList& payload);
protocol::ByteBuffer encodeFdclReadFormatRequest(const FdclReadFormatRequest& payload);
protocol::ByteBuffer encodeFdclReadFormatResponse(const FdclReadFormatResponse& payload);
protocol::ByteBuffer encodeFdclErrorDetail(const FdclErrorDetail& payload);
protocol::ByteBuffer encodeFdclFileRangeRequest(const FdclFileRangeRequest& payload);
protocol::ByteBuffer encodeFdclFileRangeResponse(const FdclFileRangeResponse& payload);
protocol::ByteBuffer encodeFdclLockObject(const FdclObjectLock& payload);
protocol::ByteBuffer encodeFdclUnlockObject(const FdclObjectLock& payload);
protocol::ByteBuffer encodeFdclDragStart(const FdclDragStart& payload);
protocol::ByteBuffer encodeFdclDragMove(const FdclDragMove& payload);
protocol::ByteBuffer encodeFdclDragDrop(const FdclDragDrop& payload);
protocol::ByteBuffer encodeFdclDragCancel(const FdclDragCancel& payload);
protocol::ByteBuffer encodeFdclCancel(const FdclCancel& payload);
FdclDecodeResult decodeFdclPayload(const protocol::ByteBuffer& payload);

FdclFormatList makeFormatListFromBundle(const TransferSourceBundle& bundle);
TransferSourceBundle makeRemoteBundleFromFormatList(const FdclFormatList& list);
TransferReadRequest makeTransferReadRequest(const FdclReadFormatRequest& request);

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_CLIPBOARD_FDCL_CODEC_H
