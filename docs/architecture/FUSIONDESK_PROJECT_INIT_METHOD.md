# FusionDesk Project Initialization Method

This document defines how architecture work starts and stays controlled during the `FusionDesk` rebuild.

The goal is not to produce decorative diagrams. The goal is to keep product behavior, module boundaries, platform support, and implementation gates aligned while `FusionDesk` is rebuilt as new code.

The legacy `Source/` tree is used for reference only. It can answer what the product currently does, how old packets are shaped, and which platform edge cases exist. It must not become the target runtime implementation through wrapper classes.

## Method Stack

Use a small set of proven practices:

```text
C4 views
  describe system, containers, components, and selected code-level interfaces

ADR records
  capture important architecture decisions and tradeoffs

arc42-style checklist
  keep requirements, constraints, runtime views, deployment, risks, and quality goals visible

Architecture fitness gates
  convert design rules into build, test, and review checks

Multi-agent review
  use independent passes for code archaeology, platform/build review, Qt/C++ review, and design consistency
```

External references:

```text
C4 Model: https://c4model.com/
Structurizr DSL: https://docs.structurizr.com/dsl
arc42 template: https://github.com/arc42/arc42-template
ADR examples: https://github.com/joelparkerhenderson/architecture-decision-record
Qt agent skills: https://github.com/TheQtCompanyRnD/agent-skills
OpenAI skills: https://github.com/openai/skills
```

These references are process inputs only. The authoritative project rules are the files in `docs/architecture`.

## Architecture Work Products

The startup baseline consists of these artifact types:

```text
Architecture index
  docs/architecture/README.md

Execution method
  FUSIONDESK_PROJECT_INIT_METHOD.md

Current implementation baseline
  FUSIONDESK_IMPLEMENTATION_BASELINE.md

Target architecture and layout
  FUSIONDESK_ARCHITECTURE.md
  FUSIONDESK_DIRECTORY_LAYOUT.md

Runtime and session model
  SESSION_MODEL.md
  RUNTIME_HOST_AND_SESSION_MANAGER_DESIGN.md

Network model
  NETWORK_MODEL.md
  NETWORK_CHANNEL_REGISTRY_AND_SCHEDULER.md

Module and interface blueprint
  MODULE_AND_INTERFACE_BLUEPRINT.md

First product slice
  DISPLAY_MODULE_DESIGN.md

Platform and delivery
  PLATFORM_TARGETS.md
  QT_BOUNDARY.md
  ANDROID_CONTROLLER_LIBRARY.md
  ABI_AND_TOOLCHAIN_MATRIX.md
  DELIVERY_AND_PACKAGING.md

Stage gates
  FUSIONDESK_STAGE_GATES.md

Decision records
  adr/*.md

Optional diagram-as-code workspace
  structurizr/fusiondesk.dsl
```

## Design Order

Use this order before moving code:

```text
1. Define product target and stage gate.
2. Define module ownership and forbidden responsibilities.
3. Define protocol and packet envelope.
4. Define session lifecycle and ownership.
5. Define network channels, priority, queue, and reconnect behavior.
6. Define module manifest and policy contracts.
7. Define platform abstractions.
8. Define framework, transport, codec, and platform adapters for new code.
9. Build one feature slice only.
10. Add tests and CI checks before broad rebuild.
```

The first feature slice is `display.screen`, because it proves the hard path:

```text
Agent capture
  -> encoder or codec adapter
  -> video channel
  -> client decoder or codec adapter
  -> renderer
  -> ACK and keyframe recovery
```

## Multi-Agent Review Pattern

Use independent agents for parallel review when the task is broad:

```text
Agent A: legacy capability archaeology and behavior reference
Agent B: docs and FusionDesk consistency
Agent C: platform, build, and toolchain constraints
Agent D: Qt/C++ code review when implementation exists
Agent E: security, policy, and audit review for enterprise modules
```

Each agent must produce:

```text
scope
files inspected
confirmed facts
architecture risks
recommended contracts
open questions
```

The controller integrates the results into canonical docs. Agent output alone is not a canonical decision.

## Decision Policy

Use an ADR when a choice changes one of these:

```text
dependency direction
runtime ownership
wire protocol compatibility
module boundaries
platform target support
public API or ABI
security and audit behavior
build system and packaging
tunnel, relay, or NAT traversal strategy
```

Do not use ADRs for temporary TODOs, local cleanup, or uncontroversial implementation details.

## Fitness Gates

Every architecture rule needs at least one verification hook.

Examples:

```text
core must not include Qt
  -> rg check in CI

FusionDesk core must compile standalone
  -> cmake configure/build gate

network routing must use channel id + channel type + packet type
  -> NetworkRouter unit tests

policy must deny unsupported platform or role
  -> PolicyEngine unit tests

display MVP must recover after keyframe request
  -> integration test with fake channels

Android public API must not expose Qt or JNI implementation types
  -> source scan and API review
```

## Startup Recommendation

Recommended project start:

```text
Target: fusiondesk_core_display_mvp

Scope:
  pure C++ core
  RuntimeHost and SessionManager skeleton
  ChannelRegistry and PriorityScheduler skeleton
  display.screen agent/client modules
  fake transport tests
  new display platform/framework adapters only behind FusionDesk/adapters or FusionDesk/platform

Out of scope:
  full tunnel/P2P
  Android AAR production delivery
  clipboard/filesystem/printer rebuild
  codec rewrite
  UI rewrite
```

This keeps the first milestone small enough to finish while still proving the complete architecture loop.
