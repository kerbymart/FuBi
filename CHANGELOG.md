# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

FuBi has not yet published versioned releases, so all project history is recorded under `Unreleased`.

## [Unreleased]

### 2013-12-30

- Initial commit

### 2022-12-29

- Update CMakeLists.txt with target and link libraries - Added comments on each source and header files - Updated `FunctionSpec` to use enum-type var and call types - Modified parser for the changes in `FunctionSpec` data structure - Modified `Fubi::Call_function` method to return the result type
- Created initial README file
- Fixed README headings
- Set platform to x86 in the CMakeLists file - Updated README build instruction
- Updated CMakeLists to enable Boost test - Added initial TestSignatureParser
- Added basic SignatureParser tests - Added grammar to capture 'class' function return type - Added addition base types

### 2022-12-30

- Updated README source file list section

### 2026-08-27

- feat: enumerate and dump complete DLL exports
- feat: add static PE reverse-engineering analysis
- docs: standardize README and usage guide
- fix: make capability analysis evidence-aware
- Merge pull request #1 from kerbymart/feat/static-pe-analysis
- docs: refocus README on function binding
- chore: remove external decoder dependency
- fix: bound static export parsing
- fix: cap static catalog allocations
- Merge pull request #15 from kerbymart/chore/remove-decoder-dependency
- feat: add static function catalog core
- fix: tighten function catalog identity and selectors
- fix: preserve complete catalog evidence output
- fix: reject non-executable function evidence
- fix: count complete export identity set
- fix: correct canonical path length check
- Merge pull request #17 from kerbymart/refactor/4-function-catalog-core
- feat: add hash-pinned prototype profiles
- fix: harden prototype profile integration
- fix: validate profile identities and type shapes
- feat: add validated local PDB symbol evidence
- feat: apply static symbol evidence to catalogs
- fix: bind symbol evidence to module identity
- fix: keep undecorated symbols display-only
- Merge pull request #18 from kerbymart/feat/5-symbol-and-profile-prototypes
- feat: add typed call request contracts
- style: normalize call contract header
- fix: harden typed call validation and transport
- fix: round-trip call diagnostics
- fix: preserve call contract metadata and errors
- fix: preserve call metadata and override transport
- fix: complete call result transport and policy
- fix: enforce complete call identity contracts
- fix: require trusted internal authorization provenance
- fix: require catalog-owned internal authorization
- Merge pull request #20 from kerbymart/feat/6-typed-call-request
- feat: add x64 exported invocation engine
- style: normalize invocation header
- fix: harden x64 invocation safety checks
- feat: bound invocation worker execution
- fix: retain timed out invocation context
- fix: cap retained timeout workers
- fix: reserve invocation worker capacity atomically
- fix: release worker reservation on preflight failure
- Merge pull request #21 from kerbymart/feat/7-native-invocation-x64
- feat: authorize checked internal RVA calls
- test: cover internal invocation authorization boundary
- Merge pull request #22 from kerbymart/feat/8-internal-rva-binding
- feat: add deterministic JSONL session mode
- fix: enforce strict JSONL request boundaries
- fix: validate JSON request structure completely
- fix: enforce JSON number and unicode grammar
- Merge pull request #23 from kerbymart/feat/9-script-session-protocol
- feat: add isolated invocation worker executable
- feat: supervise calls in isolated worker process
- fix: normalize isolated worker failures
- fix: verify worker termination and IPC cleanup
- fix: close worker result before IPC cleanup
- Merge pull request #24 from kerbymart/feat/10-isolated-call-worker
- feat: add guarded x86 cdecl invocation path
- Merge pull request #25 from kerbymart/feat/11-native-invocation-x86
- feat: add bounded Windows call pattern catalog
- fix: bound and tighten Windows pattern evidence
- fix: harden pattern catalog arithmetic bounds
- Merge pull request #26 from kerbymart/feat/12-windows-call-pattern-catalog
- test: add opt-in hash-pinned acceptance harness
- Merge pull request #27 from kerbymart/test/13-t1pidd-catalog-acceptance
- feat: extract bounded PDB type metadata
- fix: keep partial PDB metadata display-only
- fix: compile display-only PDB regression
- Merge pull request #28 from kerbymart/feat/19-pdb-type-extraction
- feat: validate bounded PDB type graphs
- fix: keep incomplete PDB graphs display-only
- Merge pull request #30 from kerbymart/feat/29-pdb-type-graph
- test: cover x64 invocation fixture matrix
- fix: preserve narrow scalar return widths
- Merge pull request #34 from kerbymart/test/33-x64-invocation-fixture-matrix
- feat: convert bounded PDB type graphs
- Merge pull request #35 from kerbymart/feat/29-pdb-type-graph-complete
- feat: dispatch persistent JSONL session actions
- fix: preserve JSONL response framing
- Merge pull request #37 from kerbymart/feat/36-jsonl-session-dispatch
- feat: support x86 calling conventions
- fix: keep x86 catalog validation portable
- fix: retain x86 worker ABI lifetime
- Merge pull request #39 from kerbymart/feat/38-x86-calling-conventions
- feat: recognize bounded x64 call patterns
- fix: preserve call pattern target semantics
- Merge pull request #41 from kerbymart/feat/40-call-iat-patterns
- chore: support offline Boost discovery
- Merge pull request #43 from kerbymart/chore/42-offline-boost-discovery
- feat: add normalized x64 call adapter
- fix: place x64 stack arguments correctly
- fix: align x64 outgoing stack arguments
- fix: preserve x64 register arguments
- Merge pull request #45 from kerbymart/feat/44-normalized-x64-call-frame
- feat: support bounded string and byte buffer arguments
- fix: harden string and buffer marshalling
- fix: bound byte buffer decoding
- Merge pull request #47 from kerbymart/feat/46-bounded-strings-byte-buffers
- feat: report isolated worker outcomes
- feat: support x64 floating point calls
- test: verify JSONL malformed-line recovery
- test: verify scanned call pattern evidence
- feat: recognize bounded WDF and CFG call patterns
- feat: select invocation workers by architecture
- test: verify hash-pinned internal RVA calls (#62)
- feat: support bounded string output arguments (#64)
- docs: document bounded string outputs (#66)
- test: cover mixed x64 floating ABI calls (#68)
- test: harden parser boundary coverage (#70)
- fix: reject invalid pointer results from workers (#74)
- feat: enforce persistent JSONL session lifecycle (#76)
- feat: define stable exit-code contract (#78)
- docs: clarify worker isolation limits (#80)
- test: verify worker failure protocol recovery (#82)
- test: verify framework-managed call blocking (#84)
- docs: document prototype profile schema (#86)
- docs: add capability matrix (#88)
- feat: complete x86 worker invocation coverage (#90)
- test: harden x86 ABI worker guarantees (#92)
- test: verify x86 nonvolatile register preservation (#94)
- feat: add x86 thiscall invocation support (#96)
- feat: add x86 fastcall invocation coverage (#98)
- test: add x86 convention matrix verification (#100)
- test: verify architecture and calling-convention matrices (#102)
- test: add Windows pattern acceptance coverage (#104)
- test: add opt-in t1pidd catalog acceptance (#106)
- feat: add opaque session reference lifecycle (#113)

