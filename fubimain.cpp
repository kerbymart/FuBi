#include "stdafx.h"

#include "AnalysisReport.h"
#include "Fubi.h"
#include "PEAnalyzer.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
struct Options
{
    std::string targetPath;
    std::string dumpPath;
    std::string jsonPath;
    std::string disassemblyTarget;
    std::string functionReportTarget;
    std::string callersTarget;
    std::string calleesTarget;
    std::string xrefsTarget;
    std::string stringXrefTarget;
    std::string importXrefTarget;
    size_t minimumStringLength = 5;
    size_t disassemblyBytes = 0;
    bool analyzeToConsole = false;
    bool stringsRequested = false;
    bool interactive = false;
};

void PrintUsage()
{
    std::cerr
        << "Usage:\n"
        << "  Fubi.exe <pe-file> --analyze\n"
        << "  Fubi.exe <pe-file> --dump <report.txt> [--json <report.json>]\n"
        << "  Fubi.exe <pe-file> --strings [--min-string-length N]\n"
        << "  Fubi.exe <pe-file> --disasm-function <name-or-rva>\n"
        << "  Fubi.exe <pe-file> --function-report <name-or-rva>\n"
        << "  Fubi.exe <pe-file> --callers|--callees|--xrefs <name-or-rva>\n"
        << "  Fubi.exe <pe-file> --xrefs-string <value>\n"
        << "  Fubi.exe <pe-file> --xrefs-import <name>\n"
        << "  Fubi.exe <dll-file> --interactive\n";
}

bool ParsePositiveSize(const char* text, size_t& value)
{
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (text == end || *end != '\0' || parsed < 2) return false;
    value = parsed;
    return true;
}

bool ParseOptions(int argc, char* argv[], Options& options)
{
    if (argc < 2) return false;
    options.targetPath = argv[1];

    for (int index = 2; index < argc; ++index)
    {
        const std::string option = argv[index];
        if (option == "--analyze") options.analyzeToConsole = true;
        else if (option == "--strings") options.stringsRequested = true;
        else if (option == "--interactive") options.interactive = true;
        else if (option == "--no-interactive") options.interactive = false;
        else if (option == "--dump" && index + 1 < argc) options.dumpPath = argv[++index];
        else if (option == "--json" && index + 1 < argc) options.jsonPath = argv[++index];
        else if ((option == "--disasm" || option == "--disasm-function") && index + 1 < argc)
            options.disassemblyTarget = argv[++index];
        else if (option == "--function-report" && index + 1 < argc)
            options.functionReportTarget = argv[++index];
        else if (option == "--callers" && index + 1 < argc) options.callersTarget = argv[++index];
        else if (option == "--callees" && index + 1 < argc) options.calleesTarget = argv[++index];
        else if (option == "--xrefs" && index + 1 < argc) options.xrefsTarget = argv[++index];
        else if (option == "--xrefs-string" && index + 1 < argc)
            options.stringXrefTarget = argv[++index];
        else if (option == "--xrefs-import" && index + 1 < argc)
            options.importXrefTarget = argv[++index];
        else if (option == "--disasm-bytes" && index + 1 < argc)
        {
            if (!ParsePositiveSize(argv[++index], options.disassemblyBytes)) return false;
        }
        else if (option == "--min-string-length" && index + 1 < argc)
        {
            if (!ParsePositiveSize(argv[++index], options.minimumStringLength)) return false;
        }
        else return false;
    }

    if (options.interactive &&
        (options.analyzeToConsole || options.stringsRequested ||
         !options.dumpPath.empty() || !options.jsonPath.empty() ||
         !options.disassemblyTarget.empty() || !options.functionReportTarget.empty() ||
         !options.callersTarget.empty() || !options.calleesTarget.empty() ||
         !options.xrefsTarget.empty() || !options.stringXrefTarget.empty() ||
         !options.importXrefTarget.empty()))
    {
        return false;
    }
    return true;
}

