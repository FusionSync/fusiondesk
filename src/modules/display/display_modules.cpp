#include "fusiondesk/modules/display/display_modules.h"

#include <chrono>
#include <utility>

#include "fusiondesk/core/module/module_catalog.h"
#include "fusiondesk/core/network/network_manager.h"

namespace fusiondesk {
namespace modules {
namespace display {

namespace {

protocol::ChannelId screenChannelId()
{
    return static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
}

protocol::ChannelId smallDataChannelId()
{
    return static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData);
}

network::ChannelKey screenChannelKey()
{
    return network::ChannelKey{screenChannelId(), protocol::ChannelType::Video};
}

network::ChannelKey displayControlChannelKey()
{
    return network::ChannelKey{smallDataChannelId(), protocol::ChannelType::Standard};
}

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

protocol::PacketEnvelope makeDisplayPacket(const module::ModuleRuntime& runtime,
                                           protocol::PacketType packetType,
                                           protocol::MessageKind messageKind)
{
    protocol::PacketEnvelope packet;
    packet.sessionId = runtime.session.sessionId;
    packet.traceId = runtime.session.traceId;
    const network::ChannelKey channel =
        packetType == protocol::PacketType::PayloadAck ?
        displayControlChannelKey() :
        screenChannelKey();
    packet.channelId = channel.channelId;
    packet.channelType = channel.channelType;
    packet.packetType = packetType;
    packet.messageKind = messageKind;
    return packet;
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

network::SendResult sendPacket(module::ModuleRuntime& runtime,
                               const protocol::PacketEnvelope& packet)
{
    if (runtime.networkManager != nullptr) {
        const network::EnqueueResult queued = runtime.networkManager->enqueue(packet);
        if (!queued.queued()) {
            const network::SendStatus status =
                queued.status == network::EnqueueStatus::Dropped ||
                        queued.pressure.level == network::PressureLevel::Congested ?
                network::SendStatus::BackPressure :
                network::SendStatus::Failed;
            return {status, queued.message};
        }

        return runtime.networkManager->flushPacket(packet);
    }

    if (runtime.network != nullptr)
        return runtime.network->send(packet);

    return {network::SendStatus::Failed, "network runtime is missing"};
}

bool hasEncodedPayload(const EncodedFrame& frame)
{
    return !frame.payload.empty();
}

bool hasCapturedFrameShape(const CapturedFrame& frame)
{
    return frame.width != 0 &&
           frame.height != 0 &&
           frame.strideBytes != 0 &&
           frame.pixelFormat != DisplayPixelFormat::Unknown;
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
    event.channel = screenChannelKey();
    event.severity = diagnostics::DiagnosticSeverity::Info;
    event.code = code;
    event.message = message;
    event.policyVersion = runtime.session.policyVersion;
    runtime.diagnostics->publish(event);
}

bool affectsDisplayChannel(const module::ModuleReconnectOptions& options)
{
    if (options.affectedChannels.empty())
        return true;

    for (const network::ChannelKey& key : options.affectedChannels) {
        if (key == screenChannelKey() || key == displayControlChannelKey())
            return true;
    }
    return false;
}

} // namespace

DisplayAgentModule::DisplayAgentModule(std::shared_ptr<IDisplayCapture> capture,
                                       std::shared_ptr<IVideoEncoder> encoder,
                                       const DisplayCaptureOpenOptions& captureOptions,
                                       const DisplayCodecRuntimeInfo& encoderCodec)
    : manifest_(module::catalog::displayScreenAgent()),
      capture_(std::move(capture)),
      encoder_(std::move(encoder)),
      captureOptions_(captureOptions),
      encoderCodec_(encoderCodec)
{
}

const module::ModuleManifest& DisplayAgentModule::manifest() const
{
    return manifest_;
}

module::ModuleState DisplayAgentModule::state() const
{
    return state_;
}

bool DisplayAgentModule::attach(const module::ModuleRuntime& runtime)
{
    runtime_ = runtime;
    state_ = module::ModuleState::Attached;
    return capture_ != nullptr && encoder_ != nullptr;
}

bool DisplayAgentModule::start(const module::ModuleStartOptions&)
{
    if ((runtime_.network == nullptr && runtime_.networkManager == nullptr) ||
        capture_ == nullptr || encoder_ == nullptr)
        return false;

    state_ = module::ModuleState::Starting;
    if (!capture_->open(captureOptions_)) {
        ++captureOpenFailures_;
        state_ = module::ModuleState::Failed;
        publish(runtime_, manifest_.moduleId, "display.capture_open_failed", "display capture open failed");
        return false;
    }

    captureOpened_ = true;
    state_ = module::ModuleState::Running;
    if (sendKeyFrame())
        return true;

    capture_->close();
    captureOpened_ = false;
    state_ = module::ModuleState::Failed;
    return false;
}

void DisplayAgentModule::stop(const module::ModuleStopOptions&)
{
    if (captureOpened_ && capture_ != nullptr) {
        capture_->close();
        captureOpened_ = false;
    }
    state_ = module::ModuleState::Stopped;
}

void DisplayAgentModule::detach()
{
    if (captureOpened_ && capture_ != nullptr) {
        capture_->close();
        captureOpened_ = false;
    }
    state_ = module::ModuleState::Detached;
}

void DisplayAgentModule::pauseForReconnect(const module::ModuleReconnectOptions&)
{
    ++reconnectPauses_;
    publish(runtime_, manifest_.moduleId, "display.reconnect_paused", "display sender paused for reconnect");
}

void DisplayAgentModule::resumeAfterReconnect(const module::ModuleReconnectOptions& options)
{
    ++reconnectResumes_;
    publish(runtime_, manifest_.moduleId, "display.reconnect_resumed", "display sender resumed after reconnect");
    if (state_ == module::ModuleState::Running && options.requestFreshState &&
        affectsDisplayChannel(options)) {
        const bool keyFrameSent = sendKeyFrame();
        if (keyFrameSent)
            sendDeltaFrame();
    }
}

void DisplayAgentModule::handlePacket(const protocol::PacketEnvelope& packet)
{
    if (packet.packetType != protocol::PacketType::PayloadAck ||
        packet.messageKind != protocol::MessageKind::Request) {
        return;
    }

    const DisplayControlDecodeResult decoded = decodeDisplayControlPayload(packet.payload);
    if (!decoded.ok ||
        decoded.payload.operation != DisplayControlOperation::RequestKeyframe) {
        ++invalidControlRequests_;
        publish(runtime_, manifest_.moduleId, "display.control_request_invalid", "display control request invalid");
        sendResponse(packet,
                     decoded.ok ? protocol::ResponseStatus::Unsupported : protocol::ResponseStatus::ProtocolError,
                     protocol::MessageKind::Error);
        return;
    }

    ++keyframeRequests_;
    publish(runtime_, manifest_.moduleId, "display.keyframe_requested", "keyframe requested");
    const bool sent = sendKeyFrame();
    DisplayControlPayload responsePayload;
    responsePayload.operation = DisplayControlOperation::KeyframeScheduled;
    responsePayload.reason = decoded.payload.reason;
    responsePayload.frameId = 0;
    sendResponse(packet,
                 sent ? protocol::ResponseStatus::Ok : protocol::ResponseStatus::Failed,
                 sent ? protocol::MessageKind::Response : protocol::MessageKind::Error,
                 encodeDisplayControlPayload(responsePayload));
}

std::string DisplayAgentModule::diagnostics() const
{
    return "display.agent captured=" + std::to_string(capturedFrames_) +
           " encoded=" + std::to_string(encodedFrames_) +
           " sent=" + std::to_string(sentFrames_) +
           " capturedPixelBytes=" +
               std::to_string(capturedPixelBytes_) +
           " encodedPayloadBytes=" +
               std::to_string(encodedPayloadBytes_) +
           " sentPayloadBytes=" +
               std::to_string(sentPayloadBytes_) +
           " lastSentPayloadBytes=" +
               std::to_string(lastSentPayloadBytes_) +
           " dropped=" + std::to_string(droppedFrames_) +
           " captureOpenFailures=" + std::to_string(captureOpenFailures_) +
           " captureErrors=" +
               std::to_string(capture_ == nullptr ? 0 : capture_->captureErrors()) +
           " encodeFailures=" + std::to_string(encodeFailures_) +
           " sendFailures=" + std::to_string(sendFailures_) +
           " invalidControlRequests=" + std::to_string(invalidControlRequests_) +
           " keyframeRequests=" + std::to_string(keyframeRequests_) +
           " reconnectPauses=" + std::to_string(reconnectPauses_) +
           " reconnectResumes=" + std::to_string(reconnectResumes_) +
           " captureGeometryOrFormatChanges=" +
               std::to_string(captureGeometryOrFormatChanges_);
}

bool DisplayAgentModule::sendDeltaFrame()
{
    return sendFrame(false);
}

bool DisplayAgentModule::reopenCapture(bool sendFreshKeyFrame)
{
    if ((runtime_.network == nullptr && runtime_.networkManager == nullptr) ||
        capture_ == nullptr || encoder_ == nullptr ||
        state_ != module::ModuleState::Running) {
        return false;
    }

    if (captureOpened_) {
        capture_->close();
        captureOpened_ = false;
    }

    if (!capture_->open(captureOptions_)) {
        ++captureOpenFailures_;
        publish(runtime_,
                manifest_.moduleId,
                "display.capture_reopen_failed",
                "display capture reopen failed");
        return false;
    }

    captureOpened_ = true;
    publish(runtime_,
            manifest_.moduleId,
            "display.capture_reopened",
            "display capture reopened");
    if (!sendFreshKeyFrame)
        return true;

    return sendKeyFrame();
}

bool DisplayAgentModule::replaceCapture(std::shared_ptr<IDisplayCapture> capture,
                                        bool sendFreshKeyFrame)
{
    if ((runtime_.network == nullptr && runtime_.networkManager == nullptr) ||
        capture == nullptr || encoder_ == nullptr ||
        state_ != module::ModuleState::Running) {
        return false;
    }

    if (captureOpened_ && capture_ != nullptr) {
        capture_->close();
        captureOpened_ = false;
    }

    capture_ = std::move(capture);
    if (!capture_->open(captureOptions_)) {
        ++captureOpenFailures_;
        publish(runtime_,
                manifest_.moduleId,
                "display.capture_switch_failed",
                "display capture switch failed");
        return false;
    }

    captureOpened_ = true;
    publish(runtime_,
            manifest_.moduleId,
            "display.capture_switched",
            "display capture switched");
    if (!sendFreshKeyFrame)
        return true;

    return sendKeyFrame();
}

int DisplayAgentModule::sentFrames() const
{
    return sentFrames_;
}

int DisplayAgentModule::droppedFrames() const
{
    return droppedFrames_;
}

int DisplayAgentModule::keyframeRequests() const
{
    return keyframeRequests_;
}

int DisplayAgentModule::responsesSent() const
{
    return responsesSent_;
}

DisplayAgentSnapshot DisplayAgentModule::snapshot() const
{
    DisplayAgentSnapshot result;
    result.state = state_;
    result.sourceId = captureOptions_.sourceId;
    result.targetWidth = captureOptions_.targetWidth;
    result.targetHeight = captureOptions_.targetHeight;
    result.scaleMode = captureOptions_.scaleMode;
    result.includeCursor = captureOptions_.includeCursor;
    result.capturedFrames = capturedFrames_;
    result.encodedFrames = encodedFrames_;
    result.sentFrames = sentFrames_;
    result.capturedPixelBytes = capturedPixelBytes_;
    result.encodedPayloadBytes = encodedPayloadBytes_;
    result.sentPayloadBytes = sentPayloadBytes_;
    result.lastCapturedPixelBytes = lastCapturedPixelBytes_;
    result.lastEncodedPayloadBytes = lastEncodedPayloadBytes_;
    result.lastSentPayloadBytes = lastSentPayloadBytes_;
    result.droppedFrames = droppedFrames_;
    result.captureOpenFailures = captureOpenFailures_;
    result.encodeFailures = encodeFailures_;
    result.sendFailures = sendFailures_;
    result.invalidControlRequests = invalidControlRequests_;
    result.keyframeRequests = keyframeRequests_;
    result.responsesSent = responsesSent_;
    result.captureGeometryOrFormatChanges = captureGeometryOrFormatChanges_;
    result.reconnectPauses = reconnectPauses_;
    result.reconnectResumes = reconnectResumes_;
    if (capture_ != nullptr) {
        result.captureBackendId = capture_->backendId();
        result.lastCaptureStatus = capture_->lastStatus();
        result.captureErrors = capture_->captureErrors();
    }
    result.encoderCodec = encoderCodec_.selected || !encoderCodec_.adapterId.empty()
                              ? encoderCodec_
                              : (encoder_ == nullptr ? DisplayCodecRuntimeInfo{}
                                                     : encoder_->codecRuntimeInfo());
    if (!captureOpened_ &&
        result.lastCaptureStatus.code == DisplayCaptureStatusCode::Ok) {
        result.lastCaptureStatus.code = DisplayCaptureStatusCode::NotOpen;
        result.lastCaptureStatus.recoverable = true;
        result.lastCaptureStatus.message = "display capture is not open";
    }
    return result;
}

bool DisplayAgentModule::sendKeyFrame()
{
    return sendFrame(true);
}

bool DisplayAgentModule::sendFrame(bool keyFrame)
{
    if ((runtime_.network == nullptr && runtime_.networkManager == nullptr) ||
        capture_ == nullptr || encoder_ == nullptr)
        return false;

    if (shouldDropFrame(keyFrame)) {
        ++droppedFrames_;
        publish(runtime_, manifest_.moduleId, "display.frame_dropped", "delta frame dropped under pressure");
        return false;
    }

    CapturedFrame captured = capture_->captureNextFrame(keyFrame);
    if (!keyFrame && frameShapeChanged(captured)) {
        captured.keyFrame = true;
        ++captureGeometryOrFormatChanges_;
        publish(runtime_,
                manifest_.moduleId,
                "display.capture_geometry_or_format_changed",
                "display capture geometry or pixel format changed");
    }
    ++capturedFrames_;
    lastCapturedPixelBytes_ =
        static_cast<std::uint64_t>(captured.pixels.size());
    capturedPixelBytes_ += lastCapturedPixelBytes_;
    const EncodedFrame encoded = encoder_->encode(captured);
    if (!hasEncodedPayload(encoded)) {
        ++encodeFailures_;
        publish(runtime_, manifest_.moduleId, "display.frame_encode_failed", "display frame encode failed");
        return false;
    }
    ++encodedFrames_;
    lastEncodedPayloadBytes_ =
        static_cast<std::uint64_t>(encoded.payload.size());
    encodedPayloadBytes_ += lastEncodedPayloadBytes_;

    protocol::PacketEnvelope packet = makeDisplayPacket(runtime_,
                                                       protocol::PacketType::Video,
                                                       protocol::MessageKind::Event);
    packet.priority = protocol::PacketPriority::Realtime;
    packet.sequence = encoded.frameId;
    packet.monotonicTimestampUsec = encoded.monotonicTimestampUsec;
    packet.flags = protocol::PacketFlagNoResponseRequired;
    if (encoded.keyFrame)
        packet.flags |= protocol::PacketFlagKeyFrame;
    packet.payload = encoded.payload;

    const network::SendResult result = sendPacket(runtime_, packet);
    if (result.status != network::SendStatus::Sent) {
        ++sendFailures_;
        return false;
    }

    publish(runtime_,
            manifest_.moduleId,
            sentFrames_ == 0 ? "display.first_frame_sent" :
                (encoded.keyFrame ? "display.keyframe_sent" : "display.frame_sent"),
            encoded.keyFrame ? "keyframe sent" : "frame sent");
    ++sentFrames_;
    lastSentPayloadBytes_ = lastEncodedPayloadBytes_;
    sentPayloadBytes_ += lastSentPayloadBytes_;
    recordSentFrameShape(encoded, captured.sourceId);
    return true;
}

bool DisplayAgentModule::frameShapeChanged(const CapturedFrame& frame) const
{
    if (!lastSentFrameShapeValid_ || !hasCapturedFrameShape(frame))
        return false;

    return frame.sourceId != lastSentSourceId_ ||
           frame.width != lastSentFrameWidth_ ||
           frame.height != lastSentFrameHeight_ ||
           frame.strideBytes != lastSentFrameStrideBytes_ ||
           frame.pixelFormat != lastSentFramePixelFormat_;
}

void DisplayAgentModule::recordSentFrameShape(const EncodedFrame& frame,
                                              std::uint32_t sourceId)
{
    if (frame.width == 0 ||
        frame.height == 0 ||
        frame.strideBytes == 0 ||
        frame.pixelFormat == DisplayPixelFormat::Unknown)
        return;

    lastSentFrameShapeValid_ = true;
    lastSentSourceId_ = sourceId;
    lastSentFrameWidth_ = frame.width;
    lastSentFrameHeight_ = frame.height;
    lastSentFrameStrideBytes_ = frame.strideBytes;
    lastSentFramePixelFormat_ = frame.pixelFormat;
}

bool DisplayAgentModule::shouldDropFrame(bool keyFrame) const
{
    if (keyFrame || runtime_.channels == nullptr)
        return false;

    const network::ChannelSnapshot snapshot =
        runtime_.channels->snapshot(screenChannelKey());
    return snapshot.pressure.level == network::PressureLevel::Congested ||
           snapshot.pressure.level == network::PressureLevel::Draining;
}

bool DisplayAgentModule::sendResponse(const protocol::PacketEnvelope& request,
                                      protocol::ResponseStatus status,
                                      protocol::MessageKind kind,
                                      const protocol::ByteBuffer& payload)
{
    protocol::PacketEnvelope response = makeDisplayPacket(runtime_,
                                                         protocol::PacketType::PayloadAck,
                                                         kind);
    response.priority = protocol::PacketPriority::Interactive;
    response.messageId = nextMessageId_++;
    response.correlationId = request.correlationId != 0 ? request.correlationId : request.messageId;
    response.responseTo = request.messageId;
    response.timeoutMs = request.timeoutMs;
    response.responseStatus = status;
    response.payload = payload;

    const network::SendResult result = sendPacket(runtime_, response);
    if (result.status != network::SendStatus::Sent) {
        ++sendFailures_;
        return false;
    }

    publish(runtime_,
            manifest_.moduleId,
            status == protocol::ResponseStatus::Ok ?
                "display.keyframe_response_sent" :
                "display.keyframe_response_failed",
            status == protocol::ResponseStatus::Ok ? "keyframe response sent" : "keyframe response failed");
    ++responsesSent_;
    return true;
}

DisplayClientModule::DisplayClientModule(std::shared_ptr<IVideoDecoder> decoder,
                                         std::shared_ptr<IDisplayRenderer> renderer,
                                         const DisplayCodecRuntimeInfo& decoderCodec)
    : manifest_(module::catalog::displayScreenClient()),
      decoder_(std::move(decoder)),
      renderer_(std::move(renderer)),
      decoderCodec_(decoderCodec)
{
}

const module::ModuleManifest& DisplayClientModule::manifest() const
{
    return manifest_;
}

module::ModuleState DisplayClientModule::state() const
{
    return state_;
}

bool DisplayClientModule::attach(const module::ModuleRuntime& runtime)
{
    runtime_ = runtime;
    state_ = module::ModuleState::Attached;
    return decoder_ != nullptr && renderer_ != nullptr;
}

bool DisplayClientModule::start(const module::ModuleStartOptions&)
{
    if ((runtime_.network == nullptr && runtime_.networkManager == nullptr) ||
        decoder_ == nullptr || renderer_ == nullptr)
        return false;

    state_ = module::ModuleState::Running;
    return true;
}

void DisplayClientModule::stop(const module::ModuleStopOptions&)
{
    state_ = module::ModuleState::Stopped;
}

void DisplayClientModule::detach()
{
    state_ = module::ModuleState::Detached;
}

void DisplayClientModule::pauseForReconnect(const module::ModuleReconnectOptions&)
{
    ++reconnectPauses_;
    publish(runtime_, manifest_.moduleId, "display.reconnect_paused", "display renderer paused for reconnect");
}

void DisplayClientModule::resumeAfterReconnect(const module::ModuleReconnectOptions& options)
{
    ++reconnectResumes_;
    reconnectRenderedFramesBaseline_ = renderedFrames_;
    publish(runtime_, manifest_.moduleId, "display.reconnect_resumed", "display renderer resumed after reconnect");
    if (state_ == module::ModuleState::Running && options.requestFreshState && affectsDisplayChannel(options))
        requestKeyFrame(DisplayKeyframeReason::Reconnect);
}

void DisplayClientModule::handlePacket(const protocol::PacketEnvelope& packet)
{
    if (packet.packetType == protocol::PacketType::Video) {
        renderVideo(packet);
        return;
    }

    if (packet.packetType == protocol::PacketType::PayloadAck &&
        (packet.messageKind == protocol::MessageKind::Response ||
         packet.messageKind == protocol::MessageKind::Error)) {
        if (!keyframeRequestsTracker_.complete(packet))
            return;
    }
}

std::string DisplayClientModule::diagnostics() const
{
    return "display.client received=" + std::to_string(receivedFrames_) +
           " decoded=" + std::to_string(decodedFrames_) +
           " rendered=" + std::to_string(renderedFrames_) +
           " receivedPayloadBytes=" +
               std::to_string(receivedPayloadBytes_) +
           " decodedPixelBytes=" +
               std::to_string(decodedPixelBytes_) +
           " renderedPixelBytes=" +
               std::to_string(renderedPixelBytes_) +
           " lastReceivedPayloadBytes=" +
               std::to_string(lastReceivedPayloadBytes_) +
           " dropped=" + std::to_string(droppedFrames_) +
           " frameGaps=" + std::to_string(frameGaps_) +
           " decodeErrors=" + std::to_string(decodeErrors_) +
           " renderErrors=" + std::to_string(renderErrors_) +
           " keyframeResponses=" + std::to_string(keyframeResponses_) +
           " keyframeRequestFailures=" + std::to_string(keyframeRequestFailures_) +
           " keyframeRequestTimeouts=" + std::to_string(keyframeRequestTimeouts_) +
           " decoderRecoveries=" + std::to_string(decoderRecoveries_) +
           " decoderPendingFrames=" +
               std::to_string(decoderPendingFrames_) +
           " delayedDecodedFrames=" +
               std::to_string(delayedDecodedFrames_) +
           " lastRenderedFrameId=" + std::to_string(lastRenderedFrameId_) +
           " reconnectPauses=" + std::to_string(reconnectPauses_) +
           " reconnectResumes=" + std::to_string(reconnectResumes_);
}

bool DisplayClientModule::requestKeyFrame(DisplayKeyframeReason reason)
{
    if (runtime_.network == nullptr && runtime_.networkManager == nullptr)
        return false;

    protocol::PacketEnvelope request = makeDisplayPacket(runtime_,
                                                        protocol::PacketType::PayloadAck,
                                                        protocol::MessageKind::Request);
    request.priority = protocol::PacketPriority::Interactive;
    request.messageId = keyframeRequestsTracker_.nextMessageId();
    request.correlationId = request.messageId;
    request.timeoutMs = 1000;
    request.flags = protocol::PacketFlagResponseRequired;
    request.monotonicTimestampUsec = monotonicNowUsec();
    DisplayControlPayload payload;
    payload.operation = DisplayControlOperation::RequestKeyframe;
    payload.reason = reason;
    request.payload = encodeDisplayControlPayload(payload);
    lastKeyframeRequestId_ = request.messageId;

    const network::TrackResult tracked = keyframeRequestsTracker_.track(
        request,
        [this](const protocol::PacketEnvelope& response) {
            handleKeyframeResponse(response);
        });
    if (!tracked.tracked) {
        ++keyframeRequestFailures_;
        return false;
    }

    const network::SendResult sent = sendPacket(runtime_, request);
    if (sent.status == network::SendStatus::Sent)
        return true;

    protocol::PacketEnvelope failed = request;
    failed.messageKind = protocol::MessageKind::Error;
    failed.responseStatus = sendStatusToResponseStatus(sent.status);
    failed.responseTo = request.messageId;
    keyframeRequestsTracker_.complete(failed);
    return false;
}

std::size_t DisplayClientModule::expireKeyframeRequests(std::uint64_t nowUsec)
{
    const std::size_t expired = keyframeRequestsTracker_.expire(nowUsec);
    keyframeRequestTimeouts_ += static_cast<int>(expired);
    return expired;
}

int DisplayClientModule::receivedFrames() const
{
    return receivedFrames_;
}

int DisplayClientModule::renderedFrames() const
{
    return renderedFrames_;
}

int DisplayClientModule::keyframeResponses() const
{
    return keyframeResponses_;
}

protocol::MessageId DisplayClientModule::lastKeyframeRequestId() const
{
    return lastKeyframeRequestId_;
}

DisplayClientSnapshot DisplayClientModule::snapshot() const
{
    DisplayClientSnapshot result;
    result.state = state_;
    result.receivedFrames = receivedFrames_;
    result.decodedFrames = decodedFrames_;
    result.renderedFrames = renderedFrames_;
    result.receivedPayloadBytes = receivedPayloadBytes_;
    result.decodedPixelBytes = decodedPixelBytes_;
    result.renderedPixelBytes = renderedPixelBytes_;
    result.lastReceivedPayloadBytes = lastReceivedPayloadBytes_;
    result.lastDecodedPixelBytes = lastDecodedPixelBytes_;
    result.lastRenderedPixelBytes = lastRenderedPixelBytes_;
    result.droppedFrames = droppedFrames_;
    result.frameGaps = frameGaps_;
    result.decodeErrors = decodeErrors_;
    result.renderErrors = renderErrors_;
    result.keyframeResponses = keyframeResponses_;
    result.keyframeRequestFailures = keyframeRequestFailures_;
    result.keyframeRequestTimeouts = keyframeRequestTimeouts_;
    result.decoderRecoveries = decoderRecoveries_;
    result.decoderPendingFrames = decoderPendingFrames_;
    result.delayedDecodedFrames = delayedDecodedFrames_;
    result.pendingKeyframeRequests = keyframeRequestsTracker_.pendingCount();
    result.lastKeyframeRequestId = lastKeyframeRequestId_;
    result.lastRenderedFrameId = lastRenderedFrameId_;
    result.reconnectRenderedFramesBaseline = reconnectRenderedFramesBaseline_;
    result.reconnectPauses = reconnectPauses_;
    result.reconnectResumes = reconnectResumes_;
    result.decoderCodec = decoderCodec_.selected || !decoderCodec_.adapterId.empty()
                              ? decoderCodec_
                              : (decoder_ == nullptr ? DisplayCodecRuntimeInfo{}
                                                     : decoder_->codecRuntimeInfo());
    return result;
}

bool DisplayClientModule::renderVideo(const protocol::PacketEnvelope& packet)
{
    if (packet.messageKind != protocol::MessageKind::Event)
        return false;

    EncodedFrame encoded;
    encoded.frameId = packet.sequence;
    encoded.keyFrame = (packet.flags & protocol::PacketFlagKeyFrame) != 0;
    encoded.payload = packet.payload;
    ++receivedFrames_;
    lastReceivedPayloadBytes_ =
        static_cast<std::uint64_t>(packet.payload.size());
    receivedPayloadBytes_ += lastReceivedPayloadBytes_;

    if (renderedFrames_ == 0 && !decoderHasPendingInput_ &&
        !encoded.keyFrame) {
        ++droppedFrames_;
        ++decoderRecoveries_;
        publish(runtime_, manifest_.moduleId, "display.first_frame_missing", "first display frame is not a keyframe");
        requestRecoveryKeyFrame(DisplayKeyframeReason::FirstFrameTimeout);
        return false;
    }

    if (renderedFrames_ > 0 && encoded.frameId <= lastRenderedFrameId_) {
        ++droppedFrames_;
        publish(runtime_, manifest_.moduleId, "display.frame_stale_dropped", "stale display frame dropped");
        return false;
    }

    const DecodedFrame decoded = decoder_->decode(encoded);
    if (decoded.decodeStatus == DisplayDecodeStatus::NeedsMoreInput) {
        ++decoderPendingFrames_;
        publish(runtime_,
                manifest_.moduleId,
                "display.decoder_needs_more_input",
                "display decoder is waiting for more input");
        decoderHasPendingInput_ = true;
        return true;
    }

    if (decoded.decodeStatus != DisplayDecodeStatus::Ok ||
        decoded.frameId == 0 ||
        decoded.frameId > encoded.frameId) {
        decoderHasPendingInput_ = false;
        ++decodeErrors_;
        ++droppedFrames_;
        ++decoderRecoveries_;
        publish(runtime_, manifest_.moduleId, "display.frame_decode_failed", "display frame decode failed");
        requestRecoveryKeyFrame(DisplayKeyframeReason::DecoderReset);
        return false;
    }

    if (decoded.frameId <= lastRenderedFrameId_) {
        ++droppedFrames_;
        publish(runtime_,
                manifest_.moduleId,
                "display.frame_stale_dropped",
                "stale decoded display frame dropped");
        return false;
    }

    if (renderedFrames_ > 0 &&
        decoded.frameId > lastRenderedFrameId_ + 1 &&
        !decoded.keyFrame) {
        ++droppedFrames_;
        ++frameGaps_;
        ++decoderRecoveries_;
        publish(runtime_, manifest_.moduleId, "display.frame_gap_detected", "display frame gap detected");
        requestRecoveryKeyFrame(DisplayKeyframeReason::FrameGap);
        return false;
    }

    if (decoded.frameId != encoded.frameId)
        ++delayedDecodedFrames_;
    decoderHasPendingInput_ = decoded.frameId < encoded.frameId;

    ++decodedFrames_;
    lastDecodedPixelBytes_ =
        static_cast<std::uint64_t>(decoded.pixels.size());
    decodedPixelBytes_ += lastDecodedPixelBytes_;
    if (!renderer_->render(decoded)) {
        ++renderErrors_;
        ++decoderRecoveries_;
        requestRecoveryKeyFrame(DisplayKeyframeReason::DecoderReset);
        return false;
    }
    lastRenderedPixelBytes_ = lastDecodedPixelBytes_;
    renderedPixelBytes_ += lastRenderedPixelBytes_;

    publish(runtime_,
            manifest_.moduleId,
            renderedFrames_ == 0 ? "display.first_frame_rendered" :
                (decoded.keyFrame ? "display.keyframe_rendered" : "display.frame_rendered"),
            decoded.keyFrame ? "keyframe rendered" : "frame rendered");
    ++renderedFrames_;
    lastRenderedFrameId_ = decoded.frameId;
    return true;
}

void DisplayClientModule::handleKeyframeResponse(const protocol::PacketEnvelope& packet)
{
    if (packet.messageKind != protocol::MessageKind::Response ||
        packet.responseStatus != protocol::ResponseStatus::Ok) {
        ++keyframeRequestFailures_;
        publish(runtime_, manifest_.moduleId, "display.keyframe_response_failed", "keyframe response failed");
        return;
    }

    const DisplayControlDecodeResult decoded = decodeDisplayControlPayload(packet.payload);
    if (!decoded.ok ||
        decoded.payload.operation != DisplayControlOperation::KeyframeScheduled) {
        ++keyframeRequestFailures_;
        publish(runtime_, manifest_.moduleId, "display.keyframe_response_invalid", "keyframe response invalid");
        return;
    }

    publish(runtime_, manifest_.moduleId, "display.keyframe_response_received", "keyframe response received");
    ++keyframeResponses_;
}

bool DisplayClientModule::requestRecoveryKeyFrame(DisplayKeyframeReason reason)
{
    return requestKeyFrame(reason);
}

} // namespace display
} // namespace modules
} // namespace fusiondesk
