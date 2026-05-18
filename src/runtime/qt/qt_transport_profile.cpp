#include "fusiondesk/runtime/qt/qt_transport_profile.h"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

#include <QByteArray>
#include <QFile>
#include <QHostAddress>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include "fusiondesk/runtime/runtime_host.h"

namespace fusiondesk {
namespace runtime {
namespace qt {

namespace {

void appendFailure(QtTransportConnectResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendFailure(QtTransportProfileLoadResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendFailure(QtReconnectOrchestrationRequestLoadResult& result,
                   std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendFailure(QtReconnectReplacementResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendFailure(QtReconnectResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

std::optional<std::uint64_t> readUnsigned(const QJsonObject& object, const char* field)
{
    const QJsonValue value = object.value(QLatin1String(field));
    if (!value.isDouble())
        return std::nullopt;

    const double numeric = value.toDouble(-1.0);
    if (numeric < 0.0)
        return std::nullopt;

    return static_cast<std::uint64_t>(numeric);
}

std::optional<protocol::ChannelType> channelTypeFromString(const QString& value)
{
    if (value == QLatin1String("standard"))
        return protocol::ChannelType::Standard;
    if (value == QLatin1String("video"))
        return protocol::ChannelType::Video;
    if (value == QLatin1String("audio"))
        return protocol::ChannelType::Audio;
    if (value == QLatin1String("bulk"))
        return protocol::ChannelType::Bulk;
    if (value == QLatin1String("control"))
        return protocol::ChannelType::Control;
    if (value == QLatin1String("input"))
        return protocol::ChannelType::Input;

    return std::nullopt;
}

QString channelTypeToString(protocol::ChannelType value)
{
    switch (value) {
    case protocol::ChannelType::Standard:
        return QLatin1String("standard");
    case protocol::ChannelType::Video:
        return QLatin1String("video");
    case protocol::ChannelType::Audio:
        return QLatin1String("audio");
    case protocol::ChannelType::Bulk:
        return QLatin1String("bulk");
    case protocol::ChannelType::Control:
        return QLatin1String("control");
    case protocol::ChannelType::Input:
        return QLatin1String("input");
    default:
        return QString::number(static_cast<std::uint16_t>(value));
    }
}

std::optional<protocol::ChannelType> readChannelType(const QJsonObject& object)
{
    const QJsonValue value = object.value(QLatin1String("channelType"));
    if (value.isString())
        return channelTypeFromString(value.toString().toLower());

    if (!value.isDouble())
        return std::nullopt;

    switch (static_cast<std::uint16_t>(value.toDouble(-1.0))) {
    case static_cast<std::uint16_t>(protocol::ChannelType::Standard):
        return protocol::ChannelType::Standard;
    case static_cast<std::uint16_t>(protocol::ChannelType::Video):
        return protocol::ChannelType::Video;
    case static_cast<std::uint16_t>(protocol::ChannelType::Audio):
        return protocol::ChannelType::Audio;
    case static_cast<std::uint16_t>(protocol::ChannelType::Bulk):
        return protocol::ChannelType::Bulk;
    case static_cast<std::uint16_t>(protocol::ChannelType::Control):
        return protocol::ChannelType::Control;
    case static_cast<std::uint16_t>(protocol::ChannelType::Input):
        return protocol::ChannelType::Input;
    default:
        return std::nullopt;
    }
}

const network::ChannelSpec* findSpec(const std::vector<network::ChannelSpec>& specs,
                                     network::ChannelKey key)
{
    for (const network::ChannelSpec& spec : specs) {
        if (spec.key == key)
            return &spec;
    }
    return nullptr;
}

bool readChannelSpecAndEndpoint(const QJsonObject& object,
                                const std::vector<network::ChannelSpec>& knownSpecs,
                                network::ChannelSpec& spec,
                                std::string& endpoint,
                                network::ChannelReadyInfo& ready,
                                QtTransportProfileLoadResult& result)
{
    const std::optional<std::uint64_t> channelId = readUnsigned(object, "channelId");
    const std::optional<protocol::ChannelType> channelType = readChannelType(object);
    const QString endpointValue = object.value(QLatin1String("endpoint")).toString();
    if (!channelId.has_value() || !channelType.has_value() || endpointValue.isEmpty()) {
        appendFailure(result, "tcp channel requires channelId, channelType, and endpoint");
        return false;
    }

    network::ChannelKey key{static_cast<protocol::ChannelId>(channelId.value()),
                            channelType.value()};
    const network::ChannelSpec* matchedSpec = findSpec(knownSpecs, key);
    if (matchedSpec == nullptr) {
        appendFailure(result, "tcp channel does not match a known channel spec");
        return false;
    }

    spec = *matchedSpec;
    endpoint = endpointValue.toStdString();
    ready.endpoint = object.value(QLatin1String("readyEndpoint")).toString().toStdString();
    return true;
}

bool parseTcpChannel(const QJsonObject& object,
                     const std::vector<network::ChannelSpec>& knownSpecs,
                     QtTcpChannelProfile& profile,
                     QtTransportProfileLoadResult& result)
{
    return readChannelSpecAndEndpoint(object,
                                      knownSpecs,
                                      profile.spec,
                                      profile.endpoint,
                                      profile.ready,
                                      result);
}

bool parseTcpListenChannel(const QJsonObject& object,
                           const std::vector<network::ChannelSpec>& knownSpecs,
                           QtTcpListenChannelProfile& profile,
                           QtTransportProfileLoadResult& result)
{
    return readChannelSpecAndEndpoint(object,
                                      knownSpecs,
                                      profile.spec,
                                      profile.endpoint,
                                      profile.ready,
                                      result);
}

bool parseEndpoint(const std::string& endpoint, QHostAddress& address, quint16& port)
{
    std::string value = endpoint;
    const std::string prefix = "tcp://";
    if (value.rfind(prefix, 0) == 0)
        value = value.substr(prefix.size());

    const std::size_t delimiter = value.rfind(':');
    if (delimiter == std::string::npos || delimiter == 0 || delimiter + 1 >= value.size())
        return false;

    bool ok = false;
    const int parsedPort = QString::fromStdString(value.substr(delimiter + 1)).toInt(&ok);
    if (!ok || parsedPort < 0 || parsedPort > 65535)
        return false;

    address = QHostAddress(QString::fromStdString(value.substr(0, delimiter)));
    if (address.isNull())
        return false;

    port = static_cast<quint16>(parsedPort);
    return true;
}

QJsonObject tcpChannelToJson(const network::ChannelSpec& spec,
                             const std::string& endpoint,
                             const network::ChannelReadyInfo& ready)
{
    QJsonObject object;
    object.insert(QLatin1String("channelId"), static_cast<int>(spec.key.channelId));
    object.insert(QLatin1String("channelType"), channelTypeToString(spec.key.channelType));
    object.insert(QLatin1String("endpoint"), QString::fromStdString(endpoint));
    if (!ready.endpoint.empty())
        object.insert(QLatin1String("readyEndpoint"), QString::fromStdString(ready.endpoint));
    return object;
}

QJsonObject sessionProfileToJson(const QtSessionTcpTransportProfile& profile)
{
    QJsonObject object;
    if (profile.sessionId != 0)
        object.insert(QLatin1String("sessionId"), static_cast<double>(profile.sessionId));

    QJsonArray tcpChannels;
    for (const QtTcpChannelProfile& channel : profile.tcpChannels)
        tcpChannels.append(tcpChannelToJson(channel.spec, channel.endpoint, channel.ready));
    if (!tcpChannels.isEmpty())
        object.insert(QLatin1String("tcpChannels"), tcpChannels);

    QJsonArray tcpListenChannels;
    for (const QtTcpListenChannelProfile& channel : profile.tcpListenChannels)
        tcpListenChannels.append(tcpChannelToJson(channel.spec, channel.endpoint, channel.ready));
    if (!tcpListenChannels.isEmpty())
        object.insert(QLatin1String("tcpListenChannels"), tcpListenChannels);

    return object;
}

QtTransportProfileLoadResult loadTcpTransportProfilesFromJsonFileInternal(
    const std::string& path,
    const std::vector<network::ChannelSpec>& knownSpecs,
    bool requireSessionId)
{
    QtTransportProfileLoadResult result;
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        appendFailure(result, "qt transport profile file cannot be opened");
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        appendFailure(result, "qt transport profile file is not valid json");
        return result;
    }

    const QJsonArray sessions = document.object().value(QLatin1String("sessions")).toArray();
    if (sessions.isEmpty()) {
        appendFailure(result, "qt transport profile requires at least one session");
        return result;
    }

    for (const QJsonValue& sessionValue : sessions) {
        if (!sessionValue.isObject()) {
            appendFailure(result, "qt transport profile session must be an object");
            continue;
        }

        const QJsonObject sessionObject = sessionValue.toObject();
        const std::optional<std::uint64_t> sessionId = readUnsigned(sessionObject, "sessionId");
        if (!sessionId.has_value() && requireSessionId) {
            appendFailure(result, "qt transport profile session requires sessionId");
            continue;
        }

        QtSessionTcpTransportProfile profile;
        profile.sessionId = sessionId.has_value()
            ? static_cast<protocol::SessionId>(sessionId.value())
            : 0;

        const QJsonArray tcpChannels = sessionObject.value(QLatin1String("tcpChannels")).toArray();
        for (const QJsonValue& channelValue : tcpChannels) {
            if (!channelValue.isObject()) {
                appendFailure(result, "qt transport profile tcp channel must be an object");
                continue;
            }

            QtTcpChannelProfile channel;
            if (parseTcpChannel(channelValue.toObject(), knownSpecs, channel, result))
                profile.tcpChannels.push_back(std::move(channel));
        }

        const QJsonArray tcpListenChannels =
            sessionObject.value(QLatin1String("tcpListenChannels")).toArray();
        for (const QJsonValue& channelValue : tcpListenChannels) {
            if (!channelValue.isObject()) {
                appendFailure(result, "qt transport profile tcp listen channel must be an object");
                continue;
            }

            QtTcpListenChannelProfile channel;
            if (parseTcpListenChannel(channelValue.toObject(), knownSpecs, channel, result))
                profile.tcpListenChannels.push_back(std::move(channel));
        }

        if (profile.tcpChannels.empty() && profile.tcpListenChannels.empty()) {
            appendFailure(result, "qt transport profile session has no valid tcp channels");
            continue;
        }

        result.profiles.push_back(std::move(profile));
    }

    result.ok = result.messages.empty() && !result.profiles.empty();
    return result;
}

} // namespace

QtTransportProfileLoadResult loadTcpTransportProfilesFromJsonFile(
    const std::string& path,
    const std::vector<network::ChannelSpec>& knownSpecs)
{
    return loadTcpTransportProfilesFromJsonFileInternal(path, knownSpecs, true);
}

QtReconnectOrchestrationRequestLoadResult
loadReconnectOrchestrationRequestFromJsonFileForSession(
    protocol::SessionId clientSessionId,
    protocol::SessionId agentSessionId,
    const std::string& path,
    const std::vector<network::ChannelSpec>& knownSpecs,
    const std::string& reason,
    bool requestDisplayKeyframe)
{
    QtReconnectOrchestrationRequestLoadResult result;
    result.request.profile.connectionPlan.knownSpecs = knownSpecs;
    result.request.profile.clientSessionId = clientSessionId;
    result.request.profile.agentSessionId = agentSessionId;
    if (!reason.empty())
        result.request.reason = reason;
    result.request.requestDisplayKeyframe = requestDisplayKeyframe;

    const QtTransportProfileLoadResult loaded =
        loadTcpTransportProfilesFromJsonFileInternal(path, knownSpecs, false);
    if (!loaded.ok) {
        result.messages.insert(result.messages.end(),
                               loaded.messages.begin(),
                               loaded.messages.end());
        return result;
    }

    std::set<network::ChannelKey> degraded;
    bool attempted = false;
    for (const QtSessionTcpTransportProfile& profile : loaded.profiles) {
        for (const QtTcpChannelProfile& channel : profile.tcpChannels) {
            attempted = true;

            connection::PeerConnectionChannelRequest requested;
            requested.key = channel.spec.key;
            requested.endpoint = channel.endpoint;
            requested.clientReadyEndpoint = channel.ready.endpoint;
            result.request.profile.connectionPlan.channels.push_back(
                std::move(requested));

            if (degraded.insert(channel.spec.key).second)
                result.request.degradedChannels.push_back(channel.spec.key);
        }
    }

    if (!attempted) {
        appendFailure(result, "qt reconnect profile has no tcp channels");
        return result;
    }

    result.ok = true;
    return result;
}

QtTcpPeerProfilePair makeTcpPeerProfilePair(
    const std::vector<network::ChannelSpec>& specs,
    const std::string& endpoint,
    const std::string& clientReadyEndpoint,
    const std::string& agentReadyEndpoint,
    protocol::SessionId clientSessionId,
    protocol::SessionId agentSessionId)
{
    QtTcpPeerProfilePair pair;
    pair.clientProfile.sessionId = clientSessionId;
    pair.agentProfile.sessionId = agentSessionId;

    for (const network::ChannelSpec& spec : specs) {
        QtTcpChannelProfile clientChannel;
        clientChannel.spec = spec;
        clientChannel.endpoint = endpoint;
        clientChannel.ready.endpoint = clientReadyEndpoint;
        pair.clientProfile.tcpChannels.push_back(std::move(clientChannel));

        QtTcpListenChannelProfile agentChannel;
        agentChannel.spec = spec;
        agentChannel.endpoint = endpoint;
        agentChannel.ready.endpoint = agentReadyEndpoint;
        pair.agentProfile.tcpListenChannels.push_back(std::move(agentChannel));
    }

    return pair;
}

std::string tcpTransportProfilesToJson(
    const std::vector<QtSessionTcpTransportProfile>& profiles)
{
    QJsonArray sessions;
    for (const QtSessionTcpTransportProfile& profile : profiles)
        sessions.append(sessionProfileToJson(profile));

    QJsonObject root;
    root.insert(QLatin1String("sessions"), sessions);
    return QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString();
}

QtTransportProfileSaveResult saveTcpTransportProfilesToJsonFile(
    const std::string& path,
    const std::vector<QtSessionTcpTransportProfile>& profiles)
{
    QtTransportProfileSaveResult result;
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.message = "qt transport profile file cannot be opened for writing";
        return result;
    }

    const std::string json = tcpTransportProfilesToJson(profiles);
    const qint64 written = file.write(json.data(), static_cast<qint64>(json.size()));
    if (written != static_cast<qint64>(json.size())) {
        result.message = "qt transport profile file write was incomplete";
        return result;
    }
    if (!file.flush()) {
        result.message = "qt transport profile file flush failed";
        return result;
    }

    result.ok = true;
    return result;
}

QtSessionTransportConnector::QtSessionTransportConnector(network::NetworkManager& manager)
    : manager_(manager),
      binder_(manager)
{
}

QtTransportConnectResult QtSessionTransportConnector::connectTcpChannels(
    const std::vector<QtTcpChannelProfile>& profiles)
{
    QtTransportConnectResult result;
    result.ok = true;

    for (const QtTcpChannelProfile& profile : profiles) {
        auto transport = std::make_shared<adapters::qt::QtTcpTransportSocket>(profile.spec.socketClass);
        network::SocketOpenOptions options;
        options.socketClass = profile.spec.socketClass;
        options.endpoint = profile.endpoint;
        if (!transport->open(options)) {
            appendFailure(result, "qt tcp transport open failed for " + profile.endpoint);
            continue;
        }

        QtTransportConnectResult bound = bindTransport(profile.spec, transport, profile.ready);
        if (!bound.ok) {
            result.ok = false;
            result.messages.insert(result.messages.end(), bound.messages.begin(), bound.messages.end());
            continue;
        }

        result.readyChannels.insert(result.readyChannels.end(),
                                    bound.readyChannels.begin(),
                                    bound.readyChannels.end());
    }

    return result;
}

QtTransportConnectResult QtSessionTransportConnector::adoptTcpChannel(
    const network::ChannelSpec& spec,
    QTcpSocket* socket,
    const network::ChannelReadyInfo& ready)
{
    QtTransportConnectResult result;
    if (socket == nullptr) {
        appendFailure(result, "accepted qt tcp socket is required");
        return result;
    }

    std::shared_ptr<adapters::qt::QtTcpTransportSocket> transport =
        adapters::qt::QtTcpTransportSocket::adopt(spec.socketClass, socket);
    return bindTransport(spec, std::move(transport), ready);
}

QtReconnectReplacementResult QtSessionTransportConnector::prepareTcpChannelReplacements(
    const std::vector<QtTcpChannelProfile>& profiles)
{
    QtReconnectReplacementResult result;
    result.ok = true;

    for (const QtTcpChannelProfile& profile : profiles) {
        auto transport = std::make_shared<adapters::qt::QtTcpTransportSocket>(profile.spec.socketClass);
        network::SocketOpenOptions options;
        options.socketClass = profile.spec.socketClass;
        options.endpoint = profile.endpoint;
        if (!transport->open(options)) {
            appendFailure(result, "qt tcp reconnect transport open failed for " + profile.endpoint);
            continue;
        }

        QtReconnectReplacementResult prepared =
            prepareReplacement(profile.spec, std::move(transport), profile.ready);
        if (!prepared.ok)
            result.ok = false;
        result.preparedChannels.insert(result.preparedChannels.end(),
                                       prepared.preparedChannels.begin(),
                                       prepared.preparedChannels.end());
        result.replacementChannels.insert(result.replacementChannels.end(),
                                          prepared.replacementChannels.begin(),
                                          prepared.replacementChannels.end());
        result.messages.insert(result.messages.end(),
                               prepared.messages.begin(),
                               prepared.messages.end());
    }

    return result;
}

QtReconnectReplacementResult QtSessionTransportConnector::prepareAdoptedTcpChannelReplacement(
    const network::ChannelSpec& spec,
    QTcpSocket* socket,
    const network::ChannelReadyInfo& ready)
{
    QtReconnectReplacementResult result;
    if (socket == nullptr) {
        appendFailure(result, "accepted qt tcp socket is required for reconnect replacement");
        return result;
    }

    std::shared_ptr<adapters::qt::QtTcpTransportSocket> transport =
        adapters::qt::QtTcpTransportSocket::adopt(spec.socketClass, socket);
    return prepareReplacement(spec, std::move(transport), ready);
}

std::size_t QtSessionTransportConnector::transportCount() const
{
    return transports_.size();
}

std::vector<std::shared_ptr<adapters::qt::QtTcpTransportSocket>>
QtSessionTransportConnector::transports() const
{
    return transports_;
}

const adapters::qt::QtChannelBinder& QtSessionTransportConnector::binder() const
{
    return binder_;
}

void QtSessionTransportConnector::commitReconnectedChannels(
    const std::vector<network::ChannelKey>& keys)
{
    for (network::ChannelKey key : keys) {
        auto pending = pendingReconnectTransports_.find(key);
        if (pending == pendingReconnectTransports_.end())
            continue;

        replaceActiveTransport(key, pending->second);
        pendingReconnectTransports_.erase(pending);
    }
}

connection::ReconnectTeardownCloseResult QtSessionTransportConnector::closeOldTransport(
    const connection::ReconnectTeardownCloseRequest& request)
{
    if (request.targetChannel.channelId == 0) {
        return connection::ReconnectTeardownCloseResult::failed(
            protocol::ResponseStatus::InvalidArgument,
            "qt reconnect teardown target channel is required");
    }

    const network::ChannelSnapshot channel =
        manager_.registry().snapshot(request.targetChannel);
    if (channel.spec.key.channelId == 0) {
        return connection::ReconnectTeardownCloseResult::failed(
            protocol::ResponseStatus::ChannelUnavailable,
            "qt reconnect teardown target channel is not registered");
    }

    const auto pending = pendingReconnectTransports_.find(request.targetChannel);
    if (pending != pendingReconnectTransports_.end()) {
        return connection::ReconnectTeardownCloseResult::failed(
            protocol::ResponseStatus::Conflict,
            "qt reconnect replacement is still pending");
    }

    auto retired = retiredTransports_.find(request.targetChannel);
    if (retired != retiredTransports_.end()) {
        std::shared_ptr<adapters::qt::QtTcpTransportSocket> transport =
            retired->second.lock();
        if (transport) {
            if (transport->state() != network::SocketState::Closed) {
                const std::string reason = request.reason.empty()
                    ? std::string("qt reconnect teardown close old transport")
                    : request.reason;
                transport->close({reason});
            }
            removeTransport(transport);
        }
        retiredTransports_.erase(retired);
        return connection::ReconnectTeardownCloseResult::closed(
            "qt reconnect old transport closed");
    }

    return connection::ReconnectTeardownCloseResult::closed(
        "qt reconnect old transport already absent");
}

QtTransportConnectResult QtSessionTransportConnector::bindTransport(
    const network::ChannelSpec& spec,
    std::shared_ptr<adapters::qt::QtTcpTransportSocket> transport,
    const network::ChannelReadyInfo& ready)
{
    QtTransportConnectResult result;
    result.ok = true;

    adapters::qt::QtChannelBindOptions bindOptions;
    bindOptions.spec = spec;
    bindOptions.transport = transport;
    bindOptions.ready = ready;

    adapters::qt::QtChannelBindResult bind = binder_.bindChannel(bindOptions);
    if (!bind.ok) {
        appendFailure(result, bind.message);
        return result;
    }

    replaceActiveTransport(spec.key, transport);
    transports_.push_back(std::move(transport));
    result.readyChannels.push_back(bind.key);
    return result;
}

QtReconnectReplacementResult QtSessionTransportConnector::prepareReplacement(
    const network::ChannelSpec& spec,
    std::shared_ptr<adapters::qt::QtTcpTransportSocket> transport,
    const network::ChannelReadyInfo& ready)
{
    QtReconnectReplacementResult result;
    result.ok = true;

    if (!transport) {
        appendFailure(result, "qt reconnect transport is required");
        return result;
    }

    if (transport->state() != network::SocketState::Open) {
        appendFailure(result, "qt reconnect transport is not open");
        return result;
    }

    auto channel = std::make_shared<adapters::qt::QtPacketChannel>(
        spec.key,
        transport,
        &manager_.router());

    session::ReconnectChannelReplacement replacement;
    replacement.channel = std::move(channel);
    replacement.ready = ready;

    auto existingPending = pendingReconnectTransports_.find(spec.key);
    if (existingPending != pendingReconnectTransports_.end() &&
        existingPending->second != transport) {
        existingPending->second->close({"qt reconnect replacement superseded"});
        removeTransport(existingPending->second);
    }
    pendingReconnectTransports_[spec.key] = transport;
    transports_.push_back(std::move(transport));
    result.preparedChannels.push_back(spec.key);
    result.replacementChannels.push_back(std::move(replacement));
    return result;
}

void QtSessionTransportConnector::replaceActiveTransport(
    network::ChannelKey key,
    const std::shared_ptr<adapters::qt::QtTcpTransportSocket>& transport)
{
    auto existing = activeTransports_.find(key);
    if (existing != activeTransports_.end() && existing->second != transport) {
        existing->second->close({"qt reconnect replaced channel"});
        removeTransport(existing->second);
        retiredTransports_[key] = existing->second;
    }

    activeTransports_[key] = transport;
}

void QtSessionTransportConnector::removeTransport(
    const std::shared_ptr<adapters::qt::QtTcpTransportSocket>& transport)
{
    transports_.erase(std::remove(transports_.begin(), transports_.end(), transport),
                      transports_.end());
}

QtRuntimeTransportManager::QtRuntimeTransportManager(RuntimeHost& host)
    : host_(host)
{
}

struct QtRuntimeTransportManager::Listener
{
    protocol::SessionId sessionId = 0;
    network::ChannelSpec spec;
    network::ChannelReadyInfo ready;
    bool reconnectOnAccept = false;
    std::string reconnectReason;
    bool requestDisplayKeyframe = true;
    std::unique_ptr<QTcpServer> server;
};

QtRuntimeTransportManager::~QtRuntimeTransportManager() = default;

QtTransportConnectResult QtRuntimeTransportManager::applyProfile(
    const QtSessionTcpTransportProfile& profile)
{
    return connectTcpChannels(profile.sessionId, profile.tcpChannels);
}

QtTransportConnectResult QtRuntimeTransportManager::applyProfilesFromJsonFile(
    const std::string& path,
    const std::vector<network::ChannelSpec>& knownSpecs)
{
    QtTransportConnectResult result;
    result.ok = true;

    const QtTransportProfileLoadResult loaded =
        loadTcpTransportProfilesFromJsonFile(path, knownSpecs);
    if (!loaded.ok) {
        result.ok = false;
        result.messages.insert(result.messages.end(), loaded.messages.begin(), loaded.messages.end());
        return result;
    }

    for (const QtSessionTcpTransportProfile& profile : loaded.profiles) {
        if (profile.tcpChannels.empty())
            continue;

        QtTransportConnectResult applied = applyProfile(profile);
        if (!applied.ok)
            result.ok = false;
        result.readyChannels.insert(result.readyChannels.end(),
                                    applied.readyChannels.begin(),
                                    applied.readyChannels.end());
        result.listeningChannels.insert(result.listeningChannels.end(),
                                        applied.listeningChannels.begin(),
                                        applied.listeningChannels.end());
        result.messages.insert(result.messages.end(), applied.messages.begin(), applied.messages.end());
    }

    return result;
}

QtTransportConnectResult QtRuntimeTransportManager::applyProfilesFromJsonFileForSession(
    protocol::SessionId sessionId,
    const std::string& path,
    const std::vector<network::ChannelSpec>& knownSpecs)
{
    QtTransportConnectResult result;
    result.ok = true;

    const QtTransportProfileLoadResult loaded =
        loadTcpTransportProfilesFromJsonFileInternal(path, knownSpecs, false);
    if (!loaded.ok) {
        result.ok = false;
        result.messages.insert(result.messages.end(), loaded.messages.begin(), loaded.messages.end());
        return result;
    }

    for (QtSessionTcpTransportProfile profile : loaded.profiles) {
        if (profile.tcpChannels.empty())
            continue;

        profile.sessionId = sessionId;
        QtTransportConnectResult applied = applyProfile(profile);
        if (!applied.ok)
            result.ok = false;
        result.readyChannels.insert(result.readyChannels.end(),
                                    applied.readyChannels.begin(),
                                    applied.readyChannels.end());
        result.listeningChannels.insert(result.listeningChannels.end(),
                                        applied.listeningChannels.begin(),
                                        applied.listeningChannels.end());
        result.messages.insert(result.messages.end(), applied.messages.begin(), applied.messages.end());
    }

    return result;
}

QtTransportConnectResult QtRuntimeTransportManager::applyListenProfilesFromJsonFile(
    const std::string& path,
    const std::vector<network::ChannelSpec>& knownSpecs)
{
    QtTransportConnectResult result;
    result.ok = true;

    const QtTransportProfileLoadResult loaded =
        loadTcpTransportProfilesFromJsonFile(path, knownSpecs);
    if (!loaded.ok) {
        result.ok = false;
        result.messages.insert(result.messages.end(), loaded.messages.begin(), loaded.messages.end());
        return result;
    }

    for (const QtSessionTcpTransportProfile& profile : loaded.profiles) {
        if (profile.tcpListenChannels.empty())
            continue;

        QtTransportConnectResult applied =
            listenTcpChannels(profile.sessionId, profile.tcpListenChannels);
        if (!applied.ok)
            result.ok = false;
        result.readyChannels.insert(result.readyChannels.end(),
                                    applied.readyChannels.begin(),
                                    applied.readyChannels.end());
        result.listeningChannels.insert(result.listeningChannels.end(),
                                        applied.listeningChannels.begin(),
                                        applied.listeningChannels.end());
        result.messages.insert(result.messages.end(), applied.messages.begin(), applied.messages.end());
    }

    return result;
}

QtTransportConnectResult QtRuntimeTransportManager::applyListenProfilesFromJsonFileForSession(
    protocol::SessionId sessionId,
    const std::string& path,
    const std::vector<network::ChannelSpec>& knownSpecs)
{
    QtTransportConnectResult result;
    result.ok = true;

    const QtTransportProfileLoadResult loaded =
        loadTcpTransportProfilesFromJsonFileInternal(path, knownSpecs, false);
    if (!loaded.ok) {
        result.ok = false;
        result.messages.insert(result.messages.end(), loaded.messages.begin(), loaded.messages.end());
        return result;
    }

    for (QtSessionTcpTransportProfile profile : loaded.profiles) {
        if (profile.tcpListenChannels.empty())
            continue;

        QtTransportConnectResult applied =
            listenTcpChannels(sessionId, profile.tcpListenChannels);
        if (!applied.ok)
            result.ok = false;
        result.readyChannels.insert(result.readyChannels.end(),
                                    applied.readyChannels.begin(),
                                    applied.readyChannels.end());
        result.listeningChannels.insert(result.listeningChannels.end(),
                                        applied.listeningChannels.begin(),
                                        applied.listeningChannels.end());
        result.messages.insert(result.messages.end(), applied.messages.begin(), applied.messages.end());
    }

    return result;
}

QtReconnectResult QtRuntimeTransportManager::reconnectProfilesFromJsonFileForSession(
    protocol::SessionId sessionId,
    const std::string& path,
    const std::vector<network::ChannelSpec>& knownSpecs,
    const std::string& reason,
    bool requestDisplayKeyframe)
{
    QtReconnectResult result;
    result.ok = true;
    result.prepared.ok = true;

    const QtTransportProfileLoadResult loaded =
        loadTcpTransportProfilesFromJsonFileInternal(path, knownSpecs, false);
    if (!loaded.ok) {
        result.ok = false;
        result.prepared.ok = false;
        result.messages.insert(result.messages.end(), loaded.messages.begin(), loaded.messages.end());
        result.prepared.messages.insert(result.prepared.messages.end(),
                                        loaded.messages.begin(),
                                        loaded.messages.end());
        return result;
    }

    bool attempted = false;
    for (const QtSessionTcpTransportProfile& profile : loaded.profiles) {
        if (profile.tcpChannels.empty())
            continue;

        attempted = true;
        QtReconnectResult reconnected =
            reconnectTcpChannels(sessionId, profile.tcpChannels, reason, requestDisplayKeyframe);
        if (!reconnected.ok)
            result.ok = false;
        if (!reconnected.prepared.ok)
            result.prepared.ok = false;

        result.prepared.preparedChannels.insert(result.prepared.preparedChannels.end(),
                                                reconnected.prepared.preparedChannels.begin(),
                                                reconnected.prepared.preparedChannels.end());
        result.prepared.replacementChannels.insert(result.prepared.replacementChannels.end(),
                                                   reconnected.prepared.replacementChannels.begin(),
                                                   reconnected.prepared.replacementChannels.end());
        result.prepared.messages.insert(result.prepared.messages.end(),
                                        reconnected.prepared.messages.begin(),
                                        reconnected.prepared.messages.end());
        result.messages.insert(result.messages.end(),
                               reconnected.messages.begin(),
                               reconnected.messages.end());
        result.report = reconnected.report;
    }

    if (!attempted)
        appendFailure(result, "qt reconnect profile has no tcp channels");

    result.prepared.ok = result.prepared.ok && result.ok;
    return result;
}

QtTransportConnectResult QtRuntimeTransportManager::connectTcpChannels(
    protocol::SessionId sessionId,
    const std::vector<QtTcpChannelProfile>& profiles)
{
    QtTransportConnectResult result;
    QtSessionTransportConnector* target = connectorFor(sessionId, result);
    if (target == nullptr)
        return result;

    return target->connectTcpChannels(profiles);
}

QtTransportConnectResult QtRuntimeTransportManager::listenTcpChannels(
    protocol::SessionId sessionId,
    const std::vector<QtTcpListenChannelProfile>& profiles)
{
    return listenTcpChannelsInternal(sessionId, profiles, false, {}, true);
}

QtTransportConnectResult QtRuntimeTransportManager::listenReconnectTcpChannels(
    protocol::SessionId sessionId,
    const std::vector<QtTcpListenChannelProfile>& profiles,
    const std::string& reason,
    bool requestDisplayKeyframe)
{
    return listenTcpChannelsInternal(sessionId,
                                     profiles,
                                     true,
                                     reason,
                                     requestDisplayKeyframe);
}

QtTransportConnectResult QtRuntimeTransportManager::listenTcpChannelsInternal(
    protocol::SessionId sessionId,
    const std::vector<QtTcpListenChannelProfile>& profiles,
    bool reconnectOnAccept,
    const std::string& reason,
    bool requestDisplayKeyframe)
{
    QtTransportConnectResult result;
    result.ok = true;

    QtTransportConnectResult connectorResult;
    if (connectorFor(sessionId, connectorResult) == nullptr) {
        result.ok = false;
        result.messages.insert(result.messages.end(),
                               connectorResult.messages.begin(),
                               connectorResult.messages.end());
        return result;
    }

    for (const QtTcpListenChannelProfile& profile : profiles) {
        QHostAddress address;
        quint16 port = 0;
        if (!parseEndpoint(profile.endpoint, address, port)) {
            appendFailure(result, "qt tcp listen endpoint is invalid");
            continue;
        }

        auto listener = std::make_unique<Listener>();
        listener->sessionId = sessionId;
        listener->spec = profile.spec;
        listener->ready = profile.ready;
        listener->reconnectOnAccept = reconnectOnAccept;
        listener->reconnectReason = reason;
        listener->requestDisplayKeyframe = requestDisplayKeyframe;
        listener->server = std::make_unique<QTcpServer>();
        if (!listener->server->listen(address, port)) {
            appendFailure(result, "qt tcp listen failed for " + profile.endpoint);
            continue;
        }

        Listener* rawListener = listener.get();
        QObject::connect(rawListener->server.get(),
                         &QTcpServer::newConnection,
                         rawListener->server.get(),
                         [this, rawListener]() {
                             while (rawListener->server->hasPendingConnections()) {
                                 acceptListenerTcpChannel(
                                     rawListener->sessionId,
                                     rawListener->spec,
                                     rawListener->server->nextPendingConnection(),
                                     rawListener->ready,
                                     rawListener->reconnectOnAccept
                                         ? rawListener->reconnectReason
                                         : std::string{},
                                     rawListener->requestDisplayKeyframe);
                             }
                         });

        result.listeningChannels.push_back(profile.spec.key);
        listeners_.push_back(std::move(listener));
    }

    return result;
}

QtTransportConnectResult QtRuntimeTransportManager::adoptTcpChannel(
    protocol::SessionId sessionId,
    const network::ChannelSpec& spec,
    QTcpSocket* socket,
    const network::ChannelReadyInfo& ready)
{
    QtTransportConnectResult result;
    QtSessionTransportConnector* target = connectorFor(sessionId, result);
    if (target == nullptr)
        return result;

    return target->adoptTcpChannel(spec, socket, ready);
}

QtReconnectReplacementResult QtRuntimeTransportManager::prepareTcpChannelReplacements(
    protocol::SessionId sessionId,
    const std::vector<QtTcpChannelProfile>& profiles)
{
    QtReconnectReplacementResult result;
    QtTransportConnectResult connectorResult;
    QtSessionTransportConnector* target = connectorFor(sessionId, connectorResult);
    if (target == nullptr) {
        result.ok = false;
        result.messages.insert(result.messages.end(),
                               connectorResult.messages.begin(),
                               connectorResult.messages.end());
        return result;
    }

    return target->prepareTcpChannelReplacements(profiles);
}

QtReconnectReplacementResult QtRuntimeTransportManager::prepareAdoptedTcpChannelReplacement(
    protocol::SessionId sessionId,
    const network::ChannelSpec& spec,
    QTcpSocket* socket,
    const network::ChannelReadyInfo& ready)
{
    QtReconnectReplacementResult result;
    QtTransportConnectResult connectorResult;
    QtSessionTransportConnector* target = connectorFor(sessionId, connectorResult);
    if (target == nullptr) {
        result.ok = false;
        result.messages.insert(result.messages.end(),
                               connectorResult.messages.begin(),
                               connectorResult.messages.end());
        return result;
    }

    return target->prepareAdoptedTcpChannelReplacement(spec, socket, ready);
}

QtReconnectResult QtRuntimeTransportManager::reconnectTcpChannels(
    protocol::SessionId sessionId,
    const std::vector<QtTcpChannelProfile>& profiles,
    const std::string& reason,
    bool requestDisplayKeyframe)
{
    return reconnectPrepared(sessionId,
                             prepareTcpChannelReplacements(sessionId, profiles),
                             reason,
                             requestDisplayKeyframe);
}

QtReconnectResult QtRuntimeTransportManager::reconnectAdoptedTcpChannel(
    protocol::SessionId sessionId,
    const network::ChannelSpec& spec,
    QTcpSocket* socket,
    const network::ChannelReadyInfo& ready,
    const std::string& reason,
    bool requestDisplayKeyframe)
{
    return reconnectPrepared(sessionId,
                             prepareAdoptedTcpChannelReplacement(sessionId, spec, socket, ready),
                             reason,
                             requestDisplayKeyframe);
}

QtSessionTransportConnector* QtRuntimeTransportManager::connector(protocol::SessionId sessionId)
{
    auto it = connectors_.find(sessionId);
    if (it == connectors_.end())
        return nullptr;
    return it->second.get();
}

const QtSessionTransportConnector* QtRuntimeTransportManager::connector(
    protocol::SessionId sessionId) const
{
    auto it = connectors_.find(sessionId);
    if (it == connectors_.end())
        return nullptr;
    return it->second.get();
}

std::size_t QtRuntimeTransportManager::connectorCount() const
{
    return connectors_.size();
}

std::size_t QtRuntimeTransportManager::listenerCount() const
{
    return listeners_.size();
}

void QtRuntimeTransportManager::releaseSession(protocol::SessionId sessionId)
{
    connectors_.erase(sessionId);
    listeners_.erase(std::remove_if(listeners_.begin(),
                                    listeners_.end(),
                                    [sessionId](const std::unique_ptr<Listener>& listener) {
                                        return listener->sessionId == sessionId;
                                     }),
                     listeners_.end());
}

connection::ReconnectTeardownCloseResult QtRuntimeTransportManager::closeOldTransport(
    const connection::ReconnectTeardownCloseRequest& request)
{
    if (request.sessionId == 0) {
        return connection::ReconnectTeardownCloseResult::failed(
            protocol::ResponseStatus::InvalidArgument,
            "qt reconnect teardown session id is required");
    }

    QtSessionTransportConnector* targetConnector = connector(request.sessionId);
    if (targetConnector == nullptr) {
        return connection::ReconnectTeardownCloseResult::failed(
            protocol::ResponseStatus::NotFound,
            "qt reconnect teardown session connector not found");
    }

    return targetConnector->closeOldTransport(request);
}

QtSessionTransportConnector* QtRuntimeTransportManager::connectorFor(
    protocol::SessionId sessionId,
    QtTransportConnectResult& result)
{
    if (sessionId == 0) {
        appendFailure(result, "session id is required for qt transport profile");
        return nullptr;
    }

    auto existing = connectors_.find(sessionId);
    if (existing != connectors_.end())
        return existing->second.get();

    session::Session* session = host_.sessions().find(sessionId);
    if (session == nullptr) {
        appendFailure(result, "session not found for qt transport profile");
        return nullptr;
    }

    network::NetworkManager* network = session->network();
    if (network == nullptr) {
        appendFailure(result, "session network is missing for qt transport profile");
        return nullptr;
    }

    auto inserted = connectors_.emplace(
        sessionId,
        std::make_unique<QtSessionTransportConnector>(*network));
    return inserted.first->second.get();
}

void QtRuntimeTransportManager::acceptListenerTcpChannel(
    protocol::SessionId sessionId,
    const network::ChannelSpec& spec,
    QTcpSocket* socket,
    const network::ChannelReadyInfo& ready,
    const std::string& reconnectReason,
    bool requestDisplayKeyframe)
{
    session::Session* target = host_.sessions().find(sessionId);
    network::NetworkManager* network = target == nullptr ? nullptr : target->network();
    if (network == nullptr) {
        adoptTcpChannel(sessionId, spec, socket, ready);
        return;
    }

    const network::ChannelSnapshot snapshot = network->registry().snapshot(spec.key);
    if (snapshot.bound) {
        reconnectAdoptedTcpChannel(sessionId,
                                   spec,
                                   socket,
                                   ready,
                                   reconnectReason.empty()
                                       ? "qt listener accepted replacement channel"
                                       : reconnectReason,
                                   requestDisplayKeyframe);
        return;
    }

    adoptTcpChannel(sessionId, spec, socket, ready);
}

QtReconnectResult QtRuntimeTransportManager::reconnectPrepared(
    protocol::SessionId sessionId,
    QtReconnectReplacementResult prepared,
    const std::string& reason,
    bool requestDisplayKeyframe)
{
    QtReconnectResult result;
    result.prepared = std::move(prepared);
    result.ok = result.prepared.ok;
    result.messages.insert(result.messages.end(),
                           result.prepared.messages.begin(),
                           result.prepared.messages.end());

    if (!result.prepared.ok)
        return result;

    if (result.prepared.replacementChannels.empty()) {
        appendFailure(result, "qt reconnect requires at least one replacement channel");
        return result;
    }

    session::Session* target = host_.sessions().find(sessionId);
    if (target == nullptr) {
        appendFailure(result, "session not found for qt reconnect");
        return result;
    }

    session::ReconnectRequest request;
    request.reason = reason;
    request.degradedChannels = result.prepared.preparedChannels;
    request.replacementChannels = result.prepared.replacementChannels;
    request.requestDisplayKeyframe = requestDisplayKeyframe;

    result.ok = host_.sessions().reconnect(sessionId, request);
    result.report = target->lastReconnectReport();
    if (result.ok) {
        QtSessionTransportConnector* targetConnector = connector(sessionId);
        if (targetConnector != nullptr)
            targetConnector->commitReconnectedChannels(result.prepared.preparedChannels);
    } else {
        result.messages.push_back("session reconnect failed for qt replacement channels");
    }
    return result;
}

} // namespace qt
} // namespace runtime
} // namespace fusiondesk
