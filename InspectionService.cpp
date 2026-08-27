#include "stdafx.h"
#include "InspectionService.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>

namespace
{
constexpr size_t kMaximumDirectoryRecords = 100000;
constexpr size_t kMaximumThunkRecords = 100000;
constexpr size_t kMaximumStringBytes = 4096;

const PeDataDirectory* Directory(const PEImage& image, uint32_t index)
{
    for (const PeDataDirectory& item : image.Headers().dataDirectories)
        if (item.index == index && item.rva != 0 && item.size != 0) return &item;
    return nullptr;
}

bool AddRva(uint32_t base, uint64_t delta, uint32_t& result)
{
    if (delta > UINT32_MAX || static_cast<uint64_t>(base) + delta > UINT32_MAX)
        return false;
    result = base + static_cast<uint32_t>(delta);
    return true;
}

bool ReadThunkTable(const PEImage& image, uint32_t lookupRva, uint32_t iatRva,
    InspectionImportModule& module, std::string& warning)
{
    const uint32_t entrySize = image.Headers().isPe32Plus ? sizeof(uint64_t) : sizeof(uint32_t);
    const uint64_t ordinalFlag = image.Headers().isPe32Plus ? IMAGE_ORDINAL_FLAG64 : IMAGE_ORDINAL_FLAG32;
    const size_t maximum = std::min(kMaximumThunkRecords,
        image.Bytes().size() / entrySize + 1);
    for (size_t index = 0; index < maximum; ++index)
    {
        uint32_t entryRva = 0;
        uint32_t iatEntryRva = 0;
        if (!AddRva(lookupRva, static_cast<uint64_t>(index) * entrySize, entryRva) ||
            !AddRva(iatRva, static_cast<uint64_t>(index) * entrySize, iatEntryRva))
        {
            warning = "Import thunk RVA arithmetic overflow for " + module.name;
            return false;
        }
        uint64_t value = 0;
        if (image.Headers().isPe32Plus)
        {
            if (!image.ReadRva(entryRva, value))
            {
                warning = "Truncated import thunk table for " + module.name;
                return false;
            }
        }
        else
        {
            uint32_t value32 = 0;
            if (!image.ReadRva(entryRva, value32))
            {
                warning = "Truncated import thunk table for " + module.name;
                return false;
            }
            value = value32;
        }
        if (value == 0) return true;

        InspectionImport symbol;
        symbol.thunkRva = entryRva;
        symbol.iatRva = iatEntryRva;
        if ((value & ordinalFlag) != 0)
        {
            symbol.byOrdinal = true;
            symbol.ordinal = static_cast<uint16_t>(value & 0xffff);
        }
        else if (value <= UINT32_MAX)
        {
            const uint32_t nameRva = static_cast<uint32_t>(value);
            if (!image.ReadRva(nameRva, symbol.hint) ||
                !AddRva(nameRva, sizeof(uint16_t), symbol.thunkRva) ||
                !image.ReadCStringAtRva(symbol.thunkRva, symbol.name, kMaximumStringBytes))
            {
                warning = "Invalid import-by-name entry for " + module.name;
                return false;
            }
            symbol.thunkRva = entryRva;
        }
        else
        {
            warning = "Invalid import name RVA for " + module.name;
            return false;
        }
        module.symbols.push_back(std::move(symbol));
    }
    warning = "Import thunk table exceeded the safety limit for " + module.name;
    return false;
}

bool ParseImports(const PEImage& image, uint32_t directoryIndex,
    std::vector<InspectionImportModule>& modules, std::vector<std::string>& warnings)
{
    const PeDataDirectory* directory = Directory(image, directoryIndex);
    if (directory == nullptr) return true;
    const size_t maximum = std::min(kMaximumDirectoryRecords,
        static_cast<size_t>(directory->size) / sizeof(IMAGE_IMPORT_DESCRIPTOR) + 1);
    for (size_t index = 0; index < maximum; ++index)
    {
        uint32_t descriptorRva = 0;
        if (!AddRva(directory->rva, static_cast<uint64_t>(index) * sizeof(IMAGE_IMPORT_DESCRIPTOR), descriptorRva))
        {
            warnings.push_back("Import directory RVA arithmetic overflow");
            return false;
        }
        IMAGE_IMPORT_DESCRIPTOR descriptor = {};
        if (!image.ReadRva(descriptorRva, descriptor))
        {
            warnings.push_back("Truncated import directory");
            return false;
        }
        if (descriptor.Name == 0 && descriptor.FirstThunk == 0 && descriptor.OriginalFirstThunk == 0)
            return true;
        InspectionImportModule module;
        if (!image.ReadCStringAtRva(descriptor.Name, module.name, kMaximumStringBytes))
        {
            warnings.push_back("Invalid imported module name");
            return false;
        }
        const uint32_t lookup = descriptor.OriginalFirstThunk != 0 ? descriptor.OriginalFirstThunk : descriptor.FirstThunk;
        std::string warning;
        ReadThunkTable(image, lookup, descriptor.FirstThunk, module, warning);
        if (!warning.empty()) warnings.push_back(std::move(warning));
        modules.push_back(std::move(module));
    }
    warnings.push_back("Import directory exceeded the safety limit");
    return false;
}

void ParseRuntimeFunctions(const PEImage& image, InspectionReport& report)
{
    const PeDataDirectory* directory = Directory(image, IMAGE_DIRECTORY_ENTRY_EXCEPTION);
    struct RawRuntimeFunction
    {
        uint32_t beginAddress;
        uint32_t endAddress;
        uint32_t unwindInfoAddress;
    };
    if (directory == nullptr || directory->size < sizeof(RawRuntimeFunction)) return;
    const size_t count = std::min(kMaximumDirectoryRecords,
        static_cast<size_t>(directory->size) / sizeof(RawRuntimeFunction));
    for (size_t index = 0; index < count; ++index)
    {
        uint32_t entryRva = 0;
        if (!AddRva(directory->rva, static_cast<uint64_t>(index) * sizeof(RawRuntimeFunction), entryRva))
        {
            report.warnings.push_back("Runtime-function RVA arithmetic overflow");
            return;
        }
        RawRuntimeFunction entry = {};
        if (!image.ReadRva(entryRva, entry))
        {
            report.warnings.push_back("Truncated runtime-function directory");
            return;
        }
        if (entry.beginAddress == 0 && entry.endAddress == 0 && entry.unwindInfoAddress == 0) continue;
        if (entry.endAddress < entry.beginAddress ||
            (image.Headers().imageSize != 0 && entry.endAddress > image.Headers().imageSize))
        {
            report.warnings.push_back("Invalid runtime-function boundary");
            continue;
        }
        report.runtimeFunctions.push_back({entry.beginAddress, entry.endAddress, entry.unwindInfoAddress});
    }
}

std::string GuidString(const GUID& guid)
{
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0')
        << std::setw(8) << guid.Data1 << '-' << std::setw(4) << guid.Data2 << '-'
        << std::setw(4) << guid.Data3 << '-' << std::setw(2)
        << static_cast<unsigned>(guid.Data4[0]) << std::setw(2)
        << static_cast<unsigned>(guid.Data4[1]) << '-';
    for (size_t index = 2; index < 8; ++index)
        output << std::setw(2) << static_cast<unsigned>(guid.Data4[index]);
    return output.str();
}

void ParseDebug(const PEImage& image, InspectionReport& report)
{
    const PeDataDirectory* directory = Directory(image, IMAGE_DIRECTORY_ENTRY_DEBUG);
    if (directory == nullptr) return;
    const size_t count = std::min(kMaximumDirectoryRecords,
        static_cast<size_t>(directory->size) / sizeof(IMAGE_DEBUG_DIRECTORY));
    for (size_t index = 0; index < count; ++index)
    {
        uint32_t entryRva = 0;
        if (!AddRva(directory->rva, static_cast<uint64_t>(index) * sizeof(IMAGE_DEBUG_DIRECTORY), entryRva))
        {
            report.warnings.push_back("Debug-directory RVA arithmetic overflow");
            return;
        }
        IMAGE_DEBUG_DIRECTORY raw = {};
        if (!image.ReadRva(entryRva, raw))
        {
            report.warnings.push_back("Truncated debug directory");
            return;
        }
        InspectionDebugEntry entry;
        entry.type = raw.Type;
        entry.timestamp = raw.TimeDateStamp;
        entry.size = raw.SizeOfData;
        if (raw.Type == IMAGE_DEBUG_TYPE_CODEVIEW && raw.PointerToRawData != 0 && raw.SizeOfData >= 24)
        {
            uint32_t signature = 0;
            GUID guid = {};
            uint32_t age = 0;
            if (image.ReadFile(raw.PointerToRawData, signature) && signature == 0x53445352 &&
                image.ReadFile(static_cast<uint64_t>(raw.PointerToRawData) + 4, guid) &&
                image.ReadFile(static_cast<uint64_t>(raw.PointerToRawData) + 20, age))
            {
                entry.codeViewFormat = "RSDS";
                entry.pdbGuid = GuidString(guid);
                entry.pdbAge = age;
                const uint64_t pathOffset = static_cast<uint64_t>(raw.PointerToRawData) + 24;
                if (pathOffset <= UINT32_MAX)
                    image.ReadCStringAtFileOffset(static_cast<uint32_t>(pathOffset), entry.pdbPath, kMaximumStringBytes);
            }
        }
        report.debugEntries.push_back(std::move(entry));
    }
}

void JsonString(std::ostream& output, const std::string& value)
{
    output << '"';
    for (const unsigned char character : value)
    {
        if (character == '"' || character == '\\') output << '\\' << character;
        else if (character == '\n') output << "\\n";
        else if (character == '\r') output << "\\r";
        else if (character == '\t') output << "\\t";
        else if (character < 0x20) output << "\\u00" << std::hex << std::setw(2)
            << std::setfill('0') << static_cast<unsigned>(character) << std::dec << std::setfill(' ');
        else output << character;
    }
    output << '"';
}

void JsonImportModules(std::ostream& output, const std::vector<InspectionImportModule>& modules)
{
    output << '[';
    for (size_t moduleIndex = 0; moduleIndex < modules.size(); ++moduleIndex)
    {
        if (moduleIndex != 0) output << ',';
        output << "{\"name\":"; JsonString(output, modules[moduleIndex].name);
        output << ",\"symbols\":[";
        for (size_t index = 0; index < modules[moduleIndex].symbols.size(); ++index)
        {
            if (index != 0) output << ',';
            const InspectionImport& item = modules[moduleIndex].symbols[index];
            output << "{\"by_ordinal\":" << (item.byOrdinal ? "true" : "false")
                << ",\"name\":"; if (item.byOrdinal) output << "null"; else JsonString(output, item.name);
            output << ",\"ordinal\":" << item.ordinal << ",\"hint\":" << item.hint
                << ",\"thunk_rva\":" << item.thunkRva << ",\"iat_rva\":" << item.iatRva << '}';
        }
        output << "]}";
    }
    output << ']';
}

std::string Hex(uint32_t value)
{
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << value;
    return output.str();
}
}

