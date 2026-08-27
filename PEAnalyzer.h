#pragma once

#include "PEAnalysis.h"

#include <cstddef>
#include <string>

class PEAnalyzer
{
public:
    explicit PEAnalyzer(size_t minimumStringLength = 5);

    bool AnalyzeFile(
        const std::string& path, PEAnalysis& analysis, std::string& error) const;
    bool AnalyzeImage(
        const PEImage& image, PEAnalysis& analysis, std::string& error) const;

private:
    void ParseExports(const PEImage& image, PEAnalysis& analysis) const;
    void ParseImports(const PEImage& image, PEAnalysis& analysis) const;
    void ParseDelayImports(const PEImage& image, PEAnalysis& analysis) const;
    void ParseStrings(const PEImage& image, PEAnalysis& analysis) const;
    void ParseDebug(const PEImage& image, PEAnalysis& analysis) const;
    void ParseRuntimeFunctions(const PEImage& image, PEAnalysis& analysis) const;
    void ParseFrameworkBindings(const PEImage& image, PEAnalysis& analysis) const;
    void DisassembleFunctions(const PEImage& image, PEAnalysis& analysis) const;
    void BuildCallGraph(PEAnalysis& analysis) const;
    void ParseLoadConfig(const PEImage& image, PEAnalysis& analysis) const;
    void ParseResources(const PEImage& image, PEAnalysis& analysis) const;
    void ParseVersionInfo(const std::string& path, PEAnalysis& analysis) const;
    void Classify(PEAnalysis& analysis) const;

    size_t minimumStringLength_;
};
