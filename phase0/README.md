# Phase 0: inventory and capability probes

Phase 0 establishes the exact upstream revision, its declared dependencies,
and the proprietary or unavailable tools encountered by an open build.

The dependency harvester records vcpkg dependencies, NuGet references, and
vendored dependency entries without downloading or redistributing package
payloads.

The build-surface harvester records:

- SDK, toolset, framework, AppContainer, WinUI, and C++/WinRT properties;
- MSBuild imports and conditioned targets;
- IDL, XAML, manifest, resource, project, and solution inputs;
- SDK, COM, assembly, and project references;
- linker dependency declarations and unique library tokens; and
- explicit executable mentions in scripts and MSBuild `Exec` tasks, including
  C++/WinRT, makepri, makeappx, mt, mc, and release/test tools. IDL and resource
  item inventories separately expose the inputs handled by MIDL and rc.

Both harvesters operate only on Git-tracked source files and produce
deterministic JSON. Future probes should add evaluated MSBuild graphs and
compiler, MIDL/WinMD, XAML, PRI, MSIX, and Wine capability matrices.