bool InspectionService::Inspect(const std::string& path, const std::string& mode,
    InspectionReport& report, std::string& error)
{
    if (!IsSupportedMode(mode))
    {
        error = "unsupported inspect mode: " + mode;
        return false;
    }
    report = {};
    report.mode = mode;
    report.path = path;
    PEImage image;
    if (!PEImage::Load(path, image, error)) return false;
    report.architecture = image.Headers().isPe32Plus ? "x64" : "x86";
    report.timestamp = image.Headers().timestamp;
    report.imageSize = image.Headers().imageSize;

    if (mode == "exports")
    {
        StaticExportCatalog catalog;
        if (!StaticExportCatalog::Load(path, catalog, error)) return false;
        report.exports = catalog.Exports();
    }
    else if (mode == "imports")
    {
        ParseImports(image, IMAGE_DIRECTORY_ENTRY_IMPORT, report.imports, report.warnings);
    }
    else if (mode == "runtime-functions") ParseRuntimeFunctions(image, report);
    else if (mode == "debug") ParseDebug(image, report);
    else
    {
        std::vector<WindowsPatternEvidence> evidence;
        if (!ScanWindowsCallPatterns(path, evidence, error)) return false;
        for (const WindowsPatternEvidence& item : evidence)
            if (item.targetKind == "wdf-table") report.wdfBindings.push_back(item);
    }
    return true;
}

