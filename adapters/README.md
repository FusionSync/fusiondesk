# Adapters

Adapters connect `FusionDesk` core contracts to concrete frameworks and external technologies.

Initial adapter groups:

```text
qt
transport
display
codec
```

Adapters are not a target path for wrapping old `Source` modules. Old code may be read for behavior and protocol reference, but new adapters should be implemented against the `FusionDesk` contracts.

Current Qt transport slice:

```text
fusiondesk_qt_adapters is optional and builds only when Qt Core/Network is found.
QtTcpTransportSocket lives under include/fusiondesk/adapters/qt and src/adapters/qt.
It implements ITransportSocket and keeps Qt out of core.
QtPacketChannel implements IChannel on top of QtTcpTransportSocket and PacketCodec.
QtChannelBinder registers Qt transports into SocketGroup, binds QtPacketChannel, and marks ChannelRegistry ready.
QtSessionTransportConnector under runtime/qt creates/adopts Qt TCP transports from profiles and drives QtChannelBinder.
fusiondesk_qt_tcp_transport_tests proves PacketCodec Request/Response bytes, NetworkRouter channel routing, and profile-driven connect/adopt over loopback Qt TCP.
```
