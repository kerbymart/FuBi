#include "stdafx.h"

#include "FunctionCatalog.h"
#include "PrototypeProfile.h"
#include "DbgHelpDll.h"
#include "CallContract.h"

#include <fstream>
#include <iostream>
#include <string>

namespace
{
void PrintUsage()
{
    std::cerr << "Usage:\n"
              << "  Fubi.exe <dll-file> [--list|--list-callable|--describe <name|#ordinal|0xRVA>|--call <selector>] [--arg <kind:value> ...] [--profile <file>] [--prototype-override <file>] [--symbols] [--json]\n";
}

struct Options
{
    std::string targetPath;
    std::string action = "list";
    std::string selector;
    bool json = false;
    std::string profilePath;
    std::string prototypeOverridePath;
    bool symbols = false;
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
        else if (argument == "--describe" && index + 1 < argc)
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
        else if (argument == "--json") options.json = true;
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
        return 2;
    }

    FunctionCatalog catalog;
    std::string error;
    if (!FunctionCatalog::Load(options.targetPath, catalog, error))
    {
        std::cerr << error << "\n";
        return 3;
    }
    if (!options.profilePath.empty())
    {
        std::ifstream profileFile(options.profilePath, std::ios::binary | std::ios::ate);
        if (!profileFile) { std::cerr << "Unable to open profile: " << options.profilePath << "\n"; return 6; }
        const std::streamoff size = profileFile.tellg();
        if (size < 0 || size > 4 * 1024 * 1024) { std::cerr << "Profile exceeds the 4 MiB limit\n"; return 6; }
        std::string document(static_cast<size_t>(size), '\0');
        profileFile.seekg(0);
        if (!document.empty()) profileFile.read(&document[0], static_cast<std::streamsize>(document.size()));
        PrototypeProfile profile;
        std::vector<ProfileValidationError> profileErrors;
        if (!ParsePrototypeProfile(document, profile, profileErrors) || !catalog.ApplyProfile(profile, profileErrors))
        {
            for (const ProfileValidationError& item : profileErrors)
                std::cerr << item.code << " at " << item.path << ": " << item.message << "\n";
            return 6;
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
            return 7;
        }
    }
    if (options.action == "call")
    {
        CallRequest request;
        request.selector = options.selector;
        request.correlationId = "cli-call";
        request.moduleSha256 = catalog.Module().sha256;
        const FunctionRecord* record = catalog.Find(options.selector);
        if (!options.prototypeOverridePath.empty())
        {
            std::ifstream overrideFile(options.prototypeOverridePath, std::ios::binary | std::ios::ate);
            if (!overrideFile) { std::cerr << "Unable to open prototype override\n"; return 6; }
            const std::streamoff size = overrideFile.tellg();
            if (size < 0 || size > 4 * 1024 * 1024) { std::cerr << "Prototype override exceeds the 4 MiB limit\n"; return 6; }
            std::string document(static_cast<size_t>(size), '\0'); overrideFile.seekg(0); if (!document.empty()) overrideFile.read(&document[0], static_cast<std::streamsize>(document.size()));
            PrototypeProfile overrideProfile; std::vector<ProfileValidationError> overrideErrors;
            if (!ParsePrototypeProfile(document, overrideProfile, overrideErrors)) { for (const auto& item : overrideErrors) std::cerr << item.code << " at " << item.path << ": " << item.message << "\n"; return 6; }
            if (record == nullptr) { std::cerr << "Function selector not found or ambiguous\n"; return 8; }
            for (const auto& item : overrideProfile.functions) if (item.rva == record->startRva) { request.hasPrototypeOverride = true; request.prototypeOverride = item.prototype; break; }
            if (!request.hasPrototypeOverride) { std::cerr << "Prototype override has no matching function\n"; return 8; }
        }
        if (record != nullptr && record->hasPrototype && record->prototype.parameters.size() == options.rawArguments.size())
        {
            for (size_t index = 0; index < options.rawArguments.size(); ++index)
            {
                const std::string& raw = options.rawArguments[index];
                const size_t separator = raw.find(':');
                if (separator == std::string::npos) { CallResult malformed; malformed.correlationId="cli-call"; malformed.status="validation-failed"; malformed.diagnostics.push_back({"invalid-argument-syntax","arguments","--arg requires kind:value"}); if(options.json) WriteCallResultJson(std::cout,malformed); else std::cerr << "--arg requires kind:value\n"; return 8; }
                CallArgument argument;
                argument.type = record->prototype.parameters[index];
                const std::string kind = raw.substr(0, separator);
                if (kind != TypeKindName(argument.type.kind)) { std::cerr << "argument type does not match prototype\n"; return 8; }
                argument.value = raw.substr(separator + 1);
                request.arguments.push_back(std::move(argument));
            }
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
        if (options.json) WriteCallResultJson(std::cout, result); else for (const CallDiagnostic& item : diagnostics) std::cerr << item.code << " at " << item.path << ": " << item.message << "\n";
        return valid ? 9 : 8;
    }
    if (options.action == "describe")
    {
        const std::vector<const FunctionRecord*> matches = catalog.FindAll(options.selector);
        if (matches.empty())
        {
            std::cerr << "Function selector not found: " << options.selector << "\n";
            return 4;
        }
        if (matches.size() > 1)
        {
            std::cerr << "Ambiguous function selector: " << options.selector << "\n";
            for (const FunctionRecord* candidate : matches)
                std::cerr << "  rva=0x" << std::hex << std::uppercase << candidate->startRva
                          << std::dec << " name=" << candidate->displayName << "\n";
            return 5;
        }
        const FunctionRecord* record = matches.front();
        if (options.json)
        {
            catalog.WriteJsonDescribe(std::cout, *record);
            return 0;
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
    return 0;
}
