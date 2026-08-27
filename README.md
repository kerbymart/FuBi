# FuBi

FuBi is a static Windows PE inspection tool with a separate legacy DLL-calling
mode. Static analysis reads the target as bytes: it does not call `LoadLibrary`,
run `DllMain`, or execute exported functions.

## Static analysis

Static inspection is the default. These commands do not execute the target:

```powershell
Fubi.exe target.dll --analyze
Fubi.exe target.dll --dump report.txt
Fubi.exe target.dll --analyze --json report.json
Fubi.exe target.dll --dump report.txt --json report.json
Fubi.exe target.dll --strings --min-string-length 5
Fubi.exe target.dll --disasm-function OpenTriggerDevice
Fubi.exe target.dll --function-report OpenTriggerDevice
Fubi.exe target.dll --callers OpenTriggerDevice
Fubi.exe target.dll --callees OpenTriggerDevice
Fubi.exe target.dll --xrefs OpenTriggerDevice
Fubi.exe target.dll --xrefs-string "NativeUSB"
Fubi.exe target.dll --xrefs-import CreateFileW
```

With no option, `Fubi.exe target.dll` prints the static report to standard
output.

The P0 report currently contains:

- SHA-256 and file size
- DOS, NT, PE32/PE32+, architecture, image, alignment, subsystem, timestamp,
  checksum, and data-directory metadata
- sections with RVA, raw-file ranges, permissions, characteristics, and entropy
- named, ordinal-only, aliased, and forwarded exports
- decoration-derived C++ signatures with explicit provenance
- normal imports and delay imports, including names/ordinals, hints, thunk RVAs,
  and IAT RVAs
- ASCII and UTF-16LE strings with RVA, file offset, section, and encoding
- debug-directory and CodeView RSDS/NB10 PDB metadata
- x64 `.pdata` runtime-function boundaries and unwind RVAs
- Zydis-based x86/x64 static disassembly
- confirmed direct callers/callees, import xrefs, and string xrefs
- function reports combining symbols, boundaries, ABI observations, calls,
  strings, and annotated disassembly
- ASLR, NX, CFG, security-cookie, Guard CF table, SafeSEH, and CET metadata
- resource types and VERSIONINFO identity
- evidence-based driver classification and an explicit API capability matrix
- warnings for malformed or truncated structures
- human-readable and machine-readable JSON output

Plain C export names do not contain parameter or return-type information. FuBi
reports those prototypes as unknown unless definitive symbol information is
available; it does not guess declarations.

## Interactive runtime mode

Interactive mode is explicitly opt-in and may execute target code while loading
the DLL or calling an export:

```powershell
Fubi.exe target.dll --interactive
```

Do not use `--interactive` with untrusted, proprietary, driver-related, or
otherwise unsafe DLLs. Static report options cannot be combined with
`--interactive`.

## Architecture

- `PEImage` owns immutable file bytes, validates PE headers/sections, and is the
  only module responsible for bounded file reads and RVA-to-file translation.
- `PEAnalyzer` converts a validated image into transparent report records for
  exports, imports, delay imports, strings, hashes, debug metadata, runtime
  functions, disassembly, xrefs, security, resources, and classification.
- `AnalysisReport` serializes the same analysis data to text or JSON.
- `SysExports`, `DbgHelpDll`, and `Fubi` preserve the legacy loaded-module and
  interactive-call path.

This boundary keeps stable RVAs and file offsets canonical in static mode.
ASLR-dependent loaded addresses appear only in explicit runtime mode.

## Build and test

FuBi requires Windows, CMake, and a Visual Studio C++ toolchain. Boost headers
are used by the legacy signature parser; CMake uses a local `boost_1_87_0`
directory when present and otherwise downloads Boost 1.87. Zydis 4.1.1 is
fetched by CMake for instruction decoding.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Tests cover legacy export behavior, PE32 and PE32+ parsing, imports, delay
imports, forwarded and ordinal exports, CodeView data, ASCII/UTF-16 strings,
malformed/truncated files, and static non-execution. The non-execution fixture
creates a marker from `DllMain`; both API and end-to-end CLI tests verify that
static analysis never creates it.

## Current limitations

- `.pdata` records are runtime/unwind boundaries, not guaranteed semantic C/C++
  functions. Export-only leaf boundaries are explicitly marked heuristic.
- Call graphs include only statically resolvable direct calls. Unresolved
  indirect calls are labeled rather than guessed.
- String xrefs currently require a directly resolvable memory reference to the
  beginning of an extracted string.
- ABI observations are conservative Windows-x64 register-use evidence with low
  confidence; they are never promoted to exact declarations.
- FuBi reports PDB identity but does not yet download PDBs or resolve private
  source/type records from a matching local PDB.
- Resource enumeration reports type-level entries and VERSIONINFO rather than
  recursively dumping binary resource payloads.
