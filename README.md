# FuBi

FuBi is a Windows Function Binding and Calling utility for discovering DLL
exports, recovering available function signatures, binding exported functions
from a trusted DLL, and calling supported functions interactively.

FuBi's primary workflow is:

```text
Discover exports → understand signatures → bind functions → call functions
```

Static Portable Executable (PE) analysis supports that workflow. It gives FuBi
a safe, complete view of a DLL before execution: names, ordinals, aliases,
forwarders, signatures, calling conventions, and other evidence needed to
understand whether and how an exported function can be called. The broader PE
reporting features are useful diagnostics, but function binding and calling
remain the purpose of the project.

## Core capabilities

### Function discovery and binding

- Enumerates the complete DLL export table, including named, ordinal-only,
  aliased, and forwarded exports.
- Recovers C++ signatures and calling conventions when decorated names contain
  enough information.
- Preserves explicit provenance when a signature or function boundary is
  recovered heuristically.
- Loads trusted DLLs and binds export RVAs to runtime addresses.
- Displays the bound function catalog in the interactive CLI.
- Resolves functions by their recovered name or exported alias.

### Function calling

- Provides an interactive prompt for selecting and calling a bound export.
- Includes low-level cdecl and stdcall invocation paths on x86 and x64, plus
  thiscall and thiscall-vararg paths on x86.
- Reports the recovered return type together with the raw function result.

### Supporting PE analysis

- Parses PE32 and PE32+ headers, sections, exports, imports, and delay imports.
- Extracts ASCII and UTF-16LE strings with RVA and file-offset provenance.
- Reports CodeView/PDB, VERSIONINFO, security, resource, and driver metadata.
- Disassembles x86 and x64 code with Zydis.
- Finds direct callers, callees, import references, and string references.
- Recovers WDF bind metadata and supported KMDF/UMDF USB dispatch slots.
- Writes formatted text and machine-readable JSON reports.

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

The executable is normally generated at `build\Release\Fubi.exe`.

## Quick start: bind and call DLL functions

Interactive mode is FuBi's function-binding and calling interface. Only use it
with a DLL you trust:

```powershell
.\build\Release\Fubi.exe C:\path\to\trusted.dll --interactive
```

FuBi loads the DLL, enumerates and binds its exports, displays the function
catalog, and prompts for an exported function name. Enter `q` to exit.

```text
Interactive mode executes target DLL code.
Enter an exported function name, or q to quit:
MyExportedFunction
RESULT (int) = 0
q
```

