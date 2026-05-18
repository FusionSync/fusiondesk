#include <cassert>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/protocol/packet_codec.h"
#include "fusiondesk/core/protocol/protocol_validator.h"
#include "fusiondesk/runtime/connection/reconnect_teardown_ack.h"

using namespace fusiondesk;

namespace {

network::ChannelKey controlKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
                               protocol::ChannelType::Control};
}

network::ChannelKey screenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                               protocol::ChannelType::Video};
}

runtime::connection::ReconnectTeardownResponse makeResponse(
    const runtime::connection::ReconnectTeardownCommand& command,
    protocol::MessageKind messageKind = protocol::MessageKind::Response,
    protocol::ResponseStatus responseStatus = protocol::ResponseStatus::Ok)
{
    runtime::connection::ReconnectTeardownResponse response;
    response.sessionId = command.sessionId;
    response.targetChannel = command.targetChannel;
    response.responseTo = command.messageId;
    response.correlationId = command.correlationId;
    response.messageKind = messageKind;
    response.responseStatus = responseStatus;
    return response;
}

void buildsCorrelatedTeardownCommands()
{
    runtime::connection::ReconnectTeardownPlanRequest request;
    request.sessionId = 7001;
    request.channels = {screenKey(), controlKey()};
    request.firstMessageId = 900;
    request.timeoutMs = 2500;
    request.reason = "rebind committed";

    const runtime::connection::ReconnectTeardownPlanResult result =
        runtime::connection::buildReconnectTeardownPlan(request);

    assert(result.ok);
    assert(result.plan.commands.size() == 2);
    assert(result.plan.commands.front().sessionId == 7001);
    assert(result.plan.commands.front().targetChannel == screenKey());
    assert(result.plan.commands.front().messageId == 900);
    assert(result.plan.commands.front().correlationId == 900);
    assert(result.plan.commands.front().timeoutMs == 2500);
    assert(result.plan.commands.front().reason == "rebind committed");
    assert(result.plan.commands.back().messageId == 901);
    assert(result.plan.commands.back().correlationId == 901);
}

void buildsFromReconnectSidePlan()
{
    runtime::connection::ReconnectOrchestrationSidePlan sidePlan;
    sidePlan.sessionId = 8001;
    sidePlan.teardownAfterSuccessfulRebind = {screenKey()};
    sidePlan.reason = "screen replacement accepted";

    const runtime::connection::ReconnectTeardownPlanResult result =
        runtime::connection::buildReconnectTeardownPlan(sidePlan, 33, 1500);

    assert(result.ok);
    assert(result.plan.commands.size() == 1);
    assert(result.plan.commands.front().sessionId == 8001);
    assert(result.plan.commands.front().targetChannel == screenKey());
    assert(result.plan.commands.front().messageId == 33);
    assert(result.plan.commands.front().correlationId == 33);
    assert(result.plan.commands.front().timeoutMs == 1500);
    assert(result.plan.commands.front().reason == "screen replacement accepted");
}

void summarizesCompleteResponses()
{
    runtime::connection::ReconnectTeardownPlanRequest request;
    request.sessionId = 7002;
    request.channels = {screenKey()};
    request.firstMessageId = 1001;

    const runtime::connection::ReconnectTeardownPlanResult plan =
        runtime::connection::buildReconnectTeardownPlan(request);
    assert(plan.ok);

    const runtime::connection::ReconnectTeardownResponseSummary summary =
        runtime::connection::summarizeReconnectTeardownResponses(
            plan.plan,
            {makeResponse(plan.plan.commands.front())});

    assert(summary.complete);
    assert(summary.ok);
    assert(summary.completedTargetChannels.size() == 1);
    assert(summary.completedTargetChannels.front() == screenKey());
    assert(summary.messages.empty());
}

