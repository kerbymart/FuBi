#include "stdafx.h"

#include "FunctionCatalog.h"
#include "PrototypeProfile.h"
#include "DbgHelpDll.h"
#include "CallContract.h"
#include "InvocationEngine.h"
#include "ProcessInvocation.h"
#include "ExitCodes.h"
#include "SessionReferences.h"

#include <fstream>
#include <cerrno>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
void WriteJsonString(std::ostream& output, const std::string& value)
{
    output << '"';
    for (const unsigned char character : value)
    {
        if (character == '"' || character == '\\') output << '\\' << character;
        else if (character == '\n') output << "\\n";
        else if (character == '\r') output << "\\r";
        else if (character == '\t') output << "\\t";
        else if (character < 0x20) output << "\\u00" << std::hex << std::setw(2)
            << std::setfill('0') << static_cast<unsigned>(character) << std::dec
            << std::setfill(' ');
        else output << character;
    }
    output << '"';
}

void WriteSessionStatus(std::ostream& output, const std::string& action,
    const std::string& correlationId, const FunctionCatalog& catalog,
    bool success, const std::string& status, const std::vector<CallDiagnostic>& diagnostics = {},
    const std::vector<std::string>& issued = {}, const std::string& released = {})
{
    CallResult response;
    response.action = action;
    response.correlationId = correlationId;
    response.resolvedModule = catalog.Module();
    response.success = success;
    response.status = status;
    response.diagnostics = diagnostics;
    response.issuedReferences = issued;
    response.releasedReference = released;
    WriteCallResultJson(output, response);
    output << '\n';
}

bool ParseWorkerPointer(const std::string& value, uint64_t& address)
{
    const std::string prefix = "opaque:0x";
    if (value.rfind(prefix, 0) != 0 || value.size() == prefix.size()) return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str() + prefix.size(), &end, 16);
    if (errno == ERANGE || end == value.c_str() + prefix.size() || *end != '\0') return false;
    address = static_cast<uint64_t>(parsed);
    return true;
}

void WriteSessionCatalog(std::ostream& output, const FunctionCatalog& catalog,
    const std::string& correlationId)
{
    std::ostringstream payload;
    catalog.WriteJson(payload);
    std::string document = payload.str();
    while (!document.empty() && (document.back() == '\n' || document.back() == '\r')) document.pop_back();
    output << "{\"schema_version\":1,\"action\":\"list\",\"correlation_id\":";
    WriteJsonString(output, correlationId);
    output << ",\"success\":true,\"status\":\"completed\",\"catalog\":"
           << document << "}\n";
}

void WriteSessionDescription(std::ostream& output, const FunctionCatalog& catalog,
    const std::string& correlationId, const std::string& selector)
{
    const std::vector<const FunctionRecord*> matches = catalog.FindAll(selector);
    if (matches.size() != 1)
    {
        const char* code = matches.empty() ? "selector-not-found" : "selector-ambiguous";
        const char* message = matches.empty() ? "selector does not identify a catalog record" : "selector identifies multiple catalog records";
        WriteSessionStatus(output, "describe", correlationId, catalog, false,
            "validation-failed", {{code, "selector", message}});
        return;
    }
    std::ostringstream payload;
    catalog.WriteJsonDescribe(payload, *matches.front());
    std::string document = payload.str();
    while (!document.empty() && (document.back() == '\n' || document.back() == '\r')) document.pop_back();
    output << "{\"schema_version\":1,\"action\":\"describe\",\"correlation_id\":";
    WriteJsonString(output, correlationId);
    output << ",\"success\":true,\"status\":\"completed\",\"description\":"
           << document << "}\n";
}

void PrintUsage()
{
    std::cerr << "Usage:\n"
              << "  Fubi.exe <dll-file> [--list|--list-callable|--describe <name|#ordinal|0xRVA>|--inspect <name|#ordinal|0xRVA>|--call <selector>] [--arg <kind:value> ...] [--timeout <ms>] [--profile <file>] [--prototype-override <file>] [--symbols] [--json|--jsonl|--shell|--interactive]\n";
}

