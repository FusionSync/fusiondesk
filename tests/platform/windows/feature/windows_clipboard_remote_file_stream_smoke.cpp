#include "windows_clipboard_ole_data_object.h"

#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;
using namespace fusiondesk::platform::windows::clipboard;

namespace {

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

TransferSourceBundle sampleBundle()
{
    TransferSourceBundle bundle;
    bundle.bundleId = 10;
    bundle.offerId = 20;
    bundle.ownerEpoch = 30;
    bundle.sequence = 40;
    bundle.side = TransferSide::Remote;
    return bundle;
}

TransferFileList sampleFileList()
{
    TransferFileList list;
    TransferFileDescriptor file;
    file.objectId = 9001;
    file.displayName = "report.txt";
    file.relativePath = "report.txt";
    file.sizeBytes = 5;
    list.files.push_back(file);
    return list;
}

protocol::ByteBuffer hglobalBytes(HGLOBAL handle)
{
    const SIZE_T size = GlobalSize(handle);
    const void* locked = GlobalLock(handle);
    assert(locked != nullptr);
    protocol::ByteBuffer output(static_cast<std::size_t>(size));
    if (size > 0)
        std::memcpy(output.data(), locked, static_cast<std::size_t>(size));
    GlobalUnlock(handle);
    return output;
}

class FakeFileReader final : public IClipboardRemoteFileReader
{
public:
    TransferFileRangeResult readRemoteFileRange(
        const TransferFileRangeRequest& request,
        std::uint32_t timeoutMs) override
    {
        ++calls;
        lastRequest = request;
        lastTimeoutMs = timeoutMs;

        TransferFileRangeResult result;
        result.status = status;
        result.bytes = payload;
        result.endOfFile = endOfFile;
        result.message = message;
        return result;
    }

    int calls = 0;
    TransferFileRangeRequest lastRequest;
    std::uint32_t lastTimeoutMs = 0;
    protocol::ResponseStatus status = protocol::ResponseStatus::Ok;
    protocol::ByteBuffer payload = bytes("hello");
    bool endOfFile = true;
    std::string message;
};

class FakeObjectLocker final : public IClipboardRemoteObjectLocker
{
public:
    TransferObjectLockResult lockRemoteObject(
        const TransferObjectLockRequest& request,
        std::uint32_t timeoutMs) override
    {
        ++lockCalls;
        lastLockRequest = request;
        lastLockTimeoutMs = timeoutMs;

        TransferObjectLockResult result;
        result.status = lockStatus;
        result.lockId = lockId;
        result.leaseUsec = leaseUsec;
        return result;
    }

    TransferObjectLockResult unlockRemoteObject(
        const TransferObjectLockRequest& request,
        std::uint32_t timeoutMs) override
    {
        ++unlockCalls;
        lastUnlockRequest = request;
        lastUnlockTimeoutMs = timeoutMs;

        TransferObjectLockResult result;
        result.status = unlockStatus;
        result.lockId = request.lockId;
        result.leaseUsec = request.leaseUsec;
        return result;
    }

    int lockCalls = 0;
    int unlockCalls = 0;
    TransferObjectLockRequest lastLockRequest;
    TransferObjectLockRequest lastUnlockRequest;
    std::uint32_t lastLockTimeoutMs = 0;
    std::uint32_t lastUnlockTimeoutMs = 0;
    protocol::ResponseStatus lockStatus = protocol::ResponseStatus::Ok;
    protocol::ResponseStatus unlockStatus = protocol::ResponseStatus::Ok;
    TransferObjectLockId lockId = 777;
    std::uint64_t leaseUsec = 123456;
};

FORMATETC fileContentsFormat()
{
    FORMATETC format = {};
    format.cfFormat =
        static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"FileContents"));
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = 0;
    format.tymed = TYMED_ISTREAM;
    return format;
}

FORMATETC fileGroupDescriptorFormat()
{
    FORMATETC format = {};
    format.cfFormat =
        static_cast<CLIPFORMAT>(windowsFileGroupDescriptorFormatToken());
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;
    return format;
}

