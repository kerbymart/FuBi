# FuBi

FuBi is a Windows command-line utility for inspecting Portable Executable (PE)
files. It statically analyzes DLLs and executables, enumerates their complete
export tables, derives signatures when symbol decoration provides enough
information, and writes human-readable or JSON reports.

Static analysis is the default and does not load or execute the target file.
An explicitly enabled legacy interactive mode is available for trusted DLLs.

## Features

- Enumerates named, ordinal-only, aliased, and forwarded exports.
- Derives C++ signatures from decorated names and records their provenance.
- Dumps complete analysis results to formatted text and JSON files.
- Parses PE32 and PE32+ headers, sections, directories, imports, and delay
  imports.
- Extracts ASCII and UTF-16LE strings with their RVA, file offset, and section.
- Reports CodeView/PDB, VERSIONINFO, security, resource, and driver metadata.
- Disassembles x86 and x64 code with Zydis.
- Finds direct callers, callees, import references, and string references.
- Recovers WDF bind metadata and resolves supported KMDF/UMDF USB dispatch
  slots, including common Control Flow Guard call patterns.
- Produces focused function reports with boundaries, calls, strings, and
  annotated disassembly.
- Reports malformed or truncated structures without executing the input.

## Requirements

- Windows
- CMake 3.10 or later
- A Visual Studio C++ toolchain with C++17 support
- Git and network access for the initial dependency fetch

CMake uses a local `boost_1_87_0` directory when available; otherwise, it
downloads Boost 1.87 headers. Zydis 4.1.1 is fetched during configuration.

## Build

Open a Developer PowerShell for Visual Studio from the repository root:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The executable is generated under the configuration-specific build directory,
typically `build\Release\Fubi.exe`.

## Basic usage

Run FuBi with a PE file to print a complete static report to standard output:

```powershell
.\build\Release\Fubi.exe C:\path\to\target.dll
```

Write the formatted report to a file:

```powershell
.\build\Release\Fubi.exe C:\path\to\target.dll --dump target-report.txt
```

Write both formatted text and machine-readable JSON reports:

```powershell
.\build\Release\Fubi.exe C:\path\to\target.dll `
  --dump target-report.txt `
  --json target-report.json
```

List extracted strings with a custom minimum length:

```powershell
.\build\Release\Fubi.exe C:\path\to\target.dll `
  --strings --min-string-length 6
```

Inspect a specific exported function by name or RVA:

```powershell
.\build\Release\Fubi.exe C:\path\to\target.dll `
  --function-report OpenTriggerDevice

.\build\Release\Fubi.exe C:\path\to\target.dll `
  --disasm-function 0x1234 --disasm-bytes 128
```

Paths containing spaces must be enclosed in double quotes.

## Command-line reference

```text
Fubi.exe <pe-file> [options]
```

| Option | Description |
| --- | --- |
| `--analyze` | Print the complete static analysis report. This is also the default when no option is supplied. |
| `--dump <file>` | Write the complete formatted text report, including all detected exports and signatures. |
| `--json <file>` | Write the complete analysis as JSON. May be combined with `--dump`. |
| `--strings` | Print extracted ASCII and UTF-16LE strings. |
| `--min-string-length <N>` | Set the minimum extracted string length; `N` must be at least 2. The default is 5. |
| `--disasm-function <name-or-rva>` | Disassemble a function selected by export name or RVA. `--disasm` is an alias. |
| `--disasm-bytes <N>` | Limit requested disassembly to `N` bytes; `N` must be at least 2. |
| `--function-report <name-or-rva>` | Print a focused report for one function. |
| `--callers <name-or-rva>` | Print confirmed direct callers. |
| `--callees <name-or-rva>` | Print confirmed direct callees. |
| `--xrefs <name-or-rva>` | Print both direct callers and callees. |
| `--xrefs-string <value>` | Print direct references to an extracted string. |
| `--xrefs-import <name>` | Print direct references to an imported function. |
| `--interactive` | Load a trusted DLL and enter the legacy export-calling mode. |

Static report options can be combined where applicable, but they cannot be
combined with `--interactive`.

## Report contents

Text and JSON reports are generated from the same analysis model and include:

- SHA-256, file size, architecture, image base, subsystem, timestamp, checksum,
  alignment, and data directories
- section RVA and raw-file ranges, permissions, characteristics, and entropy
- complete export and import records, including names, ordinals, aliases,
  forwarders, hints, thunk RVAs, and IAT RVAs
- decorated-name signature evidence and explicit unknown prototypes
- strings, debug/PDB records, x64 runtime-function boundaries, disassembly,
  call relationships, and cross-references
- ASLR, NX, CFG, security-cookie, Guard CF, SafeSEH, and CET metadata
- WDF framework version, table metadata, and recovered dispatch calls
- resource types, VERSIONINFO identity, classification evidence, capability
  states with confidence/provenance, and parser warnings

Plain C export names do not encode return or parameter types. FuBi reports
those prototypes as unknown unless definitive symbol information is available;
it does not invent declarations.

Capability states are evidence-based: `observed` means positive evidence was
recovered, `inferred` identifies a heuristic conclusion, `not observed` means
the analyzer found no supporting evidence, and `unknown` means the current
analysis cannot determine the state reliably. `not observed` is never a hard
`false`.

## Safety

Default static analysis reads the target as bytes. It does not call
`LoadLibrary`, run `DllMain`, or invoke exported functions.

`--interactive` does load the DLL and may execute target code during loading or
when calling an export:

```powershell
.\build\Release\Fubi.exe C:\path\to\trusted.dll --interactive
```

Only use interactive mode with DLLs you trust. Do not use it for untrusted,
proprietary, driver-related, or otherwise unsafe files.

## Testing

Build the project, then run the Release test suite:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The tests cover signature parsing, complete export enumeration, PE32/PE32+
analysis, imports and delay imports, forwarded and ordinal exports, CodeView
data, strings, malformed input, and static non-execution. The non-execution
fixture attempts to create a marker from `DllMain`; API and end-to-end tests
verify that static analysis never creates it.

## Architecture

- `PEImage` owns immutable file bytes, validates headers and sections, performs
  bounded reads, and translates RVAs to file offsets.
- `PEAnalyzer` converts a validated image into report records.
- `AnalysisReport` serializes analysis records as text or JSON and serves
  focused queries.
- `SysExports`, `DbgHelpDll`, and `Fubi` provide the legacy loaded-module and
  interactive-call path.

This separation keeps static analysis based on stable RVAs and file offsets;
ASLR-dependent loaded addresses are confined to explicit interactive mode.

## Limitations

- Decorated names can expose C++ signature information, but undecorated C names
  generally cannot provide exact parameter or return types.
- `.pdata` entries describe runtime/unwind boundaries and are not guaranteed to
  represent semantic C or C++ functions. Export-only leaf boundaries are
  marked as heuristic.
- Call graphs contain statically resolvable direct calls only. Unresolved
  indirect calls are labeled rather than guessed.
- WDF resolution currently names published KMDF and UMDF USB function-table
  slots. Other WDF and class-extension slots are reported by slot when their
  table origin is recoverable, or remain unknown when it is not.
- String references require a directly resolvable reference to the beginning
  of an extracted string.
- ABI observations are conservative Windows x64 register-use evidence and are
  not promoted to exact declarations.
- FuBi reports PDB identity but does not download PDBs or resolve private type
  and source records from local PDB files.
- Resource analysis reports type-level entries and VERSIONINFO rather than
  recursively dumping binary resource payloads.