struct Options
{
    std::string targetPath;
    std::string action = "list";
    std::string selector;
    bool json = false;
    bool jsonl = false;
    bool shell = false;
    std::string profilePath;
    std::string prototypeOverridePath;
    bool symbols = false;
    uint32_t timeoutMs = 0;
    std::vector<std::string> rawArguments;
};

bool ParseOptions(int argc, char* argv[], Options& options)
{
    if (argc < 2) return false;
    options.targetPath = argv[1];
    for (int index = 2; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--list" || argument == "--list-callable")
        {
            if (options.action != "list") return false;
            options.action = argument.substr(2);
        }
        else if ((argument == "--describe" || argument == "--inspect") && index + 1 < argc)
        {
            if (options.action != "list") return false;
            options.action = "describe";
            options.selector = argv[++index];
        }
        else if (argument == "--call" && index + 1 < argc)
        {
            if (options.action != "list") return false;
            options.action = "call";
            options.selector = argv[++index];
        }
        else if (argument == "--arg" && index + 1 < argc)
        {
            if (options.action != "call") return false;
            options.rawArguments.push_back(argv[++index]);
        }
        else if (argument == "--timeout" && index + 1 < argc)
        {
            if (options.action != "call") return false;
            char* end = nullptr;
            errno = 0;
            const unsigned long value = std::strtoul(argv[++index], &end, 10);
            if (errno == ERANGE || end == argv[index] || *end != '\0' || value > UINT32_MAX)
                return false;
            options.timeoutMs = static_cast<uint32_t>(value);
        }
        else if (argument == "--json") options.json = true;
        else if (argument == "--jsonl") options.jsonl = true;
        else if (argument == "--shell" || argument == "--interactive") { options.jsonl = true; options.shell = true; }
        else if (argument == "--profile" && index + 1 < argc)
        {
            if (!options.profilePath.empty()) return false;
            options.profilePath = argv[++index];
        }
        else if (argument == "--prototype-override" && index + 1 < argc)
        {
            if (!options.prototypeOverridePath.empty()) return false;
            options.prototypeOverridePath = argv[++index];
        }
        else if (argument == "--symbols") options.symbols = true;
        else return false;
    }
    return true;
}
}

