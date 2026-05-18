#include "windows_clipboard_ole_data_object.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

#if defined(_WIN32)

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

HRESULT responseStatusToHresult(protocol::ResponseStatus status)
{
    switch (status) {
    case protocol::ResponseStatus::Ok:
        return S_OK;
    case protocol::ResponseStatus::NotFound:
        return STG_E_FILENOTFOUND;
    case protocol::ResponseStatus::DeniedByPolicy:
        return E_ACCESSDENIED;
    case protocol::ResponseStatus::TooLarge:
        return STG_E_MEDIUMFULL;
    case protocol::ResponseStatus::Timeout:
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    case protocol::ResponseStatus::Unsupported:
        return DV_E_FORMATETC;
    case protocol::ResponseStatus::InvalidArgument:
        return E_INVALIDARG;
    case protocol::ResponseStatus::ChannelUnavailable:
    case protocol::ResponseStatus::BackPressure:
    case protocol::ResponseStatus::Conflict:
    case protocol::ResponseStatus::Cancelled:
    case protocol::ResponseStatus::ProtocolError:
    case protocol::ResponseStatus::Failed:
        break;
    }
    return E_FAIL;
}

HANDLE allocMoveableBytes(const protocol::ByteBuffer& bytes)
{
    const SIZE_T size = static_cast<SIZE_T>(bytes.size());
    HANDLE handle = GlobalAlloc(GMEM_MOVEABLE, size);
    if (handle == nullptr)
        return nullptr;

    void* locked = GlobalLock(handle);
    if (locked == nullptr) {
        GlobalFree(handle);
        return nullptr;
    }
    if (!bytes.empty())
        std::memcpy(locked, bytes.data(), bytes.size());
    GlobalUnlock(handle);
    return handle;
}

UINT fileGroupDescriptorFormat()
{
    return RegisterClipboardFormatW(L"FileGroupDescriptorW");
}

