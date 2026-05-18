#ifndef FUSIONDESK_MODULES_DISPLAY_DISPLAY_MODULES_H
#define FUSIONDESK_MODULES_DISPLAY_DISPLAY_MODULES_H

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

#include "fusiondesk/core/module/module.h"
#include "fusiondesk/core/network/request_tracker.h"
#include "fusiondesk/modules/display/display_control_codec.h"
#include "fusiondesk/modules/display/display_interfaces.h"

namespace fusiondesk {
namespace modules {
namespace display {

struct DisplayAgentSnapshot
{
    module::ModuleState state = module::ModuleState::Created;
    std::uint32_t sourceId = 0;
    std::uint32_t targetWidth = 0;
    std::uint32_t targetHeight = 0;
    DisplayScaleMode scaleMode = DisplayScaleMode::Fit;
    bool includeCursor = true;
    int capturedFrames = 0;
    int encodedFrames = 0;
    int sentFrames = 0;
    std::uint64_t capturedPixelBytes = 0;
    std::uint64_t encodedPayloadBytes = 0;
    std::uint64_t sentPayloadBytes = 0;
    std::uint64_t lastCapturedPixelBytes = 0;
    std::uint64_t lastEncodedPayloadBytes = 0;
    std::uint64_t lastSentPayloadBytes = 0;
    int droppedFrames = 0;
    int captureOpenFailures = 0;
    int captureErrors = 0;
    int encodeFailures = 0;
    int sendFailures = 0;
    int invalidControlRequests = 0;
    int keyframeRequests = 0;
    int responsesSent = 0;
    int captureGeometryOrFormatChanges = 0;
    int reconnectPauses = 0;
    int reconnectResumes = 0;
    std::string captureBackendId;
    DisplayCaptureStatus lastCaptureStatus;
    DisplayCodecRuntimeInfo encoderCodec;
};

struct DisplayClientSnapshot
{
    module::ModuleState state = module::ModuleState::Created;
    int receivedFrames = 0;
    int decodedFrames = 0;
    int renderedFrames = 0;
    std::uint64_t receivedPayloadBytes = 0;
    std::uint64_t decodedPixelBytes = 0;
    std::uint64_t renderedPixelBytes = 0;
    std::uint64_t lastReceivedPayloadBytes = 0;
    std::uint64_t lastDecodedPixelBytes = 0;
    std::uint64_t lastRenderedPixelBytes = 0;
    int droppedFrames = 0;
    int frameGaps = 0;
    int decodeErrors = 0;
    int renderErrors = 0;
    int keyframeResponses = 0;
    int keyframeRequestFailures = 0;
    int keyframeRequestTimeouts = 0;
    int decoderRecoveries = 0;
    int decoderPendingFrames = 0;
    int delayedDecodedFrames = 0;
    std::size_t pendingKeyframeRequests = 0;
    protocol::MessageId lastKeyframeRequestId = 0;
    std::uint64_t lastRenderedFrameId = 0;
    int reconnectRenderedFramesBaseline = 0;
    int reconnectPauses = 0;
    int reconnectResumes = 0;
    DisplayCodecRuntimeInfo decoderCodec;
};

class DisplayAgentModule : public module::IModule, public module::IReconnectAwareModule
{
public:
    DisplayAgentModule(std::shared_ptr<IDisplayCapture> capture,
                       std::shared_ptr<IVideoEncoder> encoder,
                       const DisplayCaptureOpenOptions& captureOptions = {},
                       const DisplayCodecRuntimeInfo& encoderCodec = {});

    const module::ModuleManifest& manifest() const override;
    module::ModuleState state() const override;
    bool attach(const module::ModuleRuntime& runtime) override;
    bool start(const module::ModuleStartOptions& options) override;
    void stop(const module::ModuleStopOptions& options) override;
    void detach() override;
    void pauseForReconnect(const module::ModuleReconnectOptions& options) override;
    void resumeAfterReconnect(const module::ModuleReconnectOptions& options) override;
    void handlePacket(const protocol::PacketEnvelope& packet) override;
    std::string diagnostics() const override;

