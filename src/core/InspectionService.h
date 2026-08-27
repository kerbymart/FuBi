#pragma once

#include "PEImage.h"
#include "StaticExportCatalog.h"
#include "WindowsPatternCatalog.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

struct InspectionImport
{
    std::string name;
    uint16_t hint = 0;
    uint16_t ordinal = 0;
    bool byOrdinal = false;
    uint32_t thunkRva = 0;
    uint32_t iatRva = 0;
};

struct InspectionImportModule
{
    std::string name;
    std::vector<InspectionImport> symbols;
};

struct InspectionRuntimeFunction
{
    uint32_t beginRva = 0;
    uint32_t endRva = 0;
    uint32_t unwindRva = 0;
};

struct InspectionDebugEntry
{
    uint32_t type = 0;
    uint32_t timestamp = 0;
    uint32_t size = 0;
    std::string codeViewFormat;
    std::string pdbGuid;
    uint32_t pdbAge = 0;
    std::string pdbPath;
};

struct InspectionReport
{
    static constexpr uint32_t kSchemaVersion = 1;

    std::string mode;
    std::string path;
    std::string architecture;
    uint32_t timestamp = 0;
    uint32_t imageSize = 0;
    std::vector<StaticExport> exports;
    std::vector<InspectionImportModule> imports;
    std::vector<InspectionImportModule> delayImports;
    std::vector<InspectionRuntimeFunction> runtimeFunctions;
    std::vector<InspectionDebugEntry> debugEntries;
    std::vector<WindowsPatternEvidence> wdfBindings;
    std::vector<std::string> warnings;
};

class InspectionService
{
public:
    static bool IsSupportedMode(const std::string& mode);
    static bool Inspect(const std::string& path, const std::string& mode,
        InspectionReport& report, std::string& error);

    static void WriteText(std::ostream& output, const InspectionReport& report);
    static void WriteJson(std::ostream& output, const InspectionReport& report);
};