class RemoteFileContentStream final : public IStream
{
public:
    RemoteFileContentStream(
        TransferFileRangeRequest request,
        std::uint64_t sizeBytes,
        std::uint64_t maxChunkBytes,
        std::uint32_t timeoutMs,
        std::shared_ptr<IClipboardRemoteFileReader> reader,
        std::shared_ptr<IClipboardRemoteObjectLocker> locker)
        : request_(request),
          sizeBytes_(sizeBytes),
          maxChunkBytes_(maxChunkBytes == 0 ? 1024 * 1024 : maxChunkBytes),
          timeoutMs_(timeoutMs == 0 ? 1000 : timeoutMs),
          reader_(std::move(reader)),
          locker_(std::move(locker))
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_ISequentialStream ||
            iid == IID_IStream) {
            *object = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&refCount_));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG value =
            static_cast<ULONG>(InterlockedDecrement(&refCount_));
        if (value == 0)
            delete this;
        return value;
    }

    HRESULT STDMETHODCALLTYPE Read(void* buffer,
                                  ULONG requestedBytes,
                                  ULONG* bytesRead) override
    {
        if (bytesRead != nullptr)
            *bytesRead = 0;
        if (buffer == nullptr)
            return STG_E_INVALIDPOINTER;
        if (requestedBytes == 0)
            return S_OK;
        if (reader_ == nullptr)
            return E_ACCESSDENIED;
        if (offset_ >= sizeBytes_)
            return S_FALSE;

        const HRESULT locked = ensureLocked();
        if (FAILED(locked))
            return locked;

        TransferFileWindowReadOptions options;
        options.chunkBytes = maxChunkBytes_;
        options.timeoutMs = timeoutMs_;
        const TransferFileRangeResult result =
            readRemoteFileRangeWindow(*reader_,
                                      request_,
                                      offset_,
                                      requestedBytes,
                                      options);
        if (!result.ok())
            return responseStatusToHresult(result.status);

        const ULONG totalCopied = static_cast<ULONG>(result.bytes.size());
        if (totalCopied > 0)
            std::memcpy(buffer, result.bytes.data(), totalCopied);
        offset_ += totalCopied;
        if (bytesRead != nullptr)
            *bytesRead = totalCopied;
        return totalCopied == requestedBytes ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override
    {
        return STG_E_ACCESSDENIED;
    }

    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move,
                                  DWORD origin,
                                  ULARGE_INTEGER* newPosition) override
    {
        std::int64_t base = 0;
        if (origin == STREAM_SEEK_SET)
            base = 0;
        else if (origin == STREAM_SEEK_CUR)
            base = static_cast<std::int64_t>(offset_);
        else if (origin == STREAM_SEEK_END)
            base = static_cast<std::int64_t>(sizeBytes_);
        else
            return STG_E_INVALIDFUNCTION;

        const std::int64_t next = base + move.QuadPart;
        if (next < 0)
            return STG_E_INVALIDFUNCTION;
        offset_ = static_cast<std::uint64_t>(next);
        if (offset_ > sizeBytes_)
            offset_ = sizeBytes_;
        if (newPosition != nullptr)
            newPosition->QuadPart = offset_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override
    {
        return STG_E_ACCESSDENIED;
    }

    HRESULT STDMETHODCALLTYPE CopyTo(IStream*,
                                     ULARGE_INTEGER,
                                     ULARGE_INTEGER*,
                                     ULARGE_INTEGER*) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE Commit(DWORD) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Revert() override
    {
        return STG_E_REVERTED;
    }

    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,
                                         ULARGE_INTEGER,
                                         DWORD) override
    {
        return STG_E_INVALIDFUNCTION;
    }

    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,
                                           ULARGE_INTEGER,
                                           DWORD) override
    {
        return STG_E_INVALIDFUNCTION;
    }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override
    {
        if (stat == nullptr)
            return STG_E_INVALIDPOINTER;
        std::memset(stat, 0, sizeof(*stat));
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = sizeBytes_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IStream** stream) override
    {
        if (stream == nullptr)
            return E_POINTER;
        auto* clone = new RemoteFileContentStream(request_,
                                                  sizeBytes_,
                                                  maxChunkBytes_,
                                                  timeoutMs_,
                                                  reader_,
                                                  locker_);
        clone->offset_ = offset_;
        *stream = clone;
        return S_OK;
    }

private:
    ~RemoteFileContentStream()
    {
        unlockIfNeeded();
    }

    TransferObjectLockRequest objectLockRequest() const
    {
        TransferObjectLockRequest request;
        request.bundleId = request_.bundleId;
        request.offerId = request_.offerId;
        request.ownerEpoch = request_.ownerEpoch;
        request.sourceId = request_.sourceId;
        request.objectId = request_.objectId;
        request.fileIndex = request_.fileIndex;
        return request;
    }

    HRESULT ensureLocked()
    {
        if (locker_ == nullptr || lockId_ != 0)
            return S_OK;

        TransferObjectLockRequest request = objectLockRequest();
        const TransferObjectLockResult lock =
            locker_->lockRemoteObject(request, timeoutMs_);
        if (!lock.ok())
            return responseStatusToHresult(lock.status);
        if (lock.lockId == 0)
            return E_FAIL;

        lockId_ = lock.lockId;
        leaseUsec_ = lock.leaseUsec;
        return S_OK;
    }

    void unlockIfNeeded()
    {
        if (locker_ == nullptr || lockId_ == 0)
            return;

        TransferObjectLockRequest request = objectLockRequest();
        request.lockId = lockId_;
        request.leaseUsec = leaseUsec_;
        locker_->unlockRemoteObject(request, timeoutMs_);
        lockId_ = 0;
        leaseUsec_ = 0;
    }

    LONG refCount_ = 1;
    TransferFileRangeRequest request_;
    std::uint64_t sizeBytes_ = 0;
    std::uint64_t maxChunkBytes_ = 0;
    std::uint32_t timeoutMs_ = 0;
    std::uint64_t offset_ = 0;
    std::shared_ptr<IClipboardRemoteFileReader> reader_;
    std::shared_ptr<IClipboardRemoteObjectLocker> locker_;
    TransferObjectLockId lockId_ = 0;
    std::uint64_t leaseUsec_ = 0;
};

