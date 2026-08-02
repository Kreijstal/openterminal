# Phase 0: inventory and capability probes

Phase 0 establishes the exact upstream revision, its declared dependencies,
and the proprietary or unavailable tools encountered by an open build.

The initial harvester records vcpkg dependencies, NuGet references, and vendored
dependency entries without downloading or redistributing package payloads.
Future probes should add compiler, MIDL/WinMD, XAML, PRI, MSIX, and Wine
capability matrices as deterministic text or JSON.
