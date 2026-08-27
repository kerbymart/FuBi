#pragma once

#include "PEAnalysis.h"

#include <iosfwd>
#include <string>

class AnalysisReport
{
public:
    static void WriteText(std::ostream& output, const PEAnalysis& analysis);
    static void WriteStrings(std::ostream& output, const PEAnalysis& analysis);
    static bool WriteTextFile(
        const std::string& path, const PEAnalysis& analysis, std::string& error);
    static bool WriteJsonFile(
        const std::string& path, const PEAnalysis& analysis, std::string& error);
    static bool WriteFunctionReport(
        std::ostream& output, const PEAnalysis& analysis,
        const std::string& nameOrRva, std::string& error);
    static bool WriteDisassembly(
        std::ostream& output, const PEAnalysis& analysis,
        const std::string& nameOrRva, size_t maximumBytes, std::string& error);
    static bool WriteCallers(
        std::ostream& output, const PEAnalysis& analysis,
        const std::string& nameOrRva, std::string& error);
    static bool WriteCallees(
        std::ostream& output, const PEAnalysis& analysis,
        const std::string& nameOrRva, std::string& error);
    static bool WriteStringXrefs(
        std::ostream& output, const PEAnalysis& analysis, const std::string& value);
    static bool WriteImportXrefs(
        std::ostream& output, const PEAnalysis& analysis, const std::string& name);
};
