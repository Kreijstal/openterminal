# Agent guide

## Research data

- Commit only source, documentation, and reviewable textual data such as JSON,
  XML, CSV, or plain-text reports.
- Never commit PE files, libraries, archives, installers, NuGet packages, SDK
  payloads, symbol files, XBF files, or other generated binary artifacts.
- Keep downloaded and generated binaries under `/tmp` locally or in temporary
  GitHub Actions artifact directories.
- Pin every committed research snapshot to an exact upstream commit.
- Prefer deterministic harvesters: do not put timestamps or machine-specific
  paths in committed output.

## Wine work

- Keep exploratory probes and harvested observations in this repository.
- Put Wine implementation changes in `Kreijstal/wine-kreijstal` and link them
  back to the relevant research snapshot or issue.
- Keep Terminal source changes in a Terminal fork if they become necessary.
