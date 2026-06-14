# VC-LTL Vendor Notes

This directory contains a trimmed VC-LTL source vendor used by the CMake build.
The repository intentionally does not store generated VC-LTL `.lib` files.

During an MSVC x64/x86 build, CMake copies this source tree to
`<build>/vc-ltl-build/src` and invokes MSBuild to generate:

- `TargetPlatform/6.2.9200.0/lib/x64/libucrt.lib`
- `TargetPlatform/6.2.9200.0/lib/x64/libvcruntime.lib`
- `TargetPlatform/6.2.9200.0/lib/Win32/libucrt.lib`
- `TargetPlatform/6.2.9200.0/lib/Win32/libvcruntime.lib`

`Tools/LibMaker.exe` is kept as VC-LTL's small build helper because the
upstream MSBuild projects use it to create weak symbols and extract objects.
