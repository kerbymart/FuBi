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

## Capability matrix

The table describes the implemented invocation boundary. A profile can parse
additional evidence, but parsing a type does not make it callable.

| Capability | x64 | x86 |
| --- | --- | --- |
| Supported ABI names | `x64`, `win64` | `__cdecl`, `__stdcall`, `__thiscall`, `__fastcall` |
| Integer widths | 8, 16, 32, 64 bits | 8, 16, 32, 64 bits |
| Scalar arguments | Boolean, integer, floating point, and pointer values | Boolean, integer, floating point, and pointer values |
| Scalar returns | Integer, Boolean, and floating point values; one-shot pointer returns are rejected, while persistent JSONL sessions tokenize pointer results | Integer and Boolean values; floating point and pointer returns are rejected by the x86 worker boundary |
| Structure and `void` returns | Rejected by the native adapter | Rejected by the native adapter |
| Structures and aggregates as arguments | Rejected by the native adapter | Rejected by the native adapter |
| Variadic prototypes | Not supported as an invocation contract | Not supported as an invocation contract |
| Strings and byte buffers | Narrow `cstr` or `utf8`, wide `utf16` or `wstr`; `in`, `out`, and `inout` require explicit buffer rules | Narrow `cstr` or `utf8`, wide `utf16` or `wstr`; `in`, `out`, and `inout` require explicit buffer rules |
| Buffer limit | 16 MiB per string or byte buffer | 16 MiB per string or byte buffer |
| Pointer arguments | Input opaque references only, with no output buffer or ownership descriptor | Input opaque references only, with no output buffer or ownership descriptor |
| Invocation worker | `FubiWorker.exe`, validated as x64 before launch | `FubiWorker_x86.exe`, validated as x86 before launch |

