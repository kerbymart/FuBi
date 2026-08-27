# FuBi Technical Design Implementation Tracker

Source: [`FUBI_TECHNICAL_DESIGN_PLAN.md`](./FUBI_TECHNICAL_DESIGN_PLAN.md)
Created: 2026-08-27
Last updated: 2026-08-27
Roadmap status: **Baseline implementations merged; acceptance work remains**
Current milestone: **Acceptance gaps, PDB type conversion, tracker evidence, and history maintenance**

## How to use this tracker

- Check an item only after its acceptance evidence is available.
- Use the milestone status values `Not started`, `In progress`, `Blocked`, or
  `Complete`.
- Add links to commits, pull requests, test logs, or review notes in each
  milestone's **Evidence** field.
- A milestone is complete only when all scope, acceptance, tests, and
  documentation items are checked.
- Keep target-specific knowledge in hash-pinned profiles or opt-in tests; do
  not hard-code `t1pidd.dll` details in production code.

## Progress summary

| Milestone | Branch | Depends on | Status | Progress |
| --- | --- | --- | --- | ---: |
| Design approval | n/a | n/a | In progress | [#2](https://github.com/kerbymart/FuBi/issues/2) |
| 1. Remove decoder dependency | `chore/3-remove-decoder-dependency` | Design approval | In progress | [PR #15](https://github.com/kerbymart/FuBi/pull/15) |
| 2. Function catalog core | `refactor/4-function-catalog-core` | 1 | In progress | [PR #17](https://github.com/kerbymart/FuBi/pull/17) |
| 3. Symbols and profiles | `feat/5-symbol-and-profile-prototypes` | 2 | In progress | [PR #18](https://github.com/kerbymart/FuBi/pull/18) |
| 4. Typed call requests | `feat/6-typed-call-request` | 3 | In progress | [PR #20](https://github.com/kerbymart/FuBi/pull/20) |
| 5. Native x64 invocation | `feat/7-native-invocation-x64` | 4 | In progress | [PR #21](https://github.com/kerbymart/FuBi/pull/21) |
| 6. Internal RVA binding | `feat/8-internal-rva-binding` | 5 | In progress | [PR #22](https://github.com/kerbymart/FuBi/pull/22) |
| 7. Script session protocol | `feat/9-script-session-protocol` | 6 | In progress | [PR #23](https://github.com/kerbymart/FuBi/pull/23) |
| 8. Isolated call worker | `feat/10-isolated-call-worker` | 7 | In progress | [PR #24](https://github.com/kerbymart/FuBi/pull/24) |
| 9. Native x86 invocation | `feat/11-native-invocation-x86` | 8 | In progress | [PR #25](https://github.com/kerbymart/FuBi/pull/25) |
| 10. Focused Windows patterns | `feat/12-windows-call-pattern-catalog` | 2 | In progress | [PR #26](https://github.com/kerbymart/FuBi/pull/26) |
| 11. t1pidd catalog acceptance | `test/13-t1pidd-catalog-acceptance` | 3 | In progress | [PR #27](https://github.com/kerbymart/FuBi/pull/27) |

Overall roadmap: **11/11 milestones have baseline implementation PRs, with detailed acceptance checklists still open**. Bounded PDB metadata is merged in [PR #28](https://github.com/kerbymart/FuBi/pull/28) and display-only graph validation is merged in [PR #30](https://github.com/kerbymart/FuBi/pull/30). Internal RVA acceptance is covered by [PR #62](https://github.com/kerbymart/FuBi/pull/62), bounded string output by [PR #64](https://github.com/kerbymart/FuBi/pull/64), mixed x64 ABI placement by [PR #68](https://github.com/kerbymart/FuBi/pull/68), and adversarial parser boundaries by [PR #70](https://github.com/kerbymart/FuBi/pull/70). Complete invocation-grade PDB type conversion remains open in [#29](https://github.com/kerbymart/FuBi/issues/29), published-history rewriting remains open in [#14](https://github.com/kerbymart/FuBi/issues/14), and the unchecked acceptance items below remain authoritative.

## Design approval gate

No implementation milestone should begin until the dependency policy, call
type vocabulary, internal-call policy, and scripting protocol are approved.

- [ ] D-01 Keep Boost Spirit as the only grandfathered third-party build
  dependency, with no network fetch.
- [ ] D-02 Remove general disassembly, call-graph, and xref commands with
  Zydis.
- [ ] D-03 Make `--list` the default action.
- [ ] D-04 Require invocation-grade prototypes for every call.
- [ ] D-05 Require hash-pinned profiles and `--allow-internal` for internal
  RVAs.
- [ ] D-06 Block framework-managed entry points such as `FxDriverEntryUm` by
  default.
- [ ] D-07 Use project-owned x64/x86 assembly adapters instead of libffi.
- [ ] D-08 Support repeated typed CLI arguments and a versioned JSONL session.
- [ ] D-09 Keep broad PE diagnostics secondary under `--inspect` only.
- [ ] D-10 Defer the focused Windows call-pattern scanner until the generic
  catalog and calling engine work.

Decision owner: _Unassigned_
Approval record: _Pending_

## Global requirements

These requirements apply to every milestone.

### Product invariants

- [ ] G-01 `--list`, `--describe`, and metadata-only commands never load or
  execute the target DLL.
- [ ] G-02 Only explicit calling commands may load a DLL and run `DllMain`.
- [ ] G-03 Discovered addresses are never treated as callable without an
  invocation-grade prototype and supported policy.
- [ ] G-04 FuBi never invents an exact prototype.
- [ ] G-05 Internal-RVA calls require stronger authorization than export calls.
- [ ] G-06 Every machine-readable response includes a schema version.
- [ ] G-07 Script protocol data goes to stdout and diagnostics go to stderr.
- [ ] G-08 RVA remains the canonical static identifier; loaded VA is
  runtime-only.

### Dependency and boundary rules

- [ ] G-09 Runtime dependencies remain limited to Windows system DLLs/APIs.
- [ ] G-10 Builds use MSVC, Windows SDK, CMake, and approved Boost headers only.
- [ ] G-11 New Microsoft SDK APIs are hidden behind project-owned adapters.
- [ ] G-12 No Capstone, libffi, LLVM, DIA wrapper, JSON library, or other
  decoder/invocation dependency is introduced.
- [ ] G-13 File/profile/request parsing is bounded and treats inputs as
  untrusted.
- [ ] G-14 Public domain records do not expose Windows handles.

## Milestone 1 — Remove Zydis dependency

Status: **In progress**
Branch: `chore/3-remove-decoder-dependency`
Evidence: _None_

- [ ] M1-01 Remove Zydis and Zycore configuration and linking from CMake.
- [ ] M1-02 Remove network `FetchContent` behavior.
- [ ] M1-03 Resolve Boost through `FUBI_BOOST_ROOT` or `find_package(Boost)` and
  emit an actionable configuration error when unavailable.
- [ ] M1-04 Remove decoder-dependent source paths, records, CLI flags, and
  tests.
- [ ] M1-05 Remove general disassembly and decoder-derived graph/xref/ABI
  claims.
- [ ] M1-06 Retain bounded PE metadata required by the later catalog.
- [ ] M1-07 Update README and CLI help to reflect the dependency and interface
  changes.
- [ ] M1-08 Verify a clean offline build, existing export/signature tests, and
  proof that static catalog operations do not execute `DllMain`.

## Milestone 2 — Function catalog core

Status: **In progress**
Branch: `refactor/4-function-catalog-core`
Depends on: M1
Evidence: _None_

- [ ] M2-01 Introduce `ModuleIdentity`, `FunctionId`, `FunctionRecord`,
  `PrototypeSpec`, and `TypeSpec` domain records.
- [ ] M2-02 Implement explicit callability states and stable reason codes.
- [ ] M2-03 Add static export, x64 runtime-function, and Guard CF providers.
- [ ] M2-04 Merge evidence by module hash plus RVA, never by display name alone.
- [ ] M2-05 Preserve export names, aliases, ordinals, RVAs, and forwarders.
- [ ] M2-06 Adapt `SysExports` without rewriting working enumeration behavior.
- [ ] M2-07 Make `--list` the default and implement `--list-callable` and
  `--describe`.
- [ ] M2-08 Produce deterministic text and versioned JSON without loading the
  target.
- [ ] M2-09 Verify existing export coverage and the reference t1pidd catalog
  baseline: 2 exports, 149 candidates, and source provenance.

## Milestone 3 — Symbol and profile prototypes

Status: **In progress**
Branch: `feat/5-symbol-and-profile-prototypes`
Depends on: M2
Evidence: _None_

- [ ] M3-01 Expand the DbgHelp adapter for local PDB function symbols and exact
  supported types.
- [ ] M3-02 Verify PDB GUID and age before applying symbols or types.
- [ ] M3-03 Add versioned profile parsing using the approved parser dependency.
- [ ] M3-04 Require SHA-256 and architecture matches by default.
- [ ] M3-05 Validate selectors, executable RVAs, ABI/type vocabulary, duplicate
  entries, and unknown required fields.
- [ ] M3-06 Merge PDB, profile, decorated-name, and unknown prototype evidence
  without silently resolving conflicts.
- [ ] M3-07 Treat only `exact-symbol` and explicit `user-declared` prototypes as
  invocation-grade.
- [ ] M3-08 Ensure inferred or partial prototypes remain display-only.
- [ ] M3-09 Verify profile mismatch rejection and that an exact fixture profile
  or PDB can make a supported fixture callable.

## Milestone 4 — Typed call requests

Status: **In progress**
Branch: `feat/6-typed-call-request`
Depends on: M3
Evidence: Bounded string output and inout marshalling in [PR #64](https://github.com/kerbymart/FuBi/pull/64), with targeted call-contract and string-output tests.

- [ ] M4-01 Implement versioned `CallRequest`, `CallResult`, and structured
  diagnostic records.
- [ ] M4-02 Implement typed CLI argument and one-call prototype override
  parsing.
- [ ] M4-03 Support the approved scalar, pointer, string, buffer, handle, and
  opaque-pointer vocabulary for the first calling phase.
- [ ] M4-04 Validate argument count, direction, width, signedness, ranges,
  pointer width, encoding, sizes, and ownership rules.
- [ ] M4-05 Cap input/output buffers and zero newly allocated output buffers.
- [ ] M4-06 Add stable diagnostic codes and exit-code mapping.
- [ ] M4-07 Route command processing through a fake invocation adapter; do not
  execute native calls in this milestone.
- [ ] M4-08 Make script output deterministic and prompt-free, with protocol on
  stdout and diagnostics on stderr.
- [ ] M4-09 Add positive/negative type tests and JSON request/response
  round-trip tests.

## Milestone 5 — Native x64 invocation

Status: **In progress**
Branch: `feat/7-native-invocation-x64`
Depends on: M4
Evidence: Mixed integer, floating, pointer, and stack-position coverage in [PR #68](https://github.com/kerbymart/FuBi/pull/68), with JSON round-trip and static non-execution assertions.

- [ ] M5-01 Add a normalized call frame independent of catalog and CLI logic.
- [ ] M5-02 Implement the project-owned Windows x64 assembly invocation adapter.
- [ ] M5-03 Correctly place integer/pointer arguments in RCX, RDX, R8, R9 and
  remaining arguments on the stack.
- [ ] M5-04 Preserve 32-byte shadow space, 16-byte alignment, and nonvolatile
  registers.
- [ ] M5-05 Capture supported return values from RAX and, when enabled, XMM0.
- [ ] M5-06 Keep marshalled call-frame memory alive through the call.
- [ ] M5-07 Load modules only for explicit runtime commands and bind named or
  ordinal exports with `GetProcAddress`.
- [ ] M5-08 Add one-shot `--call` using the shared command service.
- [ ] M5-09 Reject unsupported ABI/types before loading or invoking the DLL.
- [x] M5-10 Pass the x64 fixture matrix, including more than four arguments and
  stack/register invariant tests.

## Milestone 6 — Internal RVA binding

Status: **In progress**
Branch: `feat/8-internal-rva-binding`
Depends on: M5
Evidence: Hash-pinned internal RVA fixture and policy/identity rejection coverage in [PR #62](https://github.com/kerbymart/FuBi/pull/62).

- [ ] M6-01 Recheck loaded module path, SHA-256, architecture, and image identity.
- [ ] M6-02 Validate that the selected RVA lies in an executable section.
- [ ] M6-03 Compute loaded base plus RVA with overflow checks.
- [ ] M6-04 Require an invocation-grade, supported prototype.
- [ ] M6-05 Require `--allow-internal` or the equivalent request policy.
- [ ] M6-06 Block framework-managed targets unless a separately reviewed
  stronger override is defined.
- [ ] M6-07 Emit stable failures for mismatched hashes, invalid RVAs, missing
  authorization, and blocked entry points.
- [x] M6-08 Verify an internal fixture is called only with a matching hash-pinned
  profile and explicit authorization; mismatches are never executed.

## Milestone 7 — Script session protocol

Status: **In progress**
Branch: `feat/9-script-session-protocol`
Depends on: M6
Evidence: _None_

- [ ] M7-01 Implement versioned JSON and JSONL parsing with approved Boost
  Spirit only.
- [ ] M7-02 Support `hello`, `list`, `describe`, `call`, `release`, and `quit`.
- [ ] M7-03 Preserve correlation IDs in every response.
- [ ] M7-04 Add persistent module sessions through the shared command service.
- [ ] M7-05 Represent session-owned handles/pointers with opaque IDs rather
  than lossy JSON numbers.
- [ ] M7-06 Do not automatically dereference opaque IDs or arbitrary returned
  pointers.
- [ ] M7-07 Build `--shell` on the same command model and retain `--interactive`
  as a compatibility alias.
- [ ] M7-08 Recover from malformed requests without corrupting the session.
- [ ] M7-09 Pass multi-call session, opaque-reference reuse, stdout/stderr, and
  protocol-negotiation tests.

## Milestone 8 — Isolated call worker

Status: **In progress**
Branch: `feat/10-isolated-call-worker`
Depends on: M7
Evidence: _None_

- [ ] M8-01 Split the runtime into `Fubi.exe` controller and `FubiWorker.exe`.
- [ ] M8-02 Perform static catalog/profile/request validation before launching
  the worker.
- [ ] M8-03 Define a stable, versioned controller/worker protocol.
- [ ] M8-04 Add architecture-specific worker selection.
- [ ] M8-05 Enforce one-shot call timeouts.
- [ ] M8-06 Return structured process exit, crash, and timeout information while
  keeping controller stdout valid.
- [ ] M8-07 Document that isolation is not a security sandbox and forced
  termination may leak external resources.
- [ ] M8-08 Pass crash and hang fixture tests while keeping the controller
  responsive.

## Milestone 9 — Native x86 invocation

Status: **In progress**
Branch: `feat/11-native-invocation-x86`
Depends on: M8
Evidence: _None_

- [ ] M9-01 Add an x86 worker and normalized x86 call frames.
- [ ] M9-02 Implement and test x86 `cdecl`.
- [ ] M9-03 Implement and test x86 `stdcall`.
- [ ] M9-04 Add `thiscall` as a separate reviewable increment.
- [ ] M9-05 Add `fastcall` as a separate reviewable increment.
- [ ] M9-06 Verify convention-specific stack cleanup, register use, return
  capture, and nonvolatile register preservation.
- [ ] M9-07 Pass architecture and calling-convention fixture matrices.

## Milestone 10 — Focused Windows call patterns

Status: **In progress**
Branch: `feat/12-windows-call-pattern-catalog`
Depends on: M2; may proceed independently of M3 through M9 after D-10 permits it
Evidence: Bounded WDF and CFG recognizers with positive and truncated fixtures in [PR #58](https://github.com/kerbymart/FuBi/pull/58).

- [ ] M10-01 Keep this provider separate from the general catalog and do not
  expose a disassembly UI.
- [ ] M10-02 Recognize only fully specified Windows x64 direct `CALL rel32`
  patterns.
- [ ] M10-03 Recognize only fully specified RIP-relative IAT call patterns.
- [ ] M10-04 Add narrowly versioned WDF table patterns tied to recovered
  `WDF_BIND_INFO` metadata.
- [ ] M10-05 Add narrowly versioned supported MSVC CFG dispatch patterns.
- [ ] M10-06 Bounds-check every byte read and arithmetic operation and report
  pattern name, version, and provenance.
- [ ] M10-07 Treat pattern results as target/relationship evidence only; never
  as prototype evidence.
- [ ] M10-08 Pass exact positive and near-match negative fixtures; unknown
  patterns remain unknown and no decoder dependency is present.

## Milestone 11 — t1pidd catalog acceptance

Status: **In progress**
Branch: `test/13-t1pidd-catalog-acceptance`
Depends on: M3
Evidence: _None_

- [ ] M11-01 Add an opt-in harness using `FUBI_T1PIDD_PATH`; skip clearly when
  the path, DLL, or expected version is unavailable.
- [ ] M11-02 Confirm the reference module hash/version or report a mismatch.
- [ ] M11-03 Assert exports `FxDriverEntryUm` at RVA `0x7F40` and
  `OpenTriggerDevice` at RVA `0x5DE0` for the reference hash.
- [ ] M11-04 Assert 149 candidates and preserve source counts: 146 exception
  directory, 2 Guard CF, and 1 export heuristic.
- [ ] M11-05 Record PDB identity/path and UMDF 2.15 bind metadata with 257 slots.
- [ ] M11-06 Mark plain exports `requires-prototype` and `FxDriverEntryUm`
  `framework-managed` unless approved evidence changes their status.
- [ ] M11-07 Prove `--list`, `--describe`, and `--inspect` never load the target.
- [ ] M11-08 Provide a profile skeleton with no unverified prototype and do not
  add an automated t1pidd call test until its exact contract and safe test
  environment are known.

## Shared fixture and test backlog

- [ ] T-01 Controlled x64 and x86 fixture DLLs.
- [ ] T-02 Zero-argument and all supported integer-width returns.
- [ ] T-03 Pointer/handle echo and C-string/wide-string length functions.
- [x] T-04 Input checksum and output/inout buffer functions. String output and
  inout coverage is in [PR #64](https://github.com/kerbymart/FuBi/pull/64).
- [x] T-05 Floating argument/return fixtures when floating support begins.
  Mixed ABI coverage is in [PR #68](https://github.com/kerbymart/FuBi/pull/68).
- [ ] T-06 Named, ordinal-only, aliased, and forwarded exports.
- [x] T-07 Internal non-exported function with a profile-known RVA.
- [ ] T-08 Crash and hang functions.
- [x] T-09 `DllMain` marker proving metadata commands never execute the target.
  Static catalog assertions are covered in [PR #62](https://github.com/kerbymart/FuBi/pull/62) and [PR #68](https://github.com/kerbymart/FuBi/pull/68).
- [ ] T-10 Unit coverage for selectors, type parsing, validation, catalog merge,
  callability transitions, JSON round trips, diagnostics, and exit codes.
- [x] T-11 Adversarial cases for truncated PE data, offsets, sizes, counts,
  profile data, and requests. See [PR #70](https://github.com/kerbymart/FuBi/pull/70).

## Documentation and compatibility backlog

- [x] DOC-01 Document the catalog-first product workflow and safety model. See
  [PR #66](https://github.com/kerbymart/FuBi/pull/66).
- [ ] DOC-02 Document text, one-shot JSON, JSONL session, and exit-code contracts.
- [ ] DOC-03 Document the profile schema with hash-pinned export and RVA
  examples.
- [x] DOC-04 Explain that calling commands may execute `DllMain` and arbitrary
  target code. See [PR #66](https://github.com/kerbymart/FuBi/pull/66).
- [ ] DOC-05 Preserve complete export enumeration, signature dumps, aliases,
  ordinals, forwarders, decorated-name recovery, and human-readable output.
- [ ] DOC-06 Deprecate analysis-first language and general disassembly flags.
- [ ] DOC-07 Keep broad PE diagnostics secondary under `--inspect`.
- [ ] DOC-08 Record supported architecture, ABI, and type limitations.

## Roadmap definition of done

- [ ] DONE-01 Builds without Zydis, Zycore, network downloads, or new
  third-party runtime libraries.
- [ ] DONE-02 The default command lists function candidates without executing
  the target.
- [ ] DONE-03 Every function exposes stable identity, evidence, exact prototype
  source, and callability status.
- [ ] DONE-04 Exported x64 fixture functions can be invoked with validated typed
  arguments.
- [ ] DONE-05 Internal fixture functions require hash-pinned profiles and
  explicit policy.
- [ ] DONE-06 Text, one-shot JSON, and JSONL session interfaces share one
  command model.
- [ ] DONE-07 Crashes and hangs are isolated and reported structurally.
- [ ] DONE-08 t1pidd exports and internal candidates are cataloged generically.
- [ ] DONE-09 FuBi never claims an unknown t1pidd prototype is callable.
- [ ] DONE-10 No t1pidd-specific address, name, or type is hard-coded in
  production code.

## Change log

| Date | Change | Author |
| --- | --- | --- |
| 2026-08-27 | Created tracker from the technical design plan. | Codex |