bool InspectionService::IsSupportedMode(const std::string& mode)
{
    return mode == "exports" || mode == "imports" || mode == "runtime-functions" ||
        mode == "debug" || mode == "wdf-bind";
}

void InspectionService::WriteText(std::ostream& output, const InspectionReport& report)
{
    output << "FuBi static inspection\nmode = " << report.mode << "\npath = " << report.path
        << "\narchitecture = " << report.architecture << "\ntimestamp = " << report.timestamp
        << "\nimage_size = " << Hex(report.imageSize) << "\n";
    if (report.mode == "exports")
    {
        output << "export_count = " << report.exports.size() << "\n";
        for (const StaticExport& item : report.exports)
            output << "export ordinal=" << item.ordinal << " rva=" << Hex(item.rva)
                << " name=" << (item.names.empty() ? "<ordinal-only>" : item.names.front())
                << " forwarder=" << (item.forwarder.empty() ? "<none>" : item.forwarder) << "\n";
    }
    else if (report.mode == "imports")
    {
        output << "import_modules = " << report.imports.size() << "\n";
        for (const InspectionImportModule& module : report.imports)
            output << "import " << module.name << " symbols=" << module.symbols.size() << "\n";
        output << "delay_import_modules = " << report.delayImports.size() << "\n";
    }
    else if (report.mode == "runtime-functions")
    {
        output << "runtime_function_count = " << report.runtimeFunctions.size() << "\n";
        for (const InspectionRuntimeFunction& item : report.runtimeFunctions)
            output << "runtime begin=" << Hex(item.beginRva) << " end=" << Hex(item.endRva)
                << " unwind=" << Hex(item.unwindRva) << "\n";
    }
    else if (report.mode == "debug")
    {
        output << "debug_entry_count = " << report.debugEntries.size() << "\n";
        for (const InspectionDebugEntry& item : report.debugEntries)
            output << "debug type=" << item.type << " timestamp=" << item.timestamp
                << " size=" << item.size << " codeview="
                << (item.codeViewFormat.empty() ? "<none>" : item.codeViewFormat) << "\n";
    }
    else
    {
        output << "wdf_binding_count = " << report.wdfBindings.size() << "\n";
        for (const WindowsPatternEvidence& item : report.wdfBindings)
            output << "wdf rva=" << Hex(item.rva) << " target_rva=" << Hex(item.targetRva)
                << " pattern=" << item.patternId << "\n";
    }
    for (const std::string& warning : report.warnings) output << "warning = " << warning << "\n";
}

