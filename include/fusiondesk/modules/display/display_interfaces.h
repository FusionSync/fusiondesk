#ifndef FUSIONDESK_MODULES_DISPLAY_DISPLAY_INTERFACES_H
#define FUSIONDESK_MODULES_DISPLAY_DISPLAY_INTERFACES_H

#include <string>

#include "fusiondesk/modules/display/display_types.h"

namespace fusiondesk {
namespace modules {
namespace display {

class IDisplaySourceCatalog
{
public:
    virtual ~IDisplaySourceCatalog() = default;
    virtual DisplayTopologySnapshot snapshot() const = 0;
};

class IDisplayCapture
{
public:
    virtual ~IDisplayCapture() = default;
    virtual bool open(const DisplayCaptureOpenOptions& options)
    {
        (void)options;
        return true;
    }

    virtual void close()
    {
    }

    virtual CapturedFrame captureNextFrame(bool keyFrame) = 0;

    virtual std::string backendId() const
    {
        return {};
    }

    virtual DisplayCaptureStatus lastStatus() const
    {
        return {};
    }

    virtual int captureErrors() const
    {
        return 0;
    }
};

class IVideoEncoder
{
public:
    virtual ~IVideoEncoder() = default;
    virtual EncodedFrame encode(const CapturedFrame& frame) = 0;

    virtual DisplayCodecRuntimeInfo codecRuntimeInfo() const
    {
        return {};
    }
};

class IVideoDecoder
{
public:
    virtual ~IVideoDecoder() = default;
    virtual DecodedFrame decode(const EncodedFrame& frame) = 0;

    virtual DisplayCodecRuntimeInfo codecRuntimeInfo() const
    {
        return {};
    }
};

class IDisplayRenderer
{
public:
    virtual ~IDisplayRenderer() = default;
    virtual bool attachSurface(const DisplayRenderSurface& surface)
    {
        (void)surface;
        return true;
    }

    virtual void detachSurface()
    {
    }

    virtual bool render(const DecodedFrame& frame) = 0;
};

} // namespace display
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_DISPLAY_DISPLAY_INTERFACES_H
