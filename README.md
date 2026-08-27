# FuBi

FuBi is a Windows Function Binding and Calling tool. Its workflow is:

```text
discover functions -> establish signatures -> bind -> call -> return typed results
```

This milestone provides the safe first step: static export discovery. It reads
PE bytes directly and never loads the target DLL, so the target's `DllMain` and
other target code do not run.

## Current capability

- Lists named, ordinal-only, aliased, and forwarded exports.
- Reports the canonical export RVA.
- Uses a bounded PE parser for untrusted file input.
- Keeps target loading and invocation out of the current command surface.

An export address is discovery evidence, not a callable contract. Future
milestones add function candidates, exact prototype evidence, policy checks,
binding, and typed invocation.

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

## Safety model

- Listing reads only the target file and does not call `LoadLibrary`.
- A future explicit call action may load a DLL and execute arbitrary target
  code, including `DllMain`.
- FuBi will require an invocation-grade prototype, supported ABI and types,
  matching target identity, and required policy authorization before a call.
- Internal RVAs will require stronger authorization than exported functions.
- Process isolation limits failure propagation; it is not a security sandbox.

## Testing

The test suite includes a fixture DLL whose `DllMain` writes a marker. Both the
catalog unit test and CLI test verify that the marker is absent after static
export discovery.