int RunInteractive(const std::string& targetPath)
{
    HMODULE module = LoadLibraryA(targetPath.c_str());
    if (!module)
    {
        std::cerr << "Unable to load DLL for interactive mode. Windows error: "
                  << GetLastError() << "\n";
        return 5;
    }

    Fubi fubi;
    if (!fubi.sys.ImportBindings(module))
    {
        std::cerr << "Unable to read the loaded DLL export table.\n";
        FreeLibrary(module);
        return 3;
    }
    fubi.sys.PrintFunctionInfo();

    std::cout << "Interactive mode executes target DLL code.\n"
              << "Enter an exported function name, or q to quit:\n";
    std::string name;
    while (std::getline(std::cin, name))
    {
        if (name.empty() || name[0] == 'q' || name[0] == 'Q') break;
        const Result result = fubi.Call_function(name, nullptr);
        std::cout << "RESULT (" << result.res_type << ") = " << result.res << "\n";
    }

    FreeLibrary(module);
    return 0;
}

int RunStaticAnalysis(const Options& options)
{
    PEAnalysis analysis;
    std::string error;
    const PEAnalyzer analyzer(options.minimumStringLength);
    if (!analyzer.AnalyzeFile(options.targetPath, analysis, error))
    {
        std::cerr << error << "\n";
        return 3;
    }

    const bool hasQuery = !options.disassemblyTarget.empty() ||
        !options.functionReportTarget.empty() || !options.callersTarget.empty() ||
        !options.calleesTarget.empty() || !options.xrefsTarget.empty() ||
        !options.stringXrefTarget.empty() || !options.importXrefTarget.empty();
    const bool noExplicitOutput = !options.analyzeToConsole &&
        !options.stringsRequested && options.dumpPath.empty() && options.jsonPath.empty() &&
        !hasQuery;
    if (options.analyzeToConsole || noExplicitOutput)
        AnalysisReport::WriteText(std::cout, analysis);
    else if (options.stringsRequested)
        AnalysisReport::WriteStrings(std::cout, analysis);

    if (!options.dumpPath.empty())
    {
        if (!AnalysisReport::WriteTextFile(options.dumpPath, analysis, error))
        {
            std::cerr << error << "\n";
            return 4;
        }
        std::cout << "Wrote static analysis report: " << options.dumpPath << "\n";
    }

    if (!options.jsonPath.empty())
    {
        if (!AnalysisReport::WriteJsonFile(options.jsonPath, analysis, error))
        {
            std::cerr << error << "\n";
            return 4;
        }
        std::cout << "Wrote JSON analysis report: " << options.jsonPath << "\n";
    }

    if (!options.disassemblyTarget.empty() &&
        !AnalysisReport::WriteDisassembly(
            std::cout, analysis, options.disassemblyTarget,
            options.disassemblyBytes, error))
    {
        std::cerr << error << "\n";
        return 6;
    }
    if (!options.functionReportTarget.empty() &&
        !AnalysisReport::WriteFunctionReport(
            std::cout, analysis, options.functionReportTarget, error))
    {
        std::cerr << error << "\n";
        return 6;
    }
    if (!options.callersTarget.empty() &&
        !AnalysisReport::WriteCallers(std::cout, analysis, options.callersTarget, error))
    {
        std::cerr << error << "\n";
        return 6;
    }
    if (!options.calleesTarget.empty() &&
        !AnalysisReport::WriteCallees(std::cout, analysis, options.calleesTarget, error))
    {
        std::cerr << error << "\n";
        return 6;
    }
    if (!options.xrefsTarget.empty())
    {
        if (!AnalysisReport::WriteCallers(std::cout, analysis, options.xrefsTarget, error) ||
            !AnalysisReport::WriteCallees(std::cout, analysis, options.xrefsTarget, error))
        {
            std::cerr << error << "\n";
            return 6;
        }
    }
    if (!options.stringXrefTarget.empty())
        AnalysisReport::WriteStringXrefs(std::cout, analysis, options.stringXrefTarget);
    if (!options.importXrefTarget.empty())
        AnalysisReport::WriteImportXrefs(std::cout, analysis, options.importXrefTarget);
    return 0;
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
    return options.interactive
        ? RunInteractive(options.targetPath)
        : RunStaticAnalysis(options);
}
