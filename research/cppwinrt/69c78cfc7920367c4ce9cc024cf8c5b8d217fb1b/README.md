# C++/WinRT MinGW build observation

- Upstream repository: <https://github.com/microsoft/cppwinrt>
- Upstream tag: `2.0.250303.1`
- Upstream commit: `69c78cfc7920367c4ce9cc024cf8c5b8d217fb1b`
- Commit date: `2025-03-03T15:22:06-08:00`

The upstream CMake build explicitly supports cross-compiling from Linux with a
mingw-w64 toolchain. At this commit, GCC 16.1.0 successfully built the native
prebuild generator, used upstream's pinned open-source `winmd` reader at commit
`0f1eae3bfa63fa2ba3c2912cbfe72a01db94cc5a`, and linked an x64
`cppwinrt.exe`.

All generated binaries and downloaded source remained under `/tmp`. The build
is reproduced by `phase2/scripts/build_mingw.py`.