void buildsRequestEnvelopeWithTargetPayload()
{
    runtime::connection::ReconnectTeardownPlanRequest request;
    request.sessionId = 7101;
    request.channels = {screenKey()};
    request.firstMessageId = 1200;
    request.timeoutMs = 3000;
    request.reason = "replacement channel bound";
    const runtime::connection::ReconnectTeardownPlanResult plan =
        runtime::connection::buildReconnectTeardownPlan(request);
    assert(plan.ok);

    runtime::connection::ReconnectTeardownWireOptions wireOptions;
    wireOptions.traceId = 44;
    wireOptions.sequence = 7;
    wireOptions.monotonicTimestampUsec = 123456;
    wireOptions.controlChannel = screenKey();
    const protocol::PacketEnvelope packet =
        runtime::connection::makeReconnectTeardownRequestPacket(plan.plan.commands.front(), wireOptions);

    assert(packet.sessionId == 7101);
    assert(packet.traceId == 44);
    assert(packet.messageId == 1200);
    assert(packet.correlationId == 1200);
    assert(packet.channelId == controlKey().channelId);
    assert(packet.channelType == controlKey().channelType);
    assert(packet.packetType == protocol::PacketType::Control);
    assert(packet.messageKind == protocol::MessageKind::Request);
    assert(packet.priority == protocol::PacketPriority::Critical);
    assert(packet.sequence == 7);
    assert(packet.monotonicTimestampUsec == 123456);
    assert(packet.timeoutMs == 3000);
    assert((packet.flags & protocol::PacketFlagResponseRequired) != 0);

    protocol::ProtocolValidator validator;
    assert(validator.validate(packet).valid);

    protocol::PacketCodec codec;
    const protocol::PacketDecodeResult decoded = codec.decode(codec.encode(packet));
    assert(decoded.ok());

    const runtime::connection::ReconnectTeardownWireDecodeResult command =
        runtime::connection::decodeReconnectTeardownCommandPacket(decoded.packet);
    assert(command.ok);
    assert(command.command.sessionId == 7101);
    assert(command.command.targetChannel == screenKey());
    assert(command.command.messageId == 1200);
    assert(command.command.correlationId == 1200);
    assert(command.command.timeoutMs == 3000);
    assert(command.command.reason == "replacement channel bound");
}

void rejectsNonControlEnvelopeRoutes()
{
    runtime::connection::ReconnectTeardownPlanRequest request;
    request.sessionId = 7105;
    request.channels = {screenKey()};
    request.firstMessageId = 1600;
    const runtime::connection::ReconnectTeardownPlanResult plan =
        runtime::connection::buildReconnectTeardownPlan(request);
    assert(plan.ok);

    protocol::PacketEnvelope packet =
        runtime::connection::makeReconnectTeardownRequestPacket(plan.plan.commands.front());
    packet.channelId = screenKey().channelId;
    packet.channelType = screenKey().channelType;

    const runtime::connection::ReconnectTeardownWireDecodeResult decoded =
        runtime::connection::decodeReconnectTeardownCommandPacket(packet);
    assert(!decoded.ok);
}

