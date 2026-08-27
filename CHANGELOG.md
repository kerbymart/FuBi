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

