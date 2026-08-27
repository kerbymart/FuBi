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
    std::string correlationId;
    std::string selector;
    std::vector<CallArgument> arguments;
    PrototypeSpec prototypeOverride;
    bool hasPrototypeOverride = false;
    uint32_t timeoutMs = 0;
    bool allowInternal = false;
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
    std::string correlationId;
    bool success = false;
    std::string status = "not-executed";
    std::string returnValue;
    std::vector<CallDiagnostic> diagnostics;
};

bool ValidateCallRequest(const CallRequest& request, const FunctionCatalog& catalog,
    std::vector<CallDiagnostic>& diagnostics);
void WriteCallRequestJson(std::ostream& output, const CallRequest& request);
void WriteCallResultJson(std::ostream& output, const CallResult& result);
bool ParseCallRequestJson(const std::string& document, CallRequest& request,
    std::vector<CallDiagnostic>& diagnostics);
bool ParseCallResultJson(const std::string& document, CallResult& result,
    std::vector<CallDiagnostic>& diagnostics);
