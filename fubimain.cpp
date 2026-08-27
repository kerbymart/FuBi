#include "stdafx.h"

#include "FunctionCatalog.h"

#include <iostream>
#include <string>

namespace
{
void PrintUsage()
{
    std::cerr << "Usage:\n"
              << "  Fubi.exe <dll-file> [--list|--list-callable|--describe <name|#ordinal|0xRVA>] [--json]\n";
}

struct Options
{
    std::string targetPath;
    std::string action = "list";
    std::string selector;
    bool json = false;
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
    if (options.action == "describe")
    {
        const FunctionRecord* record = catalog.Find(options.selector);
        if (record == nullptr)
        {
            std::cerr << "Function selector not found: " << options.selector << "\n";
            return 4;
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
