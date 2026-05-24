# Architecture Docs

This directory is the canonical architecture set for the `FusionDesk` rebuild.

The old staged refactor notes, TODO logs, and interim SessionBus documents were removed because they described the historical `Source/` migration path. The active direction is now a clean `FusionDesk` rebuild. The legacy `Source/` tree has been moved out of this repository; any old-product comparison must use an external archive or legacy repository snapshot, never an in-repo runtime dependency.

## Canonical Documents

0. `FUSIONDESK_PROJECT_INIT_METHOD.md`
   Project startup method: C4, ADR, stage gates, multi-agent review, and verification rules.

1. `PRODUCT_ROADMAP.md`
   Product roadmap for the enterprise remote desktop platform.

2. `FUSIONDESK_ARCHITECTURE.md`
   Target code architecture and dependency rules for the rebuild.

3. `GOAL_AUTOPILOT_PLAN.md`
   Goal-driven automatic execution plan for progressing through protocol, network, session, module, display, transport, Android, and tunnel stages.

4. `FUSIONDESK_IMPLEMENTATION_BASELINE.md`
   Current real implementation state, gaps, forbidden shortcuts, and baseline verification.

5. `SESSION_MODEL.md`
   Detailed session lifecycle, ownership, authorization, reconnect, and diagnostics contract.

6. `RUNTIME_HOST_AND_SESSION_MANAGER_DESIGN.md`
   RuntimeHost, SessionManager, session state machine, product profile, lifecycle, and diagnostics design.

7. `FUSIONDESK_MINIMAL_VERSION_MAINLINE.md`
   Minimum runnable product mainline: RuntimeHost, SessionManager, link/channel readiness, ProductProfile module loading, and ModuleHost startup.

8. `FUSIONDESK_MINIMAL_VERSION_RUNBOOK.md`
   Minimum version build, smoke, diagnostics, completion gate, and out-of-scope checklist.

9. `PLATFORM_TARGETS.md`
   PC full-platform and Android Qt Client/library target model.

10. `FUSIONDESK_DIRECTORY_LAYOUT.md`
   Canonical `FusionDesk` directory structure and ownership rules.

11. `ANDROID_CONTROLLER_LIBRARY.md`
   Android embeddable AAR controller API, JNI, lifecycle, threading, and surface contract.

12. `QT_BOUNDARY.md`
   Qt usage boundary and core purity rules.

13. `ABI_AND_TOOLCHAIN_MATRIX.md`
   OS, CPU architecture, toolchain, build target, and CI matrix.

14. `NETWORK_MODEL.md`
   Multi-socket, multi-channel network architecture with priority, queue, flow-control, ACK, and reconnect rules.

15. `PROTOCOL_MESSAGE_PATTERN.md`
   Unified protocol envelope, request/response, ACK, error, stream, timeout, and correlation rules.

16. `NETWORK_CHANNEL_REGISTRY_AND_SCHEDULER.md`
   Concrete ChannelRegistry, PriorityScheduler, PacketEnvelope v1, pressure, queue, and reconnect contracts.

17. `MODULE_AND_INTERFACE_BLUEPRINT.md`
   Large and small module map, forbidden responsibilities, and abstract interface catalog.

18. `DISPLAY_MODULE_DESIGN.md`
   First module design: server screen capture and client rendering.

19. `DISPLAY_SCREEN_PIPELINE_DESIGN.md`
   Mature reference analysis and target `display.screen` production design for capture, encode, queues, channel binding, module-owned payload compatibility, recovery, render, PC/Android surfaces, diagnostics, and adapters.

20. `C4_ARCHITECTURE_VIEWS.md`
   C4-style context, container, component, Android, and deployment views.

21. `FUSIONDESK_STAGE_GATES.md`
   Stage gates from pure core through display MVP, Android controller, tunnel, and release hardening.

22. `FUSIONDESK_REBUILD_PLAN.md`
   Step-by-step rebuild plan for implementing new `FusionDesk` modules while using old code only as reference.

23. `DELIVERY_AND_PACKAGING.md`
   Windows and Linux first delivery policy, with macOS optional.

24. `FUSIONDESK_TODO_TRACKER.md`
   Live architecture TODO tracker for the current `FusionDesk` rebuild.

25. `adr/`
   Architecture decision records. Use these for dependency, platform, protocol, public API, security, and build decisions.

26. `structurizr/fusiondesk.dsl`
   Optional diagram-as-code workspace for Structurizr-compatible C4 tooling.

27. `CLIPBOARD_REDIRECTION_FOUNDATION.md`
   Foundational consensus for rebuilding `clipboard.redirect` as a governed
   data-redirection module with lazy format offers, Request/Response content
   reads, stream rules, policy, audit, reconnect, and platform adapter
   boundaries.

28. `LINUX_CLIPBOARD_IMPLEMENTATION_PLAN.md`
   Linux clipboard endpoint plan based on the current clipboard module,
   channel model, and `fuse-promise` Promise filesystem integration.

29. `CLIPBOARD_FORMAT_MATRIX.md`
   Current clipboard canonical format registry, native OS format mapping,
   endpoint support matrix, and cross-OS byte conversion rules.

## Current Scope

The first `FusionDesk` implementation slice is intentionally narrow:

```text
network
module
policy
session
display.screen
pc full-platform targets
android client controller
android embeddable controller library
```

Display, audio, clipboard, filesystem, printer, camera, input, gamepad, peripheral, auth, and future tunnel/P2P capabilities will attach through those contracts instead of owning their own session wiring.

Module mainline invariant:

```text
Mainline module management may inspect module identity, module version, role,
platform, dependency version ranges, channel bindings, and policy-visible
features only. Module payload schema, operation compatibility, downgrade,
translation, and rejection are declared by the module and enforced by the
module-owned factory, codec, validator, and runtime logic.
```

## Current Reality

Target-state documents are not implementation proof. The current real code state is tracked in `FUSIONDESK_IMPLEMENTATION_BASELINE.md`; stage completion is tracked by `FUSIONDESK_STAGE_GATES.md`.
