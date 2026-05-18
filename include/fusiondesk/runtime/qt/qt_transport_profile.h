#ifndef FUSIONDESK_RUNTIME_QT_QT_TRANSPORT_PROFILE_H
#define FUSIONDESK_RUNTIME_QT_QT_TRANSPORT_PROFILE_H

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/adapters/qt/qt_channel_binder.h"
#include "fusiondesk/core/protocol/types.h"
#include "fusiondesk/core/session/session.h"
#include "fusiondesk/runtime/connection/reconnect_orchestration_plan.h"
#include "fusiondesk/runtime/connection/reconnect_teardown_handler.h"

class QTcpSocket;

namespace fusiondesk {
namespace runtime {

class RuntimeHost;

namespace qt {

struct QtTcpChannelProfile
{
    network::ChannelSpec spec;
    std::string endpoint;
    network::ChannelReadyInfo ready;
};

struct QtTcpListenChannelProfile
{
    network::ChannelSpec spec;
    std::string endpoint;
    network::ChannelReadyInfo ready;
};

struct QtTransportConnectResult
{
    bool ok = false;
    std::vector<network::ChannelKey> readyChannels;
    std::vector<network::ChannelKey> listeningChannels;
    std::vector<std::string> messages;
};

struct QtReconnectReplacementResult
{
    bool ok = false;
    std::vector<network::ChannelKey> preparedChannels;
    std::vector<session::ReconnectChannelReplacement> replacementChannels;
    std::vector<std::string> messages;
};

struct QtReconnectResult
{
    bool ok = false;
    QtReconnectReplacementResult prepared;
    session::ReconnectReport report;
    std::vector<std::string> messages;
};

struct QtSessionTcpTransportProfile
{
    protocol::SessionId sessionId = 0;
    std::vector<QtTcpChannelProfile> tcpChannels;
    std::vector<QtTcpListenChannelProfile> tcpListenChannels;
};

struct QtTransportProfileLoadResult
{
    bool ok = false;
    std::vector<QtSessionTcpTransportProfile> profiles;
    std::vector<std::string> messages;
};

struct QtTransportProfileSaveResult
{
    bool ok = false;
    std::string message;
};

struct QtReconnectOrchestrationRequestLoadResult
{
    bool ok = false;
    connection::ReconnectOrchestrationRequest request;
    std::vector<std::string> messages;
};

struct QtTcpPeerProfilePair
{
    QtSessionTcpTransportProfile clientProfile;
    QtSessionTcpTransportProfile agentProfile;
};

QtTcpPeerProfilePair makeTcpPeerProfilePair(
    const std::vector<network::ChannelSpec>& specs,
    const std::string& endpoint,
    const std::string& clientReadyEndpoint = {},
    const std::string& agentReadyEndpoint = {},
    protocol::SessionId clientSessionId = 0,
    protocol::SessionId agentSessionId = 0);

std::string tcpTransportProfilesToJson(
    const std::vector<QtSessionTcpTransportProfile>& profiles);

QtTransportProfileSaveResult saveTcpTransportProfilesToJsonFile(
    const std::string& path,
    const std::vector<QtSessionTcpTransportProfile>& profiles);

QtTransportProfileLoadResult loadTcpTransportProfilesFromJsonFile(
    const std::string& path,
    const std::vector<network::ChannelSpec>& knownSpecs);

QtReconnectOrchestrationRequestLoadResult
loadReconnectOrchestrationRequestFromJsonFileForSession(
    protocol::SessionId clientSessionId,
    protocol::SessionId agentSessionId,
    const std::string& path,
    const std::vector<network::ChannelSpec>& knownSpecs,
    const std::string& reason = {},
    bool requestDisplayKeyframe = true);

class QtSessionTransportConnector
{
public:
    explicit QtSessionTransportConnector(network::NetworkManager& manager);

    QtTransportConnectResult connectTcpChannels(const std::vector<QtTcpChannelProfile>& profiles);
    QtTransportConnectResult adoptTcpChannel(const network::ChannelSpec& spec,
                                             QTcpSocket* socket,
                                             const network::ChannelReadyInfo& ready = {});
    QtReconnectReplacementResult prepareTcpChannelReplacements(
        const std::vector<QtTcpChannelProfile>& profiles);
    QtReconnectReplacementResult prepareAdoptedTcpChannelReplacement(
        const network::ChannelSpec& spec,
        QTcpSocket* socket,
        const network::ChannelReadyInfo& ready = {});

    std::size_t transportCount() const;
    std::vector<std::shared_ptr<adapters::qt::QtTcpTransportSocket>> transports() const;
    const adapters::qt::QtChannelBinder& binder() const;
    void commitReconnectedChannels(const std::vector<network::ChannelKey>& keys);
    connection::ReconnectTeardownCloseResult closeOldTransport(
        const connection::ReconnectTeardownCloseRequest& request);

private:
    QtTransportConnectResult bindTransport(const network::ChannelSpec& spec,
                                           std::shared_ptr<adapters::qt::QtTcpTransportSocket> transport,
                                           const network::ChannelReadyInfo& ready);
    QtReconnectReplacementResult prepareReplacement(
        const network::ChannelSpec& spec,
        std::shared_ptr<adapters::qt::QtTcpTransportSocket> transport,
        const network::ChannelReadyInfo& ready);
    void replaceActiveTransport(network::ChannelKey key,
                                const std::shared_ptr<adapters::qt::QtTcpTransportSocket>& transport);
    void removeTransport(const std::shared_ptr<adapters::qt::QtTcpTransportSocket>& transport);

private:
    network::NetworkManager& manager_;
    adapters::qt::QtChannelBinder binder_;
    std::vector<std::shared_ptr<adapters::qt::QtTcpTransportSocket>> transports_;
    std::map<network::ChannelKey, std::shared_ptr<adapters::qt::QtTcpTransportSocket>> activeTransports_;
    std::map<network::ChannelKey, std::shared_ptr<adapters::qt::QtTcpTransportSocket>> pendingReconnectTransports_;
    std::map<network::ChannelKey, std::weak_ptr<adapters::qt::QtTcpTransportSocket>> retiredTransports_;
};

class QtRuntimeTransportManager : public connection::IReconnectTeardownCloseTarget
{
public:
    explicit QtRuntimeTransportManager(RuntimeHost& host);
    ~QtRuntimeTransportManager();