class FormatEtcEnumerator final : public IEnumFORMATETC
{
public:
    explicit FormatEtcEnumerator(std::vector<FORMATETC> formats)
        : formats_(std::move(formats))
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IEnumFORMATETC) {
            *object = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&refCount_));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG value =
            static_cast<ULONG>(InterlockedDecrement(&refCount_));
        if (value == 0)
            delete this;
        return value;
    }

    HRESULT STDMETHODCALLTYPE Next(ULONG count,
                                  FORMATETC* output,
                                  ULONG* fetched) override
    {
        if (output == nullptr)
            return E_POINTER;
        ULONG copied = 0;
        while (copied < count && index_ < formats_.size())
            output[copied++] = formats_[index_++];
        if (fetched != nullptr)
            *fetched = copied;
        return copied == count ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override
    {
        index_ = std::min<std::size_t>(formats_.size(), index_ + count);
        return index_ < formats_.size() ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Reset() override
    {
        index_ = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** output) override
    {
        if (output == nullptr)
            return E_POINTER;
        auto* clone = new FormatEtcEnumerator(formats_);
        clone->index_ = index_;
        *output = clone;
        return S_OK;
    }

private:
    ~FormatEtcEnumerator() = default;

    LONG refCount_ = 1;
    std::vector<FORMATETC> formats_;
    std::size_t index_ = 0;
};

class RemoteFileDataObject final : public IDataObject
{
public:
    RemoteFileDataObject(TransferSourceBundle bundle,
                         TransferFileList fileList,
                         protocol::ByteBuffer groupDescriptor,
                         TransferSourceId sourceId,
                         std::uint64_t maxChunkBytes,
                         std::uint32_t timeoutMs,
                         std::shared_ptr<IClipboardRemoteFileReader> reader,
                         std::shared_ptr<IClipboardRemoteObjectLocker> locker)
        : bundle_(std::move(bundle)),
          fileList_(std::move(fileList)),
          groupDescriptor_(std::move(groupDescriptor)),
          sourceId_(sourceId),
          maxChunkBytes_(maxChunkBytes),
          timeoutMs_(timeoutMs),
          reader_(std::move(reader)),
          locker_(std::move(locker))
    {
        fileGroupDescriptorFormat_ =
            static_cast<CLIPFORMAT>(fileGroupDescriptorFormat());
        fileContentsFormat_ =
            static_cast<CLIPFORMAT>(
                RegisterClipboardFormatW(L"FileContents"));
        formats_.push_back(
            FORMATETC{fileGroupDescriptorFormat_,
                      nullptr,
                      DVASPECT_CONTENT,
                      -1,
                      TYMED_HGLOBAL});
        formats_.push_back(
            FORMATETC{fileContentsFormat_,
                      nullptr,
                      DVASPECT_CONTENT,
                      -1,
                      TYMED_ISTREAM});
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDataObject) {
            *object = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&refCount_));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG value =
            static_cast<ULONG>(InterlockedDecrement(&refCount_));
        if (value == 0)
            delete this;
        return value;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format,
                                      STGMEDIUM* medium) override
    {
        if (format == nullptr || medium == nullptr)
            return E_INVALIDARG;
        if (QueryGetData(format) != S_OK)
            return DV_E_FORMATETC;

        std::memset(medium, 0, sizeof(*medium));
        if (format->cfFormat == fileGroupDescriptorFormat_) {
            HANDLE handle = allocMoveableBytes(groupDescriptor_);
            if (handle == nullptr)
                return STG_E_MEDIUMFULL;
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = handle;
            return S_OK;
        }

        if (format->cfFormat == fileContentsFormat_) {
            if (format->lindex < 0 ||
                static_cast<std::size_t>(format->lindex) >=
                    fileList_.files.size()) {
                return DV_E_LINDEX;
            }
            if (reader_ == nullptr)
                return E_ACCESSDENIED;

            const std::size_t index =
                static_cast<std::size_t>(format->lindex);
            TransferFileRangeRequest request;
            request.bundleId = bundle_.bundleId;
            request.offerId = bundle_.offerId;
            request.ownerEpoch = bundle_.ownerEpoch;
            request.sourceId = sourceId_;
            request.objectId = fileList_.files[index].objectId;
            request.fileIndex = static_cast<std::uint32_t>(index);

            auto* stream = new RemoteFileContentStream(
                request,
                fileList_.files[index].sizeBytes,
                maxChunkBytes_,
                timeoutMs_,
                reader_,
                locker_);
            medium->tymed = TYMED_ISTREAM;
            medium->pstm = stream;
            return S_OK;
        }

        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override
    {
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override
    {
        if (format == nullptr)
            return E_INVALIDARG;
        if ((format->dwAspect & DVASPECT_CONTENT) == 0)
            return DV_E_DVASPECT;

        if (format->cfFormat == fileGroupDescriptorFormat_) {
            return (format->tymed & TYMED_HGLOBAL) != 0
                       ? S_OK
                       : DV_E_TYMED;
        }
        if (format->cfFormat == fileContentsFormat_) {
            if ((format->tymed & TYMED_ISTREAM) == 0)
                return DV_E_TYMED;
            if (format->lindex >= 0 &&
                static_cast<std::size_t>(format->lindex) >=
                    fileList_.files.size()) {
                return DV_E_LINDEX;
            }
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,
                                                    FORMATETC* output) override
    {
        if (output != nullptr)
            output->ptd = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,
                                            IEnumFORMATETC** output) override
    {
        if (output == nullptr)
            return E_POINTER;
        if (direction != DATADIR_GET)
            return E_NOTIMPL;
        *output = new FormatEtcEnumerator(formats_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,
                                      DWORD,
                                      IAdviseSink*,
                                      DWORD*) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    ~RemoteFileDataObject() = default;

    LONG refCount_ = 1;
    TransferSourceBundle bundle_;
    TransferFileList fileList_;
    protocol::ByteBuffer groupDescriptor_;
    TransferSourceId sourceId_ = 0;
    std::uint64_t maxChunkBytes_ = 0;
    std::uint32_t timeoutMs_ = 0;
    std::shared_ptr<IClipboardRemoteFileReader> reader_;
    std::shared_ptr<IClipboardRemoteObjectLocker> locker_;
    CLIPFORMAT fileGroupDescriptorFormat_ = 0;
    CLIPFORMAT fileContentsFormat_ = 0;
    std::vector<FORMATETC> formats_;
};

class RemoteFileDropSource final : public IDropSource
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDropSource) {
            *object = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&refCount_));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG value =
            static_cast<ULONG>(InterlockedDecrement(&refCount_));
        if (value == 0)
            delete this;
        return value;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed,
                                                DWORD keyState) override
    {
        if (escapePressed)
            return DRAGDROP_S_CANCEL;
        const bool buttonDown = (keyState & (MK_LBUTTON | MK_RBUTTON)) != 0;
        if (buttonDown) {
            sawButtonDown_ = true;
            return S_OK;
        }
        if (sawButtonDown_)
            return DRAGDROP_S_DROP;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
    {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    ~RemoteFileDropSource() = default;

    LONG refCount_ = 1;
    bool sawButtonDown_ = false;
};

} // namespace

IDataObject* createRemoteFileDataObject(
    TransferSourceBundle bundle,
    TransferFileList fileList,
    protocol::ByteBuffer groupDescriptor,
    TransferSourceId sourceId,
    std::uint64_t maxChunkBytes,
    std::uint32_t timeoutMs,
    std::shared_ptr<IClipboardRemoteFileReader> reader,
    std::shared_ptr<IClipboardRemoteObjectLocker> locker)
{
    return new RemoteFileDataObject(std::move(bundle),
                                    std::move(fileList),
                                    std::move(groupDescriptor),
                                    sourceId,
                                    maxChunkBytes,
                                    timeoutMs,
                                    std::move(reader),
                                    std::move(locker));
}

IDropSource* createRemoteFileDropSource()
{
    return new RemoteFileDropSource();
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif
