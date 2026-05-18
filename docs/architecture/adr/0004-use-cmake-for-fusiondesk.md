# ADR 0004: Use CMake For FusionDesk And Keep Legacy Builds Outside This Repo

## Status

Accepted

## Context

The legacy product used qmake and platform-specific build scripts. The new architecture needs independent CI gates, Android NDK integration, CMake targets, and staged rebuild without bringing old build scripts or old product source back into the active repository.

## Decision

Use CMake as the build system for `FusionDesk`.

Keep CMake as the only in-repository build system for the `FusionDesk` line. Legacy qmake builds may exist only in an external archive or separate legacy repository when old behavior comparison is required.

Transition model:

```text
FusionDesk CMake builds independent targets
CI builds and tests FusionDesk directly
old code remains outside this repository and is used only for reference or comparison
final product packaging must be FusionDesk-owned unless an explicit external legacy artifact is injected by release engineering
```

## Consequences

Positive:

```text
FusionDesk can add modern target boundaries
Android CMake integration is natural
core can be built without legacy scripts
old product builds cannot silently influence FusionDesk packaging or CI
```

Negative:

```text
old behavior comparison requires access to an external archive
dependency assumptions copied from legacy builds must be made explicit in FusionDesk
```

## Verification

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
no in-repository qmake/Source build path is required
```