void InspectionService::WriteJson(std::ostream& output, const InspectionReport& report)
{
    output << "{\"schema_version\":" << InspectionReport::kSchemaVersion
        << ",\"action\":\"inspect\",\"mode\":"; JsonString(output, report.mode);
    output << ",\"success\":true,\"status\":\"completed\",\"module\":{\"path\":";
    JsonString(output, report.path);
    output << ",\"architecture\":"; JsonString(output, report.architecture);
    output << ",\"timestamp\":" << report.timestamp << ",\"image_size\":" << report.imageSize << '}';
    output << ",\"exports\":[";
    for (size_t index = 0; index < report.exports.size(); ++index)
    {
        if (index != 0) output << ',';
        const StaticExport& item = report.exports[index];
        output << "{\"ordinal\":" << item.ordinal << ",\"rva\":" << item.rva << ",\"names\":[";
        for (size_t nameIndex = 0; nameIndex < item.names.size(); ++nameIndex)
        {
            if (nameIndex != 0) output << ',';
            JsonString(output, item.names[nameIndex]);
        }
        output << "],\"forwarder\":"; JsonString(output, item.forwarder); output << '}';
    }
    output << "],\"imports\":"; JsonImportModules(output, report.imports);
    output << ",\"delay_imports\":"; JsonImportModules(output, report.delayImports);
    output << ",\"runtime_functions\":[";
    for (size_t index = 0; index < report.runtimeFunctions.size(); ++index)
    {
        if (index != 0) output << ',';
        const auto& item = report.runtimeFunctions[index];
        output << "{\"begin_rva\":" << item.beginRva << ",\"end_rva\":" << item.endRva
            << ",\"unwind_rva\":" << item.unwindRva << '}';
    }
    output << "],\"debug\":[";
    for (size_t index = 0; index < report.debugEntries.size(); ++index)
    {
        if (index != 0) output << ',';
        const auto& item = report.debugEntries[index];
        output << "{\"type\":" << item.type << ",\"timestamp\":" << item.timestamp
            << ",\"size\":" << item.size << ",\"codeview\":"; JsonString(output, item.codeViewFormat);
        output << ",\"pdb_guid\":"; JsonString(output, item.pdbGuid);
        output << ",\"pdb_age\":" << item.pdbAge << ",\"pdb_path\":"; JsonString(output, item.pdbPath); output << '}';
    }
    output << "],\"wdf_bindings\":[";
    for (size_t index = 0; index < report.wdfBindings.size(); ++index)
    {
        if (index != 0) output << ',';
        const auto& item = report.wdfBindings[index];
        output << "{\"rva\":" << item.rva << ",\"pattern\":"; JsonString(output, item.patternId);
        output << ",\"provenance\":"; JsonString(output, item.provenance);
        output << ",\"target_rva\":" << item.targetRva << ",\"target_kind\":"; JsonString(output, item.targetKind); output << '}';
    }
    output << "],\"warnings\":[";
    for (size_t index = 0; index < report.warnings.size(); ++index)
    {
        if (index != 0) output << ',';
        JsonString(output, report.warnings[index]);
    }
    output << "]}\n";
}
