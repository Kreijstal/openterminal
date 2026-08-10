# ICU MinGW build observation

- Upstream repository: <https://github.com/unicode-org/icu>
- Upstream tag: `release-78.3`
- Upstream commit: `21d1eb0f306e1141c10931e914dfc038c06121da`
- vcpkg baseline: `927f62e4b8838bd7e441e9c45103a16ffd75007e`
- vcpkg port tree: `f52873e40a919aa2d4c9f2832eeacb81b406dc28`

vcpkg built ICU 78.3's data, internationalization, and common libraries from
source for `x64-mingw-static`. The pinned port verifies the upstream release
archive with SHA-512
`04A49455E1489030C520A4BFD2664FA2171E7938D08F2ACDBBCB1FDA976639FD8B1F0704F2EEC89BA59A7B6D118CEAAB6EC5A096E40D9085A0895D91CE225245`.

Terminal's flattened Windows `icu.h` include is supplied by a textual adapter
that includes ICU's public headers. All downloaded source archives, native
tools, objects, libraries, and PE files remained below `/tmp`.