Profiles use the schema documented in [Prototype profiles](#prototype-profiles).
Every call still needs a complete invocation-grade prototype and arguments
whose types, widths, direction, encoding, and buffer sizes match it. A
function's export name or discovered RVA is evidence of identity, not proof of
its ABI.

For strings, narrow encodings use UTF-8 text and reject embedded NUL bytes.
Wide encodings use UTF-16 and require output or inout buffer sizes divisible by
two. `out` arguments start zeroed, while `inout` arguments preserve the input
within the supplied size. Every string or byte buffer is capped at exactly
16 MiB, and oversized or overflowing descriptors are rejected before loading.

Internal RVAs require a hash-pinned profile, an executable catalog RVA, a
complete module identity match, and explicit internal-call authorization.
Framework-managed entries are rejected by default. Returned pointers are
reported only as opaque values and are not dereferenced, followed, or freed;
the isolated worker rejects pointer-return prototypes before launch.

Listing, describing, cataloging, and profile validation are static operations.
They read bounded file or metadata bytes and do not load the target DLL or run
`DllMain`. Only an explicit call action can load the target. See the
[Exit-code contract](#exit-code-contract) for stable command-line results and
structured JSONL process semantics.

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

Machine-readable output can be selected with either the compatibility aliases
`--json` and `--jsonl`, or the documented format options `--format json` and
`--format jsonl`. The `--session` option is an alias for `--format jsonl` and
reads the versioned JSONL request stream from standard input. `--shell` and
`--interactive` remain aliases for a JSONL session with shell behavior.

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

## Prototype profiles

Use `--profile <file>` to add an explicit, invocation-grade prototype to a
catalog entry. Profile schema version `1` is a JSON object with exactly these
top-level fields:

```json
{
  "schema_version": 1,
  "module": {
    "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    "architecture": "x64",
    "path": "C:\\trusted\\example.dll",
    "timestamp": 0,
    "image_size": 0,
    "preferred_image_base": 0,
    "pdb_guid": "",
    "pdb_age": 0
  },
  "functions": [
    {
      "rva": 4096,
      "selector": "Example",
      "abi": "x64",
      "return_type": { "kind": "integer", "width": 32, "signed": true },
      "parameters": [],
      "variadic": false,
      "framework_managed": false
    }
  ]
}
```

The module `sha256` and `architecture` are required. `path`, `timestamp`,
`image_size`, `preferred_image_base`, `pdb_guid`, and `pdb_age` are optional
identity evidence. A function requires an executable `rva`, a unique optional
`selector`, a supported ABI, a complete `return_type`, and a `parameters`
array. `x64` and `win64` apply to x64 images. `__cdecl`, `__stdcall`,
`__thiscall`, and `__fastcall` apply to x86 images.

For x86 `__thiscall`, the first parameter is an explicit, non-null
`pointer` value encoded as a 32-bit `opaque:` reference. FuBi places that
reference in ECX and passes the remaining parameters on the stack. The
reference is a caller-supplied object handle; FuBi never dereferences it or
assumes an object layout. Missing, null, malformed, or non-pointer object
references are rejected before the target module is loaded.

Type objects support `void`, `bool`, `integer`, `floating`, `string`, `bytes`,
`pointer`, and `structure`. Type details may include `width`, `signed`,
`pointer_depth`, `direction` (`in`, `out`, or `inout`), `element_count`,
`encoding`, `ownership`, and `layout`. Integer widths are 8, 16, 32, or 64;
floating widths are 32 or 64. Strings and byte buffers require pointer depth
one, and byte buffers use width 8. Width, pointer depth, encoding, ownership,
and layout must describe the actual target contract.

For an exported function, a sanitized profile entry can identify the export
without exposing a local path:

```json
{
  "schema_version": 1,
  "module": { "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "architecture": "x64" },
  "functions": [
    { "rva": 4096, "selector": "Example", "abi": "x64",
      "return_type": { "kind": "integer", "width": 32 }, "parameters": [] }
  ]
}
```

An internal-RVA entry uses the same function shape, but its RVA must be in the
catalog and executable. Calling it also requires an explicit internal-call
policy, a complete module identity match, and the corresponding request fields
such as `allow_internal`, `authorization_provenance`, `module_sha256`,
`module_path`, `module_timestamp`, `module_image_size`,
`module_preferred_image_base`, `module_pdb_guid`, and `module_pdb_age`.

For example, this is a distinct internal-RVA profile entry. The identity and
RVA values are synthetic and must be replaced with values obtained from the
same trusted catalog and target image:

```json
{
  "schema_version": 1,
  "module": {
    "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "architecture": "x64",
    "timestamp": 305419896,
    "image_size": 65536,
    "preferred_image_base": 6442450944,
    "pdb_guid": "00000000-0000-0000-0000-000000000000",
    "pdb_age": 1
  },
  "functions": [
    {
      "rva": 8192,
      "selector": "internal_candidate",
      "abi": "x64",
      "return_type": { "kind": "integer", "width": 32, "signed": true },
      "parameters": [],
      "variadic": false,
      "framework_managed": false
    }
  ]
}
```

This profile does not authorize a call by itself. The caller must opt into the
internal-call policy and provide complete matching identity evidence. FuBi
rejects framework-managed functions, non-executable RVAs, stale identities,
and missing authorization before loading the target.

Profiles do not prove that a declaration matches machine code. They record a
user-declared contract, and framework-managed entries remain blocked by
default. Invalid JSON, unknown fields, duplicate RVAs, unsupported types or
ABIs, hash mismatches, non-executable RVAs, and incomplete identity are
rejected before the target is loaded.

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
worker. x64 builds use `FubiWorker.exe`; an x86 build emits
`FubiWorker_x86.exe`. A worker is rejected when it is unavailable or
its PE bitness does not match the target.

For explicit `--call` operations, `--timeout <ms>` sets the worker wait limit
in milliseconds. It must be a decimal value that fits the protocol's unsigned
32-bit range. A value of zero uses the default timeout. A timed-out worker may
be forcibly terminated, but target-owned resources and external side effects
cannot be rolled back.

Worker isolation is an execution boundary, not a security sandbox. A target
DLL may execute arbitrary code, access its permitted process resources, start
threads, and perform external I/O. A timeout can force worker termination, but
it cannot guarantee cleanup of target-owned resources or undo external side
effects. The controller reports this limitation and does not describe an
unknown DLL as safe merely because it ran in a worker.

Static catalog, listing, description, and profile operations read file and
metadata bytes only. They do not load the target or execute `DllMain`. One-shot
CLI pointer results remain rejected. An explicit JSONL session keeps one
validated worker alive and tokenizes pointer results as opaque references;
numeric addresses never cross the protocol boundary. Runtime tests use
controlled, trusted fixture DLLs and should not load arbitrary contributor or
system DLLs.

Persistent JSONL sessions use `opaque:session-N` identifiers for values that
are explicitly retained by the session. The identifier is never a numeric
address, is valid only in the session that issued it, and can be released only
once. Release only removes the session record; FuBi never dereferences or
automatically frees the underlying target value.

## Testing

The test suite includes a fixture DLL whose `DllMain` writes a marker. Both the
catalog unit test and CLI test verify that the marker is absent after static
export discovery.
