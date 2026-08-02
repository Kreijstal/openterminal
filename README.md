# OpenTerminal research

OpenTerminal is a harvest-first research project for building Microsoft
Windows Terminal with an open-source toolchain and running it on Wine.

The project records reproducible facts before attempting broad compatibility
work. CI probes may use a Windows installation as an oracle, but committed
research data must remain reviewable text or JSON. Microsoft binaries, SDK
payloads, NuGet archives, generated applications, and CI binaries are never
committed.

## Project phases

- **Phase 0:** inventory dependencies, tools, packages, and build capabilities.
- **Phase 1:** harvest WinMD, XAML/XBF, PRI, imports, and baseline runtime data.
- **Phase 2:** generate and validate an open-source build path.
- **Phase 3:** launch progressively larger Terminal components under Wine.
- **Phase 4:** validate the complete UI and ConPTY behavior, then split reusable
  fixes into upstreamable Wine patches.

The implementation targets are intentionally kept separate:

- Terminal build/source changes belong in a Terminal fork when they become
  necessary.
- Wine compatibility changes belong in
  [wine-kreijstal](https://github.com/Kreijstal/wine-kreijstal).
- Harvesters, research snapshots, and cross-project orchestration live here.

See [wine-kreijstal issue #5](https://github.com/Kreijstal/wine-kreijstal/issues/5)
for the umbrella compatibility tracker.

## Running Phase 0 locally

```bash
git clone https://github.com/microsoft/terminal /tmp/windows-terminal
python3 phase0/scripts/harvest_dependencies.py \
  /tmp/windows-terminal \
  --output /tmp/dependency-inventory.json
python3 -m json.tool /tmp/dependency-inventory.json >/dev/null
```

The Phase 0 GitHub Actions workflow performs the same harvest and publishes the
results as a CI artifact.