int main(int argc, char* argv[])
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        PrintUsage();
        return FubiExitCode::Usage;
    }

    FunctionCatalog catalog;
    std::string error;
    if (!FunctionCatalog::Load(options.targetPath, catalog, error))
    {
        std::cerr << error << "\n";
        return FubiExitCode::CatalogLoadFailed;
    }
    if (!options.profilePath.empty())
    {
        std::ifstream profileFile(options.profilePath, std::ios::binary | std::ios::ate);
        if (!profileFile) { std::cerr << "Unable to open profile: " << options.profilePath << "\n"; return FubiExitCode::ProfileLoadFailed; }
        const std::streamoff size = profileFile.tellg();
        if (size < 0 || size > 4 * 1024 * 1024) { std::cerr << "Profile exceeds the 4 MiB limit\n"; return FubiExitCode::ProfileLoadFailed; }
        std::string document(static_cast<size_t>(size), '\0');
        profileFile.seekg(0);
        if (!document.empty()) profileFile.read(&document[0], static_cast<std::streamsize>(document.size()));
        PrototypeProfile profile;
        std::vector<ProfileValidationError> profileErrors;
        if (!ParsePrototypeProfile(document, profile, profileErrors) || !catalog.ApplyProfile(profile, profileErrors))
        {
            for (const ProfileValidationError& item : profileErrors)
                std::cerr << item.code << " at " << item.path << ": " << item.message << "\n";
            return FubiExitCode::ProfileLoadFailed;
        }
    }
    if (options.symbols)
    {
        DbgHelpDll provider;
        std::vector<SymbolPrototypeEvidence> evidence;
        if (!provider.EnumerateExactFunctionSymbols(options.targetPath, catalog.Module(), evidence, error) ||
            !catalog.ApplySymbolEvidence(evidence, error))
        {
            std::cerr << error << "\n";
            return FubiExitCode::SymbolLoadFailed;
        }
    }
    if (options.jsonl)
    {
        bool negotiated = false;
        SessionReferences sessionReferences;
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            CallRequest request;
            std::vector<CallDiagnostic> diagnostics;
            const bool parsed = ParseCallRequestJson(line, request, diagnostics);
            const std::string correlationId = request.correlationId.empty() ? "jsonl-error" : request.correlationId;
            if (!parsed)
            {
                WriteSessionStatus(std::cout, request.action.empty() ? "call" : request.action,
                    correlationId, catalog, false, "validation-failed", diagnostics);
                continue;
            }
            if (request.action != "hello" && !negotiated)
            {
                WriteSessionStatus(std::cout, request.action, correlationId, catalog, false,
                    "validation-failed", {{"session-not-negotiated", "action", "hello is required before session actions"}});
                continue;
            }
            if (request.action == "hello")
            {
                negotiated = true;
                WriteSessionStatus(std::cout, "hello", correlationId, catalog, true, "ready");
            }
            else if (request.action == "list")
            {
                WriteSessionCatalog(std::cout, catalog, correlationId);
            }
            else if (request.action == "describe")
            {
                WriteSessionDescription(std::cout, catalog, correlationId, request.selector);
            }
            else if (request.action == "release")
            {
                if (!sessionReferences.Release(request.reference))
                    WriteSessionStatus(std::cout, "release", correlationId, catalog, false,
                        "validation-failed", {{"reference-not-found", "reference", "reference is unknown or already released"}});
                else
                    WriteSessionStatus(std::cout, "release", correlationId, catalog, true,
                        "released", {}, {}, request.reference);
            }
            else if (request.action == "quit")
            {
                WriteSessionStatus(std::cout, "quit", correlationId, catalog, true, "closed");
                break;
            }
            else
            {
                request.moduleSha256 = request.moduleSha256.empty() ? catalog.Module().sha256 : request.moduleSha256;
                request.modulePath = request.modulePath.empty() ? catalog.Module().canonicalPath : request.modulePath;
                request.moduleTimestamp = request.moduleTimestamp == 0 ? catalog.Module().timestamp : request.moduleTimestamp;
                request.moduleImageSize = request.moduleImageSize == 0 ? catalog.Module().imageSize : request.moduleImageSize;
                request.modulePreferredImageBase = request.modulePreferredImageBase == 0 ? catalog.Module().preferredImageBase : request.modulePreferredImageBase;
                request.modulePdbGuid = request.modulePdbGuid.empty() ? catalog.Module().pdbGuid : request.modulePdbGuid;
                request.modulePdbAge = request.modulePdbAge == 0 ? catalog.Module().pdbAge : request.modulePdbAge;
                CallResult response;
                response.action = "call";
                response.correlationId = correlationId;
                response.resolvedModule = catalog.Module();
                response.prototypeUsed = request.prototypeOverride;
                const bool valid = ValidateCallRequest(request, catalog, diagnostics);
                response.status = valid ? "not-executed" : "validation-failed";
                response.diagnostics = diagnostics;
                if (valid)
                {
                    for (const CallArgument& argument : request.arguments)
                        if (argument.type.kind == TypeKind::Pointer && argument.value.rfind("opaque:session-", 0) == 0)
                        {
                            response.status = "validation-failed";
                            response.diagnostics.push_back({"session-reference-unsupported", "arguments", "session references are not resolved into isolated worker calls"});
                            break;
                        }
                    std::string invocationError;
                    const bool referenceRejected = !response.diagnostics.empty();
                    WorkerInvocationAdapter invocationAdapter(options.targetPath,
                        catalog, true);
                    if (!referenceRejected && !DispatchCall(request, catalog,
                        invocationAdapter, response, invocationError) && !invocationError.empty())
                        response.diagnostics.push_back({"invocation-failed", "call", invocationError});
                    if (!referenceRejected && response.success && response.prototypeUsed.returnType.kind == TypeKind::Pointer)
                    {
                        uint64_t address = 0;
                        if (!ParseWorkerPointer(response.returnValue, address))
                        {
                            response.success = false;
                            response.status = "worker-failed";
                            response.returnValue.clear();
                            response.diagnostics.push_back({"pointer-result-invalid", "return_value", "worker returned an invalid pointer representation"});
                        }
                        else
                        {
                            response.returnValue = sessionReferences.Issue(address);
                            response.issuedReferences.push_back(response.returnValue);
                        }
                    }
                }
                WriteCallResultJson(std::cout, response);
                std::cout << '\n';
            }
        }
        return FubiExitCode::Success;
    }
    if (options.action == "call")
    {
        CallRequest request;
        request.selector = options.selector;
        request.correlationId = "cli-call";
        request.moduleSha256 = catalog.Module().sha256;
        request.modulePath = catalog.Module().canonicalPath;
        request.moduleTimestamp = catalog.Module().timestamp;
        request.moduleImageSize = catalog.Module().imageSize;
        request.modulePreferredImageBase = catalog.Module().preferredImageBase;
        request.modulePdbGuid = catalog.Module().pdbGuid;
        request.modulePdbAge = catalog.Module().pdbAge;
        request.timeoutMs = options.timeoutMs;
        const FunctionRecord* record = catalog.Find(options.selector);
        if (!options.prototypeOverridePath.empty())
        {
            std::ifstream overrideFile(options.prototypeOverridePath, std::ios::binary | std::ios::ate);
            if (!overrideFile) { std::cerr << "Unable to open prototype override\n"; return FubiExitCode::ProfileLoadFailed; }
            const std::streamoff size = overrideFile.tellg();
            if (size < 0 || size > 4 * 1024 * 1024) { std::cerr << "Prototype override exceeds the 4 MiB limit\n"; return FubiExitCode::ProfileLoadFailed; }
            std::string document(static_cast<size_t>(size), '\0'); overrideFile.seekg(0); if (!document.empty()) overrideFile.read(&document[0], static_cast<std::streamsize>(document.size()));
            PrototypeProfile overrideProfile; std::vector<ProfileValidationError> overrideErrors;
            if (!ParsePrototypeProfile(document, overrideProfile, overrideErrors)) { for (const auto& item : overrideErrors) std::cerr << item.code << " at " << item.path << ": " << item.message << "\n"; return FubiExitCode::ProfileLoadFailed; }
            if (record == nullptr) { std::cerr << "Function selector not found or ambiguous\n"; return FubiExitCode::ValidationFailed; }
            for (const auto& item : overrideProfile.functions) if (item.rva == record->startRva) { request.hasPrototypeOverride = true; request.prototypeOverride = item.prototype; break; }
            if (!request.hasPrototypeOverride) { std::cerr << "Prototype override has no matching function\n"; return FubiExitCode::ValidationFailed; }
        }
        if (!request.hasPrototypeOverride && record != nullptr && record->hasPrototype)
        {
            request.hasPrototypeOverride = true;
            request.prototypeOverride = record->prototype;
        }
        const PrototypeSpec* argumentPrototype = request.hasPrototypeOverride ? &request.prototypeOverride : (record != nullptr && record->hasPrototype ? &record->prototype : nullptr);
        if (argumentPrototype != nullptr && argumentPrototype->parameters.size() == options.rawArguments.size())
        {
            for (size_t index = 0; index < options.rawArguments.size(); ++index)
            {
                const std::string& raw = options.rawArguments[index];
                const size_t separator = raw.find(':');
                if (separator == std::string::npos) { CallResult malformed; malformed.correlationId="cli-call"; malformed.status="validation-failed"; malformed.diagnostics.push_back({"invalid-argument-syntax","arguments","--arg requires kind:value"}); if(options.json) WriteCallResultJson(std::cout,malformed); else std::cerr << "--arg requires kind:value\n"; return FubiExitCode::ValidationFailed; }
                CallArgument argument;
                argument.type = argumentPrototype->parameters[index];
                const std::string kind = raw.substr(0, separator);
                if (kind != TypeKindName(argument.type.kind)) { CallResult malformed; malformed.correlationId="cli-call"; malformed.status="validation-failed"; malformed.diagnostics.push_back({"argument-type-mismatch","arguments","argument type does not match prototype"}); if(options.json) WriteCallResultJson(std::cout,malformed); else std::cerr << "argument type does not match prototype\n"; return FubiExitCode::ValidationFailed; }
                argument.value = raw.substr(separator + 1);
                request.arguments.push_back(std::move(argument));
            }
        }
        // The isolated worker reloads the module independently, so forward
        // catalog-owned profile evidence as an explicit invocation contract.
        if (!request.hasPrototypeOverride && record != nullptr && record->hasPrototype)
        {
            request.hasPrototypeOverride = true;
            request.prototypeOverride = record->prototype;
        }
        std::vector<CallDiagnostic> diagnostics;
        const bool valid = ValidateCallRequest(request, catalog, diagnostics);
        CallResult result;
        result.correlationId = request.correlationId;
        result.resolvedModule = catalog.Module();
        if (record != nullptr && record->hasPrototype) result.prototypeUsed = record->prototype;
        result.success = false;
        result.status = valid ? "not-executed" : "validation-failed";
        result.diagnostics = diagnostics;
        if (!valid)
        {
            if (options.json) WriteCallResultJson(std::cout, result);
            else WriteCallResultText(std::cout, result);
            for (const CallDiagnostic& item : diagnostics) std::cerr << item.code << " at " << item.path << ": " << item.message << "\n";
            return FubiExitCode::ValidationFailed;
        }
        std::string invocationError;
        WorkerInvocationAdapter invocationAdapter(options.targetPath, catalog);
        if (!DispatchCall(request, catalog, invocationAdapter, result,
            invocationError))
        {
            if (!invocationError.empty()) result.diagnostics.push_back({"invocation-failed", "call", invocationError});
            if (options.json) WriteCallResultJson(std::cout, result);
            else WriteCallResultText(std::cout, result);
            for (const CallDiagnostic& item : result.diagnostics) std::cerr << item.code << " at " << item.path << ": " << item.message << "\n";
            return FubiExitCode::InvocationFailed;
        }
        if (options.json) WriteCallResultJson(std::cout, result);
        else WriteCallResultText(std::cout, result);
        return FubiExitCode::Success;
    }
    if (options.action == "describe")
    {
        const std::vector<const FunctionRecord*> matches = catalog.FindAll(options.selector);
        if (matches.empty())
        {
            std::cerr << "Function selector not found: " << options.selector << "\n";
            return FubiExitCode::SelectorNotFound;
        }
        if (matches.size() > 1)
        {
            std::cerr << "Ambiguous function selector: " << options.selector << "\n";
            for (const FunctionRecord* candidate : matches)
                std::cerr << "  rva=0x" << std::hex << std::uppercase << candidate->startRva
                          << std::dec << " name=" << candidate->displayName << "\n";
            return FubiExitCode::SelectorAmbiguous;
        }
        const FunctionRecord* record = matches.front();
        if (options.json)
        {
            catalog.WriteJsonDescribe(std::cout, *record);
            return FubiExitCode::Success;
        }
        std::cout << "FuBi function description\n"
                  << "schema_version = " << FunctionCatalog::kSchemaVersion << "\n"
                  << "module = " << catalog.Module().canonicalPath << "\n"
                  << "rva = 0x" << std::hex << std::uppercase << record->startRva << std::dec << "\n"
                  << "name = " << record->displayName << "\n"
                  << "callability = " << CallabilityName(record->callability) << "\n"
                  << "reason = " << CallabilityReason(record->callability) << "\n";
    }
    else if (options.json) catalog.WriteJson(std::cout, options.action == "list-callable");
    else catalog.WriteText(std::cout, options.action == "list-callable");
    return FubiExitCode::Success;
}
