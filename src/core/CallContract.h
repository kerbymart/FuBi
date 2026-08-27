#pragma once

#include "FunctionCatalog.h"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

struct CallArgument
{
    TypeSpec type;
    std::string value;
    uint64_t bufferSize = 0;
    std::string ownership;
};

struct CallRequest
{
    static constexpr uint32_t kSchemaVersion = 1;
    uint32_t schemaVersion = kSchemaVersion;
    std::string action = "call";
    std::string correlationId;
    std::string selector;
    std::string reference;
    std::string moduleSha256;
    std::string modulePath;
    uint32_t moduleTimestamp = 0;
    uint32_t moduleImageSize = 0;
    uint64_t modulePreferredImageBase = 0;
    std::string modulePdbGuid;
    uint32_t modulePdbAge = 0;
    std::string authorizationProvenance;
    std::vector<CallArgument> arguments;
    PrototypeSpec prototypeOverride;
    bool hasPrototypeOverride = false;
    uint32_t timeoutMs = 0;
    bool allowInternal = false;
    bool allowSessionReferences = false;
};

struct CallDiagnostic
{
    std::string code;
    std::string path;
    std::string message;
};

struct CallResult
{
    static constexpr uint32_t kSchemaVersion = 1;
    uint32_t schemaVersion = kSchemaVersion;
    std::string action = "call";
    std::string correlationId;
    bool success = false;
    std::string status = "not-executed";
    std::string returnValue;
    std::vector<std::string> issuedReferences;
    std::string releasedReference;
    TypeSpec returnType;
    std::vector<CallArgument> outputValues;
    ModuleIdentity resolvedModule;
    PrototypeSpec prototypeUsed;
    uint64_t durationMs = 0;
    bool hasWorkerExitCode = false;
    uint32_t workerExitCode = 0;
    std::vector<CallDiagnostic> diagnostics;
};

bool ValidateCallRequest(const CallRequest& request, const FunctionCatalog& catalog,
    std::vector<CallDiagnostic>& diagnostics);
void WriteCallRequestJson(std::ostream& output, const CallRequest& request);
void WriteCallResultJson(std::ostream& output, const CallResult& result);
void WriteCallResultText(std::ostream& output, const CallResult& result);
bool ParseCallRequestJson(const std::string& document, CallRequest& request,
    std::vector<CallDiagnostic>& diagnostics);
bool ParseCallResultJson(const std::string& document, CallResult& result,
    std::vector<CallDiagnostic>& diagnostics);
