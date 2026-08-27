#pragma once

#include "PEImage.h"

#include <cstdint>
#include <string>
#include <vector>

struct PeExport
{
    uint32_t ordinal = 0;
    uint32_t rva = 0;
    std::vector<std::string> names;
    std::string forwarder;
    std::string signature;
    std::string signatureSource = "unknown";
};

struct PeImport
{
    std::string name;
    uint16_t hint = 0;
    uint16_t ordinal = 0;
    bool byOrdinal = false;
    uint32_t iatRva = 0;
    uint32_t thunkRva = 0;
};

struct PeImportModule
{
    std::string name;
    std::vector<PeImport> symbols;
};

struct PeString
{
    uint32_t rva = 0;
    uint32_t fileOffset = 0;
    std::string section;
    std::string encoding;
    std::string value;
};

struct PeDebugEntry
{
    uint32_t type = 0;
    uint32_t timestamp = 0;
    std::string codeViewFormat;
    std::string pdbGuid;
    uint32_t pdbAge = 0;
    std::string pdbPath;
};

struct PeRuntimeFunction
{
    uint32_t beginRva = 0;
    uint32_t endRva = 0;
    uint32_t unwindRva = 0;
};

struct PeInstruction
{
    uint32_t rva = 0;
    uint8_t length = 0;
    std::string bytes;
    std::string text;
    bool isCall = false;
    bool indirectCall = false;
    uint32_t directTargetRva = 0;
    std::string annotation;
    std::vector<uint32_t> referencedStringRvas;
};

struct PeFunction
{
    uint32_t beginRva = 0;
    uint32_t endRva = 0;
    uint32_t unwindRva = 0;
    uint32_t fileOffset = 0;
    std::string section;
    std::string name;
    std::string nameSource = "heuristic";
    std::string boundarySource = "exception-directory";
    std::vector<std::string> aliases;
    std::vector<uint32_t> callers;
    std::vector<uint32_t> callees;
    std::vector<std::string> importedCalls;
    std::vector<uint32_t> referencedStringRvas;
    std::vector<PeInstruction> instructions;
    std::vector<std::string> abiConsumedRegisters;
    uint32_t inferredMinimumArguments = 0;
    std::string abiConfidence = "none";
};

struct PeSecurity
{
    bool dynamicBase = false;
    bool highEntropyVa = false;
    bool nxCompatible = false;
    bool controlFlowGuard = false;
    bool safeSeh = false;
    bool cetCompatible = false;
    uint32_t securityCookieRva = 0;
    uint32_t guardFlags = 0;
    uint64_t guardFunctionCount = 0;
    std::vector<uint32_t> guardFunctionRvas;
};

struct PeResources
{
    std::vector<std::string> types;
    std::string companyName;
    std::string productName;
    std::string fileDescription;
    std::string fileVersion;
    std::string productVersion;
    std::string originalFilename;
    std::string internalName;
};

struct PeClassification
{
    std::string name;
    std::vector<std::string> evidence;
};

struct PeCapability
{
    std::string name;
    bool present = false;
    std::vector<std::string> evidence;
};

struct PEAnalysis
{
    std::string path;
    uint64_t fileSize = 0;
    std::string sha256;
    PeHeaders headers;
    std::vector<PeSection> sections;
    std::vector<PeExport> exports;
    std::vector<PeImportModule> imports;
    std::vector<PeImportModule> delayImports;
    std::vector<PeString> strings;
    std::vector<PeDebugEntry> debugEntries;
    std::vector<PeRuntimeFunction> runtimeFunctions;
    std::vector<PeFunction> functions;
    PeSecurity security;
    PeResources resources;
    std::vector<PeClassification> classifications;
    std::vector<PeCapability> capabilities;
    std::vector<std::string> warnings;
};
