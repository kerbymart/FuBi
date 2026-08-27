#include "stdafx.h"

#include "FunctionCatalog.h"
#include "PrototypeProfile.h"

#include <fstream>
#include <iostream>
#include <string>

namespace
{
void PrintUsage()
{
    std::cerr << "Usage:\n"
              << "  Fubi.exe <dll-file> [--list|--list-callable|--describe <name|#ordinal|0xRVA>] [--profile <file>] [--json]\n";
}

struct Options
{
    std::string targetPath;
    std::string action = "list";
    std::string selector;
    bool json = false;
    std::string profilePath;
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
        else if (argument == "--json") options.json = true;
        else if (argument == "--profile" && index + 1 < argc)
        {
            if (!options.profilePath.empty()) return false;
            options.profilePath = argv[++index];
        }
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