An export appearing in the catalog does not automatically make it safe or
currently callable. FuBi must have sufficient signature and calling-convention
information, and the caller must provide arguments that match the real ABI.
See [Current calling limitations](#current-calling-limitations).

## Discover functions without loading the DLL

Use static mode to inspect an unknown DLL before deciding whether to load it.
With no option, FuBi prints the complete static report:

```powershell
.\build\Release\Fubi.exe C:\path\to\target.dll
```

Write the formatted report—including the complete export catalog and recovered
signatures—to a file:

```powershell
.\build\Release\Fubi.exe C:\path\to\target.dll --dump target-report.txt
```

Write both text and JSON reports:

```powershell
.\build\Release\Fubi.exe C:\path\to\target.dll `
  --dump target-report.txt `
  --json target-report.json
```

Inspect one exported function by name or RVA:

```powershell
.\build\Release\Fubi.exe C:\path\to\target.dll `
  --function-report MyExportedFunction

.\build\Release\Fubi.exe C:\path\to\target.dll `
  --disasm-function 0x1234 --disasm-bytes 128
```

Paths containing spaces must be enclosed in double quotes.

## Command-line reference

```text
Fubi.exe <dll-or-pe-file> [options]
```

| Option | Purpose |
| --- | --- |
| `--interactive` | Load a trusted DLL, bind its exports, display the function catalog, and enter the calling prompt. |
| `--analyze` | Print the complete static analysis report. This is also the default when no option is supplied. |
| `--dump <file>` | Write the complete formatted report, including detected exports and signatures. |
| `--json <file>` | Write the complete analysis as JSON. May be combined with `--dump`. |
| `--function-report <name-or-rva>` | Print the recovered evidence for one function. |
| `--disasm-function <name-or-rva>` | Disassemble a selected function. `--disasm` is an alias. |
| `--disasm-bytes <N>` | Limit requested disassembly to `N` bytes; `N` must be at least 2. |
| `--callers <name-or-rva>` | Print confirmed direct callers. |
| `--callees <name-or-rva>` | Print confirmed direct callees. |
| `--xrefs <name-or-rva>` | Print confirmed direct callers and callees. |
| `--strings` | Print extracted ASCII and UTF-16LE strings. |
| `--min-string-length <N>` | Set the minimum string length; the default is 5 and the minimum is 2. |
| `--xrefs-string <value>` | Print direct references to an extracted string. |
| `--xrefs-import <name>` | Print direct references to an imported function. |

Static report options can be combined where applicable. They cannot be
combined with `--interactive` because static inspection and DLL execution have
different safety boundaries.

## Why FuBi performs PE analysis

The PE analyzer exists to improve function binding and calling decisions. It
helps FuBi answer questions such as:

- Which functions are truly exported, including aliases and ordinals?
- Is an export executable code or a forwarder to another module?
- Does a decorated name encode a signature and calling convention?
- Where does the function begin, and what evidence supports that boundary?
- Which imported APIs, strings, and other functions does it reference?
- Is available evidence definitive, heuristic, absent, or unknown?

This information makes the function catalog more complete and transparent. It
does not prove that an arbitrary function is safe to invoke, and FuBi does not
invent missing C prototypes.

## Safety model

Static mode reads a DLL or PE file as bytes. It does not call `LoadLibrary`, run
`DllMain`, or invoke exported functions.

Interactive mode crosses that boundary: loading a DLL can execute `DllMain`,
and selecting an export executes code inside the target module. Use interactive
mode only with DLLs whose origin and behavior you trust. Do not use it with an
untrusted, unauthorized, driver-related, or otherwise unsafe binary.

## Current calling limitations

FuBi's export discovery is broader than its current high-level calling support:

- The interactive CLI currently supplies no function arguments.
- `Fubi::Call_function` currently dispatches only functions recovered as
  `__cdecl`.
- The x64 cdecl path currently supports zero-argument calls and returns a raw
  `DWORD` value.
- The x86 low-level implementation contains additional calling-convention
  paths, but the interactive CLI does not yet expose typed argument entry or
  convention selection.
- Plain C export names normally do not encode parameter or return types. FuBi
  reports those signatures as unknown instead of guessing.
- Forwarded, ordinal-only, unknown-signature, and unsupported-convention
  exports may be cataloged without being callable through the current prompt.

Future calling work should add an explicit callability status for every export,
typed CLI arguments, ABI validation, broader return types, and clearer errors
when a selected function cannot be invoked.

## Advanced analysis output

Text and JSON reports can include:

- file hash, architecture, image base, subsystem, timestamp, and data
  directories
- section ranges, permissions, characteristics, and entropy
- complete exports, imports, delay imports, ordinals, aliases, and forwarders
- signatures with evidence source and confidence
- strings, PDB records, x64 runtime-function boundaries, disassembly, call
  relationships, and cross-references
- ASLR, NX, CFG, security-cookie, Guard CF, SafeSEH, and CET metadata
- WDF framework versions, table metadata, and recovered dispatch calls
- resource identity, classification evidence, capability states, and warnings

Capability states are evidence-based: `observed`, `inferred`, `not observed`,
or `unknown`. In particular, `not observed` does not mean `false`.

Supporting analysis is intentionally conservative. Undecorated exports do not
provide exact prototypes, runtime-function records are not guaranteed semantic
C/C++ boundaries, and call graphs contain only statically resolvable direct
calls. Unsupported WDF slots, indirect calls, private PDB types, and binary
resource payloads remain unresolved rather than being guessed.

## Architecture

- `Fubi` owns the function-calling paths and dispatches a selected bound
  function.
- `SysExports` builds the runtime function catalog from a loaded DLL, recovers
  signatures, prints bindings, and writes export dumps.
- `DbgHelpDll` provides decorated-name recovery through Windows DbgHelp.
- `PEImage` safely validates file bytes and translates RVAs to file offsets.
- `PEAnalyzer` builds static evidence for exports, functions, imports, strings,
  disassembly, security, resources, WDF calls, and classification.
- `AnalysisReport` writes that static evidence as text or JSON and serves
  focused function queries.

The runtime and static paths are intentionally separate: static analysis helps
the user understand a DLL, while runtime binding and calling are explicit
operations for trusted modules.

## Testing

Build the project, then run the Release test suite:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Tests cover signature parsing, complete export enumeration, runtime export
metadata, PE32/PE32+ analysis, imports, forwarded and ordinal exports, WDF
dispatch recovery, malformed input, and static non-execution.
