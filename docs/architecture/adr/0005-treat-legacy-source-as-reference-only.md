# ADR 0005: Keep Legacy Source Outside The FusionDesk Repository

## Status

Accepted

## Context

The project goal is to rebuild the architecture in `FusionDesk`, not to keep the legacy implementation alive behind wrapper classes.

The old `Source/` tree contains useful behavior, protocol, and platform knowledge, but it also contains the coupling the rebuild is meant to remove: UI controllers tied to feature logic, concrete transport classes inside modules, platform-specific assumptions, and scattered policy decisions. It also pulls large third-party payloads and release tooling into the active repository when kept in-tree.

## Decision

Keep legacy `Source/`, old `Build/`, old `Tools/`, and old `packaging/` out of the FusionDesk repository. Use them only from an external archive or separate legacy repository when reference is required.

Allowed uses:

```text
read behavior from an external archive
confirm protocol constants and packet semantics before encoding them into FusionDesk contracts
study platform edge cases
derive regression scenarios
compare performance or product behavior without linking or wrapping legacy code
```

Forbidden target uses:

```text
wrap old module classes as FusionDesk modules
include Source private headers in FusionDesk core
make old app/controller classes part of FusionDesk runtime
hide old transport managers behind the new module boundary
copy old directory structure into FusionDesk or back into the repository root
```

`adapter` in `FusionDesk` means integration with external technologies such as Qt, OS APIs, sockets, codecs, JNI, relay, or tunnel implementations. It does not mean a long-term wrapper around legacy `Source` modules.

## Consequences

Positive:

```text
FusionDesk can remove old coupling instead of preserving it
module boundaries stay clean
new implementation can choose better designs
old code remains available only through explicit external reference material
```

Negative:

```text
more code must be written before the first feature runs
old behavior must be converted into explicit tests and contracts
some undocumented edge cases will require archaeology in the external archive
```

## Verification

```text
rg -n "Source/|ThirdParty/" include/fusiondesk/core src/core
rg -n "Source/Modules|Source/Apps" include/fusiondesk FusionDesk/src
git ls-files Source Build Tools packaging
```

The `git ls-files` command must return no files. Any non-documentation `Source/` reference inside `FusionDesk` must be reviewed and removed before the relevant stage gate.
