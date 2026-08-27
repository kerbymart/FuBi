#pragma once

#include "FunctionCatalog.h"

#include <string>
#include <vector>

struct ProfileValidationError
{
    std::string code;
    std::string path;
    std::string message;
};

struct ProfileFunction
{
    uint32_t rva = 0;
    std::string selector;
    PrototypeSpec prototype;
    bool frameworkManaged = false;
};

struct PrototypeProfile
{
    uint32_t schemaVersion = 0;
    ModuleIdentity module;
    std::vector<ProfileFunction> functions;
};

// Parses the versioned, hash-pinned profile format. Parsing is static and has
// no dependency on a target module or Windows loader state.
bool ParsePrototypeProfile(const std::string& document,
    PrototypeProfile& profile, std::vector<ProfileValidationError>& errors);

// Validates a parsed profile against a catalog. The catalog is also the source
// of executable-RVA truth, so this operation cannot authorize an unknown RVA.
bool ValidatePrototypeProfile(const PrototypeProfile& profile,
    const FunctionCatalog& catalog, std::vector<ProfileValidationError>& errors);

// Adds one evidence item to a record. Conflicting declarations are retained as
// data and never silently replace the existing declaration.
bool MergePrototypeEvidence(FunctionRecord& record, const PrototypeSpec& prototype,
    std::string source, PrototypeQuality quality);
