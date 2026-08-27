# FuBi

FuBi is a Windows Function Binding and Calling tool. Its workflow is:

```text
discover functions -> establish signatures -> bind -> call -> return typed results
```

Static catalog actions read PE bytes directly and never load the target DLL, so
the target's `DllMain` and other target code do not run. Explicit call actions
load the target only after request, prototype, policy, and identity validation.

## Current capability

- Lists named, ordinal-only, aliased, and forwarded exports.
- Reports the canonical export RVA.
- Uses a bounded PE parser for untrusted file input.
- Supports typed scalar, bounded byte-buffer, and bounded string calls.
- Supports narrow UTF-8 and UTF-16 output and input/output buffers.
- Reports returned pointer values as opaque values and never dereferences them.

An export address is discovery evidence, not a callable contract. Calls require
an invocation-grade prototype and a supported ABI. String output and input /
output arguments require an explicit buffer size. Narrow strings use `utf8` or
`cstr` encoding; wide strings use `utf16` or `wstr` and a buffer size divisible
by two. Returned string buffers are converted to deterministic UTF-8 text up
to the first null terminator. FuBi does not automatically free DLL-owned
memory or follow opaque pointers.

## Requirements

- Windows
- CMake 3.10 or later
- Visual Studio C++ toolchain with C++17 support
- Local Boost headers for the grandfathered signature-parser boundary

FuBi never downloads build dependencies. By default CMake uses the local
`boost_1_87_0` directory. Set `FUBI_BOOST_ROOT` when the headers live elsewhere.
Static image reads are capped at 64 MiB by default; set
`FUBI_MAX_IMAGE_BYTES` at CMake configuration time to choose a different hard
limit.

## Build

From a Developer PowerShell:

```powershell
cmake -S . -B build -A x64 -DFUBI_BOOST_ROOT=C:\path\to\boost
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

FuBi never downloads dependencies during configuration. If the bundled
`boost_1_87_0` headers are not available, CMake first checks the explicit
`FUBI_BOOST_ROOT` path and then uses a locally installed Boost package found
through `CMAKE_PREFIX_PATH`. To verify an offline configuration, use a clean
build directory and disable all package-manager and FetchContent network
behavior explicitly:

```powershell
cmake -S . -B build-offline -A x64 `
  -DFUBI_BOOST_ROOT=C:\path\to\boost `
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF `
  -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF `
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
```

The command must complete using only the supplied local headers and the
approved MSVC, Windows SDK, and CMake inputs.

Configuration fails with an actionable error if the local Boost header root is
unavailable.

## List exports safely

```powershell
.\build\Release\Fubi.exe C:\path\to\target.dll
.\build\Release\Fubi.exe C:\path\to\target.dll --list
```

The no-option command is equivalent to `--list`. It is deliberately a static,
non-executing operation.

Example output:

```text
FuBi static export catalog
module = C:\path\to\target.dll
export_count = 1

[export]
ordinal = 1
rva = 0x1000
name = Example
aliases = Example
forwarder = <none>
```

## Exit-code contract

The command-line interface uses stable numeric exit codes: `0` success, `2`
usage, `3` catalog load failure, `4` selector not found, `5` ambiguous
selector, `6` profile load or validation failure, `7` symbol load failure, `8`
request validation failure, and `9` invocation failure. JSONL sessions return
structured responses and normally exit `0` after processing the stream.

## Safety model

- Listing reads only the target file and does not call `LoadLibrary`.
- Only an explicit call action may load a DLL and execute arbitrary target code,
  including `DllMain`.
- FuBi will require an invocation-grade prototype, supported ABI and types,
  matching target identity, and required policy authorization before a call.
- Internal RVAs will require stronger authorization than exported functions.
- Process isolation limits failure propagation; it is not a security sandbox.

The isolated controller validates the target architecture before launching a
worker. x64 builds use `FubiInvocationWorker.exe`; an x86 build emits
`FubiInvocationWorker_x86.exe`. A worker is rejected when it is unavailable or
its PE bitness does not match the target.

Worker isolation is an execution boundary, not a security sandbox. A target
DLL may execute arbitrary code, access its permitted process resources, start
threads, and perform external I/O. A timeout can force worker termination, but
it cannot guarantee cleanup of target-owned resources or undo external side
effects. The controller reports this limitation and does not describe an
unknown DLL as safe merely because it ran in a worker.

Static catalog, listing, description, and profile operations read file and
metadata bytes only. They do not load the target or execute `DllMain`. Pointer
results remain unsupported across the isolated worker boundary because a raw
address is not a reusable session reference. Runtime tests use controlled,
trusted fixture DLLs and should not load arbitrary contributor or system DLLs.

## Testing

The test suite includes a fixture DLL whose `DllMain` writes a marker. Both the
catalog unit test and CLI test verify that the marker is absent after static
export discovery.