class TestDropTarget final : public IDropTarget
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDropTarget) {
            *object = static_cast<IDropTarget*>(this);
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

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* object,
                                        DWORD,
                                        POINTL,
                                        DWORD* effect) override
    {
        ++dragEnters;
        if (effect != nullptr)
            *effect = DROPEFFECT_NONE;
        if (object == nullptr)
            return E_INVALIDARG;

        FORMATETC descriptor = fileGroupDescriptorFormat();
        FORMATETC contents = fileContentsFormat();
        if (object->QueryGetData(&descriptor) != S_OK ||
            object->QueryGetData(&contents) != S_OK) {
            return DV_E_FORMATETC;
        }
        if (effect != nullptr)
            *effect = DROPEFFECT_COPY;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD,
                                       POINTL,
                                       DWORD* effect) override
    {
        ++dragOvers;
        if (effect != nullptr)
            *effect = DROPEFFECT_COPY;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        ++dragLeaves;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* object,
                                   DWORD,
                                   POINTL,
                                   DWORD* effect) override
    {
        ++drops;
        if (effect != nullptr)
            *effect = DROPEFFECT_NONE;
        if (object == nullptr)
            return E_INVALIDARG;

        FORMATETC descriptorFormat = fileGroupDescriptorFormat();
        STGMEDIUM descriptorMedium = {};
        HRESULT hr = object->GetData(&descriptorFormat, &descriptorMedium);
        if (FAILED(hr))
            return hr;

        const TransferFileListDecodeResult decoded =
            transferFileListFromWindowsFileGroupDescriptor(
                hglobalBytes(descriptorMedium.hGlobal));
        ReleaseStgMedium(&descriptorMedium);
        if (!decoded.ok)
            return E_INVALIDARG;
        droppedFiles = decoded.fileList.files;

        FORMATETC contentsFormat = fileContentsFormat();
        STGMEDIUM contentsMedium = {};
        hr = object->GetData(&contentsFormat, &contentsMedium);
        if (FAILED(hr))
            return hr;
        if (contentsMedium.tymed != TYMED_ISTREAM ||
            contentsMedium.pstm == nullptr) {
            ReleaseStgMedium(&contentsMedium);
            return DV_E_TYMED;
        }

        char buffer[16] = {};
        ULONG readBytes = 0;
        hr = contentsMedium.pstm->Read(buffer, 5, &readBytes);
        if (SUCCEEDED(hr) && readBytes > 0)
            droppedBytes.assign(buffer, buffer + readBytes);
        ReleaseStgMedium(&contentsMedium);
        if (FAILED(hr))
            return hr;

        if (effect != nullptr)
            *effect = DROPEFFECT_COPY;
        return S_OK;
    }

    int dragEnters = 0;
    int dragOvers = 0;
    int dragLeaves = 0;
    int drops = 0;
    std::vector<TransferFileDescriptor> droppedFiles;
    std::string droppedBytes;

private:
    ~TestDropTarget() = default;

    LONG refCount_ = 1;
};

IDataObject* makeDataObject(
    std::shared_ptr<IClipboardRemoteFileReader> reader,
    std::shared_ptr<IClipboardRemoteObjectLocker> locker = {})
{
    const TransferFileList fileList = sampleFileList();
    return createRemoteFileDataObject(sampleBundle(),
                                      fileList,
                                      windowsFileGroupDescriptorFromTransferFileList(
                                          fileList),
                                      55,
                                      64,
                                      88,
                                      std::move(reader),
                                      std::move(locker));
}