    QtTransportConnectResult applyProfile(const QtSessionTcpTransportProfile& profile);
    QtTransportConnectResult applyProfilesFromJsonFile(
        const std::string& path,
        const std::vector<network::ChannelSpec>& knownSpecs);
    QtTransportConnectResult applyProfilesFromJsonFileForSession(
        protocol::SessionId sessionId,
        const std::string& path,
        const std::vector<network::ChannelSpec>& knownSpecs);
    QtTransportConnectResult applyListenProfilesFromJsonFile(
        const std::string& path,
        const std::vector<network::ChannelSpec>& knownSpecs);
    QtTransportConnectResult applyListenProfilesFromJsonFileForSession(
        protocol::SessionId sessionId,
        const std::string& path,
        const std::vector<network::ChannelSpec>& knownSpecs);
    QtReconnectResult reconnectProfilesFromJsonFileForSession(
        protocol::SessionId sessionId,
        const std::string& path,
        const std::vector<network::ChannelSpec>& knownSpecs,
        const std::string& reason = {},
        bool requestDisplayKeyframe = true);
    QtTransportConnectResult connectTcpChannels(protocol::SessionId sessionId,
                                                const std::vector<QtTcpChannelProfile>& profiles);
    QtTransportConnectResult listenTcpChannels(protocol::SessionId sessionId,
                                               const std::vector<QtTcpListenChannelProfile>& profiles);
    QtTransportConnectResult listenReconnectTcpChannels(
        protocol::SessionId sessionId,
        const std::vector<QtTcpListenChannelProfile>& profiles,
        const std::string& reason = {},
        bool requestDisplayKeyframe = true);
    QtTransportConnectResult adoptTcpChannel(protocol::SessionId sessionId,
                                             const network::ChannelSpec& spec,
                                             QTcpSocket* socket,
                                             const network::ChannelReadyInfo& ready = {});
    QtReconnectReplacementResult prepareTcpChannelReplacements(
        protocol::SessionId sessionId,
        const std::vector<QtTcpChannelProfile>& profiles);
    QtReconnectReplacementResult prepareAdoptedTcpChannelReplacement(
        protocol::SessionId sessionId,
        const network::ChannelSpec& spec,
        QTcpSocket* socket,
        const network::ChannelReadyInfo& ready = {});
    QtReconnectResult reconnectTcpChannels(
        protocol::SessionId sessionId,
        const std::vector<QtTcpChannelProfile>& profiles,
        const std::string& reason = {},
        bool requestDisplayKeyframe = true);
    QtReconnectResult reconnectAdoptedTcpChannel(
        protocol::SessionId sessionId,
        const network::ChannelSpec& spec,
        QTcpSocket* socket,
        const network::ChannelReadyInfo& ready = {},
        const std::string& reason = {},
        bool requestDisplayKeyframe = true);

    QtSessionTransportConnector* connector(protocol::SessionId sessionId);
    const QtSessionTransportConnector* connector(protocol::SessionId sessionId) const;
    std::size_t connectorCount() const;
    std::size_t listenerCount() const;
    void releaseSession(protocol::SessionId sessionId);
    connection::ReconnectTeardownCloseResult closeOldTransport(
        const connection::ReconnectTeardownCloseRequest& request) override;

private:
    QtSessionTransportConnector* connectorFor(protocol::SessionId sessionId,
                                              QtTransportConnectResult& result);
    QtTransportConnectResult listenTcpChannelsInternal(
        protocol::SessionId sessionId,
        const std::vector<QtTcpListenChannelProfile>& profiles,
        bool reconnectOnAccept,
        const std::string& reason,
        bool requestDisplayKeyframe);
    void acceptListenerTcpChannel(protocol::SessionId sessionId,
                                  const network::ChannelSpec& spec,
                                  QTcpSocket* socket,
                                  const network::ChannelReadyInfo& ready,
                                  const std::string& reconnectReason,
                                  bool requestDisplayKeyframe);
    QtReconnectResult reconnectPrepared(protocol::SessionId sessionId,
                                        QtReconnectReplacementResult prepared,
                                        const std::string& reason,
                                        bool requestDisplayKeyframe);

private:
    struct Listener;

private:
    RuntimeHost& host_;
    std::map<protocol::SessionId, std::unique_ptr<QtSessionTransportConnector>> connectors_;
    std::vector<std::unique_ptr<Listener>> listeners_;
};

} // namespace qt
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_QT_QT_TRANSPORT_PROFILE_H