    bool sendDeltaFrame();
    bool reopenCapture(bool sendFreshKeyFrame);
    bool replaceCapture(std::shared_ptr<IDisplayCapture> capture,
                        bool sendFreshKeyFrame);
    int sentFrames() const;
    int droppedFrames() const;
    int keyframeRequests() const;
    int responsesSent() const;
    DisplayAgentSnapshot snapshot() const;

private:
    bool sendKeyFrame();
    bool sendFrame(bool keyFrame);
    bool frameShapeChanged(const CapturedFrame& frame) const;
    void recordSentFrameShape(const EncodedFrame& frame, std::uint32_t sourceId);
    bool shouldDropFrame(bool keyFrame) const;
    bool sendResponse(const protocol::PacketEnvelope& request,
                      protocol::ResponseStatus status,
                      protocol::MessageKind kind,
                      const protocol::ByteBuffer& payload = {});

private:
    module::ModuleManifest manifest_;
    module::ModuleRuntime runtime_;
    module::ModuleState state_ = module::ModuleState::Created;
    std::shared_ptr<IDisplayCapture> capture_;
    std::shared_ptr<IVideoEncoder> encoder_;
    DisplayCaptureOpenOptions captureOptions_;
    DisplayCodecRuntimeInfo encoderCodec_;
    bool captureOpened_ = false;
    protocol::MessageId nextMessageId_ = 1000;
    int capturedFrames_ = 0;
    int encodedFrames_ = 0;
    int sentFrames_ = 0;
    std::uint64_t capturedPixelBytes_ = 0;
    std::uint64_t encodedPayloadBytes_ = 0;
    std::uint64_t sentPayloadBytes_ = 0;
    std::uint64_t lastCapturedPixelBytes_ = 0;
    std::uint64_t lastEncodedPayloadBytes_ = 0;
    std::uint64_t lastSentPayloadBytes_ = 0;
    int droppedFrames_ = 0;
    int captureOpenFailures_ = 0;
    int encodeFailures_ = 0;
    int sendFailures_ = 0;
    int invalidControlRequests_ = 0;
    int keyframeRequests_ = 0;
    int responsesSent_ = 0;
    int captureGeometryOrFormatChanges_ = 0;
    int reconnectPauses_ = 0;
    int reconnectResumes_ = 0;
    bool lastSentFrameShapeValid_ = false;
    std::uint32_t lastSentSourceId_ = 0;
    std::uint32_t lastSentFrameWidth_ = 0;
    std::uint32_t lastSentFrameHeight_ = 0;
    std::uint32_t lastSentFrameStrideBytes_ = 0;
    DisplayPixelFormat lastSentFramePixelFormat_ = DisplayPixelFormat::Unknown;
};

class DisplayClientModule : public module::IModule, public module::IReconnectAwareModule
{
public:
    DisplayClientModule(std::shared_ptr<IVideoDecoder> decoder,
                        std::shared_ptr<IDisplayRenderer> renderer,
                        const DisplayCodecRuntimeInfo& decoderCodec = {});

    const module::ModuleManifest& manifest() const override;
    module::ModuleState state() const override;
    bool attach(const module::ModuleRuntime& runtime) override;
    bool start(const module::ModuleStartOptions& options) override;
    void stop(const module::ModuleStopOptions& options) override;
    void detach() override;
    void pauseForReconnect(const module::ModuleReconnectOptions& options) override;
    void resumeAfterReconnect(const module::ModuleReconnectOptions& options) override;
    void handlePacket(const protocol::PacketEnvelope& packet) override;
    std::string diagnostics() const override;

    bool requestKeyFrame(DisplayKeyframeReason reason = DisplayKeyframeReason::Manual);
    std::size_t expireKeyframeRequests(std::uint64_t nowUsec);
    int receivedFrames() const;
    int renderedFrames() const;
    int keyframeResponses() const;
    protocol::MessageId lastKeyframeRequestId() const;
    DisplayClientSnapshot snapshot() const;

private:
    bool renderVideo(const protocol::PacketEnvelope& packet);
    void handleKeyframeResponse(const protocol::PacketEnvelope& packet);
    bool requestRecoveryKeyFrame(DisplayKeyframeReason reason);

private:
    module::ModuleManifest manifest_;
    module::ModuleRuntime runtime_;
    module::ModuleState state_ = module::ModuleState::Created;
    std::shared_ptr<IVideoDecoder> decoder_;
    std::shared_ptr<IDisplayRenderer> renderer_;
    DisplayCodecRuntimeInfo decoderCodec_;
    network::RequestTracker keyframeRequestsTracker_;
    protocol::MessageId lastKeyframeRequestId_ = 0;
    std::uint64_t lastRenderedFrameId_ = 0;
    int receivedFrames_ = 0;
    int decodedFrames_ = 0;
    int renderedFrames_ = 0;
    std::uint64_t receivedPayloadBytes_ = 0;
    std::uint64_t decodedPixelBytes_ = 0;
    std::uint64_t renderedPixelBytes_ = 0;
    std::uint64_t lastReceivedPayloadBytes_ = 0;
    std::uint64_t lastDecodedPixelBytes_ = 0;
    std::uint64_t lastRenderedPixelBytes_ = 0;
    int droppedFrames_ = 0;
    int frameGaps_ = 0;
    int decodeErrors_ = 0;
    int renderErrors_ = 0;
    int keyframeResponses_ = 0;
    int keyframeRequestFailures_ = 0;
    int keyframeRequestTimeouts_ = 0;
    int decoderRecoveries_ = 0;
    int decoderPendingFrames_ = 0;
    int delayedDecodedFrames_ = 0;
    bool decoderHasPendingInput_ = false;
    int reconnectRenderedFramesBaseline_ = 0;
    int reconnectPauses_ = 0;
    int reconnectResumes_ = 0;
};

} // namespace display
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_DISPLAY_DISPLAY_MODULES_H
