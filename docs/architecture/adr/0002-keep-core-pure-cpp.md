# ADR 0002: Keep Core Pure C++

## Status

Accepted

## Context

The product needs Qt on PC and Android, but the same architecture must support multiple OS targets, multiple CPU architectures, native Android embedding, and future transport changes.

If Qt types enter core contracts, Android AAR, tests, transport, and non-Qt service processes become harder to isolate.

## Decision

`include/fusiondesk/core` and `src/core` are pure C++.

Forbidden in core:

```text
Qt includes
Qt value types
QObject ownership
QTcpSocket
QThread
QWindow
Android JNI types
OS handles
legacy Source includes
```

Qt belongs in:

```text
runtime/qt
adapters/qt
platform/<os>
bindings/android internals
apps
```

## Consequences

Positive:

```text
core can compile quickly
unit tests do not require Qt
Android public API can hide Qt
transport and module contracts stay stable
```

Negative:

```text
adapters must translate between Qt types and core types
some old code cannot move directly
```

## Verification

```text
rg -n "#include <Q|QString|QByteArray|QObject|QTcpSocket|QThread|QVariant|QJson|QWindow|QAndroid" include/fusiondesk/core src/core
```

The command should produce no matches.