void buildsTerminalResponseEnvelope()
{
    runtime::connection::ReconnectTeardownPlanRequest request;
    request.sessionId = 7102;
    request.channels = {screenKey()};
    request.firstMessageId = 1300;
    const runtime::connection::ReconnectTeardownPlanResult plan =
        runtime::connection::buildReconnectTeardownPlan(request);
    assert(plan.ok);

    const protocol::PacketEnvelope requestPacket =
        runtime::connection::makeReconnectTeardownRequestPacket(plan.plan.commands.front());
    runtime::connection::ReconnectTeardownWireResponseOptions responseOptions;
    responseOptions.messageId = 2300;
    responseOptions.sequence = 8;
    responseOptions.monotonicTimestampUsec = 456789;
    responseOptions.status = protocol::ResponseStatus::Ok;
    responseOptions.message = "old transport closed";
    const protocol::PacketEnvelope responsePacket =
        runtime::connection::makeReconnectTeardownResponsePacket(requestPacket, responseOptions);

    assert(responsePacket.sessionId == requestPacket.sessionId);
    assert(responsePacket.messageId == 2300);
    assert(responsePacket.correlationId == requestPacket.correlationId);
    assert(responsePacket.responseTo == requestPacket.messageId);
    assert(responsePacket.channelId == requestPacket.channelId);
    assert(responsePacket.channelType == requestPacket.channelType);
    assert(responsePacket.packetType == protocol::PacketType::Control);
    assert(responsePacket.messageKind == protocol::MessageKind::Response);
    assert(responsePacket.responseStatus == protocol::ResponseStatus::Ok);
    assert(responsePacket.sequence == 8);
    assert(responsePacket.monotonicTimestampUsec == 456789);

    protocol::ProtocolValidator validator;
    assert(validator.validate(responsePacket).valid);

    protocol::PacketCodec codec;
    const protocol::PacketDecodeResult decoded = codec.decode(codec.encode(responsePacket));
    assert(decoded.ok());

    const runtime::connection::ReconnectTeardownResponse response =
        runtime::connection::reconnectTeardownResponseFromPacket(plan.plan.commands.front(), decoded.packet);
    const runtime::connection::ReconnectTeardownResponseSummary summary =
        runtime::connection::summarizeReconnectTeardownResponses(plan.plan, {response});
    assert(summary.complete);
    assert(summary.ok);
    assert(summary.completedTargetChannels.size() == 1);
    assert(response.message == "old transport closed");

    runtime::connection::ReconnectTeardownWireResponseOptions defaultMessageIdOptions;
    const protocol::PacketEnvelope defaultMessageIdResponse =
        runtime::connection::makeReconnectTeardownResponsePacket(requestPacket, defaultMessageIdOptions);
    assert(defaultMessageIdResponse.messageId != 0);
    assert(defaultMessageIdResponse.messageId != requestPacket.messageId);
}

void ackEnvelopeDoesNotCompleteTeardown()
{
    runtime::connection::ReconnectTeardownPlanRequest request;
    request.sessionId = 7103;
    request.channels = {screenKey()};
    request.firstMessageId = 1400;
    const runtime::connection::ReconnectTeardownPlanResult plan =
        runtime::connection::buildReconnectTeardownPlan(request);
    assert(plan.ok);

    const protocol::PacketEnvelope requestPacket =
        runtime::connection::makeReconnectTeardownRequestPacket(plan.plan.commands.front());
    runtime::connection::ReconnectTeardownWireResponseOptions responseOptions;
    responseOptions.messageId = 2400;
    responseOptions.status = protocol::ResponseStatus::Accepted;
    const protocol::PacketEnvelope ackPacket =
        runtime::connection::makeReconnectTeardownResponsePacket(requestPacket, responseOptions);

    assert(ackPacket.messageKind == protocol::MessageKind::Ack);
    assert(ackPacket.responseStatus == protocol::ResponseStatus::Accepted);
    protocol::ProtocolValidator validator;
    assert(validator.validate(ackPacket).valid);

    const runtime::connection::ReconnectTeardownResponse response =
        runtime::connection::reconnectTeardownResponseFromPacket(plan.plan.commands.front(), ackPacket);
    const runtime::connection::ReconnectTeardownResponseSummary summary =
        runtime::connection::summarizeReconnectTeardownResponses(plan.plan, {response});
    assert(!summary.complete);
    assert(!summary.ok);
    assert(!summary.messages.empty());
}

void rejectsInvalidPlanInputs()
{
    runtime::connection::ReconnectTeardownPlanRequest missingSession;
    missingSession.channels = {screenKey()};
    assert(!runtime::connection::buildReconnectTeardownPlan(missingSession).ok);

    runtime::connection::ReconnectTeardownPlanRequest missingChannels;
    missingChannels.sessionId = 7003;
    assert(!runtime::connection::buildReconnectTeardownPlan(missingChannels).ok);

    runtime::connection::ReconnectTeardownPlanRequest duplicateChannels;
    duplicateChannels.sessionId = 7004;
    duplicateChannels.channels = {screenKey(), screenKey()};
    assert(!runtime::connection::buildReconnectTeardownPlan(duplicateChannels).ok);

    runtime::connection::ReconnectTeardownPlanRequest missingCorrelationSeed;
    missingCorrelationSeed.sessionId = 7005;
    missingCorrelationSeed.channels = {screenKey()};
    missingCorrelationSeed.firstMessageId = 0;
    assert(!runtime::connection::buildReconnectTeardownPlan(missingCorrelationSeed).ok);
}

