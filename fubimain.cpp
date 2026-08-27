#include "stdafx.h"

#include "StaticExportCatalog.h"

#include <iostream>
#include <string>

namespace
{
void PrintUsage()
{
    std::cerr << "Usage:\n"
              << "  Fubi.exe <dll-file> [--list]\n";
}

bool ParseOptions(int argc, char* argv[], std::string& targetPath)
{
    if (argc < 2 || argc > 3) return false;
    targetPath = argv[1];
    return argc == 2 || std::string(argv[2]) == "--list";
}
}

int main(int argc, char* argv[])
{
    std::string targetPath;
    if (!ParseOptions(argc, argv, targetPath))
    {
        PrintUsage();
        return 2;
    }

    StaticExportCatalog catalog;
    std::string error;
    if (!StaticExportCatalog::Load(targetPath, catalog, error))
    {
        std::cerr << error << "\n";
        return 3;
    }
    catalog.WriteText(std::cout);
    return 0;
}
