# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

FuBi has not yet published versioned releases, so all notable project history is currently recorded under `Unreleased`.

## [Unreleased]

### Added

- Static DLL export discovery that reads PE files without loading or executing the target module.
- Complete export enumeration for named, ordinal-only, aliased, and forwarded exports.
- Bounded PE parsing with explicit limits for image size, export tables, delay imports, and other untrusted metadata.
- A unified function catalog that combines PE exports, x64 runtime-function metadata, Guard CF metadata, and other supported function evidence.
- Module identity tracking based on canonical path, hash, architecture, timestamp, image size, preferred image base, and available PDB identity.
- Function selectors and catalog records that preserve names, aliases, ordinals, RVAs, executable-state checks, provenance, and callability state.
- Hash-pinned prototype profiles for supplying trusted function signatures when the binary itself does not contain enough type information.
- Local PDB symbol discovery with module-identity validation.
- Bounded PDB type extraction and type-graph conversion for supported function prototype evidence.
- Typed call request, result, diagnostic, argument, return-type, ABI, and policy contracts.
- Explicit authorization for calling cataloged non-exported functions by internal RVA.
- An isolated invocation worker executable for executing target functions outside the main FuBi process.
- Worker supervision, result transport, timeout handling, IPC cleanup, and normalized worker failure reporting.
- Architecture-aware worker selection for x86 and x64 targets.
- A normalized Windows x64 native-call adapter implemented in MASM.
- Windows x64 argument marshalling for integer, pointer, floating-point, and mixed register/stack calls.
- Windows x64 scalar integer, pointer, boolean, float, and double return handling.
- A normalized x86 call-frame model.
- x86 invocation support for `__cdecl`, `__stdcall`, `__thiscall`, and `__fastcall`.
- x86 ABI fixtures for calling-convention behavior, register preservation, and stack handling.
- Persistent JSONL session mode for machine-readable command dispatch.
- Strict JSONL request parsing, response framing, malformed-line recovery, and session lifecycle handling.
- Bounded string and byte-buffer arguments.
- Bounded output and in/out string buffers.
- Opaque session references for pointer-like values that must remain process-local.
- Opaque handle ownership and lifecycle contracts.
- Stable CLI exit codes for automation.
- Windows-specific bounded call-pattern recognition for supported WDF and CFG-related evidence.
- Static inspection diagnostics for PE headers, sections, imports, delay imports, exports, runtime functions, load configuration, and related metadata.
- CLI output-format and session-compatibility handling for the current static and invocation workflows.
- Optional hash-pinned acceptance testing against an external reference DLL.
- Acceptance coverage for Windows call-pattern recognition and architecture/calling-convention matrices.
- Regression fixtures for exports, persistent output buffers, opaque handles, static PE metadata, x86 ABIs, thiscall, fastcall, and register preservation.
- Early Boost-based signature parsing and its unit tests from the original FuBi implementation.
- The original direct DLL binding and calling implementation, preserved as legacy source for historical reference.

### Changed

- Reworked FuBi from the original direct `LoadLibrary`/raw-call design into a static-first discovery and validated invocation architecture.
- Replaced the original `Fubi::Call_function` production path with the current `FunctionCatalog`, `PrototypeProfile`, `CallContract`, `InvocationEngine`, and worker-based execution pipeline.
- Replaced ad-hoc x86 dispatch with normalized x86 call frames and compiler-declared calling-convention function types.
- Moved Windows x64 ABI marshalling into the dedicated `NativeCall_x64.asm` boundary.
- Kept incomplete or weak PDB and undecorated-symbol information display-only instead of treating it as sufficient call authorization.
- Bound prototype, symbol, and internal-RVA evidence to the exact target module identity.
- Strengthened call validation so the selected function, module identity, prototype, architecture, ABI, and argument shapes must agree before execution.
- Improved typed result formatting for narrow signed and unsigned scalar values.
- Improved session handling so process-local pointer and handle values are represented through controlled references rather than reusable raw integers.
- Made worker names and architecture-specific output artifacts predictable for orchestration and tests.
- Made Boost discovery work with local/offline installations instead of requiring build-time downloads.
- Removed dependency-fetch wording and references to retired external components from project documentation.
- Reorganized the repository into production source, platform-specific code, preserved legacy code, unit tests, fixtures, and integration verification scripts.
- Moved the original `Fubi.cpp`, `Fubi.h`, `SysExports.cpp`, and `SysExports.h` out of the modern production source path while preserving them in the repository.
- Moved platform-specific assembly into dedicated Windows source/fixture locations.
- Updated CMake paths and MASM object handling to match the organized source tree.
- Standardized README and usage documentation around the current function-discovery, prototype, binding, and invocation workflow.
- Documented the prototype-profile format, supported capability matrix, export/signature evidence model, bounded string behavior, worker-isolation limits, and current CLI behavior.
- Removed obsolete local project metadata while retaining GitHub issue and contribution templates.
- Added ignore rules for local verification build outputs.

### Fixed

- Corrected static export parsing bounds and allocation limits.
- Corrected function-catalog identity, selector handling, complete evidence output, executable-range checks, and export counts.
- Corrected canonical path length validation.
- Corrected profile identity validation and supported type-shape validation.
- Prevented undecorated or incomplete symbol/type evidence from becoming callable contracts.
- Corrected call-contract serialization so diagnostics, metadata, overrides, and errors survive round trips.
- Required trusted catalog provenance before internal-function authorization is accepted.
- Hardened Windows x64 native invocation safety checks.
- Bounded native invocation execution and retained timed-out contexts safely until process isolation is available.
- Prevented unbounded accumulation of timed-out in-process workers.
- Corrected worker-capacity reservation and release on preflight failures.
- Corrected worker termination verification and IPC cleanup ordering.
- Normalized isolated worker error and result handling.
- Hardened x86 invocation portability and worker ABI lifetime handling.
- Corrected Windows x64 outgoing stack-argument placement and alignment.
- Preserved x64 register arguments while preparing stack arguments.
- Corrected mixed integer/floating x64 calls and floating-point return capture.
- Preserved narrow scalar return widths and signedness.
- Hardened string and byte-buffer marshalling.
- Rejected oversized string payloads and invalid output-buffer descriptors.
- Rejected invalid pointer-like results returned through worker/session boundaries.
- Hardened parser boundaries for malformed JSON numbers, Unicode escapes, request structures, and JSONL framing.
- Preserved JSONL response framing after malformed input.
- Hardened Windows call-pattern arithmetic and evidence bounds.
- Corrected call-pattern target semantics.
- Corrected delay-import reporting under bounded inspection.
- Corrected worker failure fixture RVAs after source/build changes.
- Corrected export fixture expectations to match the complete export identity set.
- Corrected MASM fixture object paths after the repository layout refactor.

### Removed

- Removed the external decoder/disassembly dependency from the current production path.
- Removed automatic build-time dependency fetching from the supported build flow.
- Removed the original direct raw-call implementation from the modern FuBi executable; the source remains preserved under legacy code.
- Removed the legacy x86 call-dispatch path after normalized x86 frames became authoritative.
- Removed obsolete project metadata and retired dependency references.