void rejectsMalformedRequestPackets()
{
    runtime::connection::ReconnectTeardownPlanRequest request;
    request.sessionId = 7104;
    request.channels = {screenKey()};
    request.firstMessageId = 1500;
    const runtime::connection::ReconnectTeardownPlanResult plan =
        runtime::connection::buildReconnectTeardownPlan(request);
    assert(plan.ok);

    protocol::PacketEnvelope packet =
        runtime::connection::makeReconnectTeardownRequestPacket(plan.plan.commands.front());
    packet.payload.clear();
    assert(!runtime::connection::decodeReconnectTeardownCommandPacket(packet).ok);

    packet = runtime::connection::makeReconnectTeardownRequestPacket(plan.plan.commands.front());
    packet.messageKind = protocol::MessageKind::Ack;
    assert(!runtime::connection::decodeReconnectTeardownCommandPacket(packet).ok);

    packet = runtime::connection::makeReconnectTeardownRequestPacket(plan.plan.commands.front());
    packet.flags = protocol::PacketFlagNone;
    assert(!runtime::connection::decodeReconnectTeardownCommandPacket(packet).ok);
}

void rejectsMismatchedIncompleteOrNonTerminalResponses()
{
    runtime::connection::ReconnectTeardownPlanRequest request;
    request.sessionId = 7006;
    request.channels = {screenKey(), controlKey()};
    const runtime::connection::ReconnectTeardownPlanResult plan =
        runtime::connection::buildReconnectTeardownPlan(request);
    assert(plan.ok);

    runtime::connection::ReconnectTeardownResponse wrongCorrelation =
        makeResponse(plan.plan.commands.front());
    wrongCorrelation.correlationId += 1;
    const runtime::connection::ReconnectTeardownResponseSummary mismatched =
        runtime::connection::summarizeReconnectTeardownResponses(plan.plan, {wrongCorrelation});
    assert(!mismatched.ok);
    assert(!mismatched.complete);
    assert(!mismatched.messages.empty());

    const runtime::connection::ReconnectTeardownResponseSummary incomplete =
        runtime::connection::summarizeReconnectTeardownResponses(
            plan.plan,
            {makeResponse(plan.plan.commands.front())});
    assert(!incomplete.ok);
    assert(!incomplete.complete);

    runtime::connection::ReconnectTeardownResponse nonTerminal =
        makeResponse(plan.plan.commands.front(), protocol::MessageKind::Ack, protocol::ResponseStatus::Accepted);
    const runtime::connection::ReconnectTeardownResponseSummary nonTerminalSummary =
        runtime::connection::summarizeReconnectTeardownResponses(plan.plan, {nonTerminal});
    assert(!nonTerminalSummary.ok);
    assert(!nonTerminalSummary.complete);
    assert(!nonTerminalSummary.messages.empty());

    runtime::connection::ReconnectTeardownResponse failed =
        makeResponse(plan.plan.commands.front(), protocol::MessageKind::Error, protocol::ResponseStatus::Failed);
    failed.message = "remote close failed";
    const runtime::connection::ReconnectTeardownResponseSummary failedSummary =
        runtime::connection::summarizeReconnectTeardownResponses(
            plan.plan,
            {failed, makeResponse(plan.plan.commands.back())});
    assert(!failedSummary.ok);
    assert(failedSummary.complete);
    assert(!failedSummary.messages.empty());
}

} // namespace

int main()
{
    buildsCorrelatedTeardownCommands();
    buildsFromReconnectSidePlan();
    summarizesCompleteResponses();
    buildsRequestEnvelopeWithTargetPayload();
    rejectsNonControlEnvelopeRoutes();
    buildsTerminalResponseEnvelope();
    ackEnvelopeDoesNotCompleteTeardown();
    rejectsInvalidPlanInputs();
    rejectsMalformedRequestPackets();
    rejectsMismatchedIncompleteOrNonTerminalResponses();
    return 0;
}