void streamReadLocksAndUnlocksRemoteObject()
{
    auto reader = std::make_shared<FakeFileReader>();
    auto locker = std::make_shared<FakeObjectLocker>();
    IDataObject* object = makeDataObject(reader, locker);

    FORMATETC format = fileContentsFormat();
    STGMEDIUM medium = {};
    assert(object->GetData(&format, &medium) == S_OK);
    assert(locker->lockCalls == 0);

    char buffer[8] = {};
    ULONG readBytes = 0;
    assert(medium.pstm->Read(buffer, 5, &readBytes) == S_OK);
    assert(readBytes == 5);
    assert(std::string(buffer, buffer + readBytes) == "hello");
    assert(locker->lockCalls == 1);
    assert(locker->unlockCalls == 0);
    assert(locker->lastLockTimeoutMs == 88);
    assert(locker->lastLockRequest.bundleId == 10);
    assert(locker->lastLockRequest.offerId == 20);
    assert(locker->lastLockRequest.ownerEpoch == 30);
    assert(locker->lastLockRequest.sourceId == 55);
    assert(locker->lastLockRequest.objectId == 9001);
    assert(locker->lastLockRequest.fileIndex == 0);
    assert(reader->calls == 1);
    assert(reader->lastRequest.objectId == 9001);

    ReleaseStgMedium(&medium);
    assert(locker->unlockCalls == 1);
    assert(locker->lastUnlockTimeoutMs == 88);
    assert(locker->lastUnlockRequest.lockId == 777);
    assert(locker->lastUnlockRequest.leaseUsec == 123456);
    object->Release();
}

void dropTargetReadsDescriptorAndContentsLazily()
{
    auto reader = std::make_shared<FakeFileReader>();
    auto locker = std::make_shared<FakeObjectLocker>();
    IDataObject* object = makeDataObject(reader, locker);

    auto* target = new TestDropTarget();
    DWORD effect = DROPEFFECT_COPY;
    POINTL point = {};
    assert(target->DragEnter(object, MK_LBUTTON, point, &effect) == S_OK);
    assert(effect == DROPEFFECT_COPY);
    assert(target->DragOver(MK_LBUTTON, point, &effect) == S_OK);
    assert(effect == DROPEFFECT_COPY);
    assert(target->Drop(object, MK_LBUTTON, point, &effect) == S_OK);
    assert(effect == DROPEFFECT_COPY);

    assert(target->dragEnters == 1);
    assert(target->dragOvers == 1);
    assert(target->drops == 1);
    assert(target->droppedFiles.size() == 1);
    assert(target->droppedFiles.front().displayName == "report.txt");
    assert(target->droppedFiles.front().relativePath == "report.txt");
    assert(target->droppedFiles.front().sizeBytes == 5);
    assert(target->droppedBytes == "hello");
    assert(reader->calls == 1);
    assert(reader->lastRequest.objectId == 9001);
    assert(locker->lockCalls == 1);
    assert(locker->unlockCalls == 1);

    target->Release();
    object->Release();
}

void missingLockerPreservesRangeReads()
{
    auto reader = std::make_shared<FakeFileReader>();
    IDataObject* object = makeDataObject(reader);

    FORMATETC format = fileContentsFormat();
    STGMEDIUM medium = {};
    assert(object->GetData(&format, &medium) == S_OK);

    char buffer[8] = {};
    ULONG readBytes = 0;
    assert(medium.pstm->Read(buffer, 5, &readBytes) == S_OK);
    assert(readBytes == 5);
    assert(reader->calls == 1);

    ReleaseStgMedium(&medium);
    object->Release();
}

void lockFailurePreventsRangeRead()
{
    auto reader = std::make_shared<FakeFileReader>();
    auto locker = std::make_shared<FakeObjectLocker>();
    locker->lockStatus = protocol::ResponseStatus::DeniedByPolicy;

    IDataObject* object = makeDataObject(reader, locker);
    FORMATETC format = fileContentsFormat();
    STGMEDIUM medium = {};
    assert(object->GetData(&format, &medium) == S_OK);

    char buffer[8] = {};
    ULONG readBytes = 42;
    assert(medium.pstm->Read(buffer, 5, &readBytes) == E_ACCESSDENIED);
    assert(readBytes == 0);
    assert(locker->lockCalls == 1);
    assert(locker->unlockCalls == 0);
    assert(reader->calls == 0);

    ReleaseStgMedium(&medium);
    object->Release();
}

} // namespace

int main()
{
    streamReadLocksAndUnlocksRemoteObject();
    dropTargetReadsDescriptorAndContentsLazily();
    missingLockerPreservesRangeReads();
    lockFailurePreventsRangeRead();
    return 0;
}
