#include "AnalysisReport.h"

#include <windows.h>

#include <ctime>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace
{
std::string Hex(uint64_t value)
{
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << value;
    return output.str();
}

const char* YesNo(bool value)
{
    return value ? "yes" : "no";
}

std::string MachineName(uint16_t machine)
{
    switch (machine)
    {
    case IMAGE_FILE_MACHINE_I386: return "x86";
    case IMAGE_FILE_MACHINE_AMD64: return "x64";
    case IMAGE_FILE_MACHINE_ARM: return "ARM";
    case IMAGE_FILE_MACHINE_ARM64: return "ARM64";
    default: return "unknown";
    }
}

std::string DirectoryName(uint32_t index)
{
    static const char* names[] = {
        "exports", "imports", "resources", "exceptions", "certificates",
        "relocations", "debug", "architecture", "global_ptr", "tls",
        "load_config", "bound_imports", "iat", "delay_imports", "com_descriptor",
        "reserved"
    };
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}

std::string JsonEscape(const std::string& value)
{
    std::ostringstream output;
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20)
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned>(character) << std::dec;
            else
                output << character;
        }
    }
    return output.str();
}

const PeFunction* FindFunction(const PEAnalysis& analysis, const std::string& query)
{
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(query.c_str(), &end, 0);
    if (end != query.c_str() && *end == '\0' && parsed <= UINT32_MAX)
    {
        const uint32_t rva = static_cast<uint32_t>(parsed);
        for (const PeFunction& function : analysis.functions)
            if (rva >= function.beginRva && rva < function.endRva) return &function;
    }

    for (const PeFunction& function : analysis.functions)
    {
        if (function.name == query ||
            std::find(function.aliases.begin(), function.aliases.end(), query) != function.aliases.end())
            return &function;
    }
    return nullptr;
}

const PeFunction* FindFunctionByRva(const PEAnalysis& analysis, uint32_t rva)
{
    for (const PeFunction& function : analysis.functions)
        if (function.beginRva == rva) return &function;
    return nullptr;
}

const PeString* FindStringByRva(const PEAnalysis& analysis, uint32_t rva)
{
    for (const PeString& item : analysis.strings)
        if (item.rva == rva) return &item;
    return nullptr;
}

void WriteInstruction(std::ostream& output, const PeInstruction& instruction)
{
    output << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
           << instruction.rva << std::dec << std::setfill(' ')
           << "  " << std::left << std::setw(28) << instruction.bytes
           << "  " << instruction.text << std::right;
    if (!instruction.annotation.empty()) output << "  ; " << instruction.annotation;
    output << "\n";
}

void WriteFunctionList(
    std::ostream& output, const PEAnalysis& analysis,
    const std::vector<uint32_t>& functions)
{
    if (functions.empty())
    {
        output << "    unavailable\n";
        return;
    }
    for (const uint32_t rva : functions)
    {
        const PeFunction* function = FindFunctionByRva(analysis, rva);
        output << "    " << (function ? function->name : ("sub_" + Hex(rva)))
               << " @ " << Hex(rva) << "\n";
    }
}

void JsonString(std::ostream& output, const std::string& value)
{
    output << '"' << JsonEscape(value) << '"';
}

void WriteImports(
    std::ostream& output, const std::vector<PeImportModule>& modules,
    const std::string& heading)
{
    output << "\n" << heading << "\n" << std::string(heading.size(), '=') << "\n";
    if (modules.empty()) output << "unavailable\n";
    for (const PeImportModule& module : modules)
    {
        output << "[imports: " << module.name << "]\n";
        for (const PeImport& symbol : module.symbols)
        {
            output << "  " << (symbol.byOrdinal ? ("#" + std::to_string(symbol.ordinal)) : symbol.name)
                   << "  hint=" << symbol.hint
                   << " thunk_rva=" << Hex(symbol.thunkRva)
                   << " iat_rva=" << Hex(symbol.iatRva) << "\n";
        }
    }
}

void WriteJsonImports(
    std::ostream& output, const std::vector<PeImportModule>& modules)
{
    output << '[';
    for (size_t moduleIndex = 0; moduleIndex < modules.size(); ++moduleIndex)
    {
        if (moduleIndex != 0) output << ',';
        const PeImportModule& module = modules[moduleIndex];
        output << "{\"dll\":";
        JsonString(output, module.name);
        output << ",\"symbols\":[";
        for (size_t symbolIndex = 0; symbolIndex < module.symbols.size(); ++symbolIndex)
        {
            if (symbolIndex != 0) output << ',';
            const PeImport& symbol = module.symbols[symbolIndex];
            output << "{\"name\":";
            JsonString(output, symbol.name);
            output << ",\"by_ordinal\":" << (symbol.byOrdinal ? "true" : "false")
                   << ",\"ordinal\":" << symbol.ordinal
                   << ",\"hint\":" << symbol.hint
                   << ",\"thunk_rva\":" << symbol.thunkRva
                   << ",\"iat_rva\":" << symbol.iatRva << '}';
        }
        output << "]}";
    }
    output << ']';
}

void WriteJson(std::ostream& output, const PEAnalysis& analysis)
{
    output << "{\n  \"file\":{\"path\":";
    JsonString(output, analysis.path);
    output << ",\"size\":" << analysis.fileSize << ",\"sha256\":";
    JsonString(output, analysis.sha256);
    output << "},\n  \"pe\":{\"format\":";
    JsonString(output, analysis.headers.isPe32Plus ? "PE32+" : "PE32");
    output << ",\"machine\":";
    JsonString(output, MachineName(analysis.headers.machine));
    output << ",\"machine_value\":" << analysis.headers.machine
           << ",\"image_base\":" << analysis.headers.preferredImageBase
           << ",\"entry_point_rva\":" << analysis.headers.entryPointRva
           << ",\"section_alignment\":" << analysis.headers.sectionAlignment
           << ",\"file_alignment\":" << analysis.headers.fileAlignment
           << ",\"subsystem\":" << analysis.headers.subsystem
           << ",\"dll_characteristics\":" << analysis.headers.dllCharacteristics
           << ",\"timestamp\":" << analysis.headers.timestamp
           << ",\"image_size\":" << analysis.headers.imageSize
           << ",\"checksum\":" << analysis.headers.checksum << "},\n";

    output << "  \"data_directories\":[";
    for (size_t index = 0; index < analysis.headers.dataDirectories.size(); ++index)
    {
        if (index != 0) output << ',';
        const PeDataDirectory& directory = analysis.headers.dataDirectories[index];
        output << "{\"index\":" << directory.index << ",\"name\":";
        JsonString(output, DirectoryName(directory.index));
        output << ",\"address\":" << directory.rva
               << ",\"address_kind\":";
        JsonString(output, directory.usesFileOffset ? "file_offset" : "rva");
        output << ",\"size\":" << directory.size << '}';
    }
    output << "],\n  \"sections\":[";
    for (size_t index = 0; index < analysis.sections.size(); ++index)
    {
        if (index != 0) output << ',';
        const PeSection& section = analysis.sections[index];
        output << "{\"name\":"; JsonString(output, section.name);
        output << ",\"rva\":" << section.rva
               << ",\"virtual_size\":" << section.virtualSize
               << ",\"raw_offset\":" << section.rawOffset
               << ",\"raw_size\":" << section.rawSize
               << ",\"characteristics\":" << section.characteristics
               << ",\"executable\":" << (section.executable ? "true" : "false")
               << ",\"readable\":" << (section.readable ? "true" : "false")
               << ",\"writable\":" << (section.writable ? "true" : "false")
               << ",\"entropy\":" << std::fixed << std::setprecision(4) << section.entropy
               << std::defaultfloat << '}';
    }
    output << "],\n  \"exports\":[";
    for (size_t index = 0; index < analysis.exports.size(); ++index)
    {
        if (index != 0) output << ',';
        const PeExport& item = analysis.exports[index];
        output << "{\"ordinal\":" << item.ordinal << ",\"rva\":" << item.rva
               << ",\"names\":[";
        for (size_t nameIndex = 0; nameIndex < item.names.size(); ++nameIndex)
        {
            if (nameIndex != 0) output << ',';
            JsonString(output, item.names[nameIndex]);
        }
        output << "],\"forwarder\":"; JsonString(output, item.forwarder);
        output << ",\"signature\":"; JsonString(output, item.signature);
        output << ",\"signature_source\":"; JsonString(output, item.signatureSource);
        output << '}';
    }
    output << "],\n  \"imports\":"; WriteJsonImports(output, analysis.imports);
    output << ",\n  \"delay_imports\":"; WriteJsonImports(output, analysis.delayImports);

    output << ",\n  \"debug\":[";
    for (size_t index = 0; index < analysis.debugEntries.size(); ++index)
    {
        if (index != 0) output << ',';
        const PeDebugEntry& item = analysis.debugEntries[index];
        output << "{\"type\":" << item.type << ",\"timestamp\":" << item.timestamp
               << ",\"codeview_format\":"; JsonString(output, item.codeViewFormat);
        output << ",\"pdb_guid\":"; JsonString(output, item.pdbGuid);
        output << ",\"pdb_age\":" << item.pdbAge << ",\"pdb_path\":";
        JsonString(output, item.pdbPath); output << '}';
    }
    output << "],\n  \"strings\":[";
    for (size_t index = 0; index < analysis.strings.size(); ++index)
    {
        if (index != 0) output << ',';
        const PeString& item = analysis.strings[index];
        output << "{\"rva\":" << item.rva << ",\"file_offset\":" << item.fileOffset
               << ",\"section\":"; JsonString(output, item.section);
        output << ",\"encoding\":"; JsonString(output, item.encoding);
        output << ",\"value\":"; JsonString(output, item.value); output << '}';
    }
    output << "],\n  \"resources\":{\"types\":[";
    for (size_t index = 0; index < analysis.resources.types.size(); ++index)
    {
        if (index != 0) output << ',';
        JsonString(output, analysis.resources.types[index]);
    }
    output << "],\"company_name\":"; JsonString(output, analysis.resources.companyName);
    output << ",\"product_name\":"; JsonString(output, analysis.resources.productName);
    output << ",\"file_description\":"; JsonString(output, analysis.resources.fileDescription);
    output << ",\"file_version\":"; JsonString(output, analysis.resources.fileVersion);
    output << ",\"product_version\":"; JsonString(output, analysis.resources.productVersion);
    output << ",\"original_filename\":"; JsonString(output, analysis.resources.originalFilename);
    output << ",\"internal_name\":"; JsonString(output, analysis.resources.internalName);
    output << "},\n  \"functions\":[";
    for (size_t index = 0; index < analysis.functions.size(); ++index)
    {
        if (index != 0) output << ',';
        const PeFunction& function = analysis.functions[index];
        output << "{\"name\":"; JsonString(output, function.name);
        output << ",\"name_source\":"; JsonString(output, function.nameSource);
        output << ",\"name_confidence\":"; JsonString(output, function.nameConfidence);
        output << ",\"boundary_source\":"; JsonString(output, function.boundarySource);
        output << ",\"boundary_confidence\":";
        JsonString(output, function.boundaryConfidence);
        output << ",\"begin_rva\":" << function.beginRva
               << ",\"end_rva\":" << function.endRva
               << ",\"size\":" << (function.endRva - function.beginRva)
               << ",\"file_offset\":" << function.fileOffset
               << ",\"section\":"; JsonString(output, function.section);
        output << ",\"unwind_rva\":" << function.unwindRva << ",\"aliases\":[";
        for (size_t aliasIndex = 0; aliasIndex < function.aliases.size(); ++aliasIndex)
        {
            if (aliasIndex != 0) output << ',';
            JsonString(output, function.aliases[aliasIndex]);
        }
        output << "],\"callers\":[";
        for (size_t itemIndex = 0; itemIndex < function.callers.size(); ++itemIndex)
        {
            if (itemIndex != 0) output << ',';
            output << function.callers[itemIndex];
        }
        output << "],\"callees\":[";
        for (size_t itemIndex = 0; itemIndex < function.callees.size(); ++itemIndex)
        {
            if (itemIndex != 0) output << ',';
            output << function.callees[itemIndex];
        }
        output << "],\"imported_calls\":[";
        for (size_t itemIndex = 0; itemIndex < function.importedCalls.size(); ++itemIndex)
        {
            if (itemIndex != 0) output << ',';
            JsonString(output, function.importedCalls[itemIndex]);
        }
        output << "],\"referenced_string_rvas\":[";
        for (size_t itemIndex = 0; itemIndex < function.referencedStringRvas.size(); ++itemIndex)
        {
            if (itemIndex != 0) output << ',';
            output << function.referencedStringRvas[itemIndex];
        }
        output << "],\"abi_observations\":{\"consumed_argument_registers\":[";
        for (size_t registerIndex = 0;
             registerIndex < function.abiConsumedRegisters.size(); ++registerIndex)
        {
            if (registerIndex != 0) output << ',';
            JsonString(output, function.abiConsumedRegisters[registerIndex]);
        }
        output << "],\"inferred_minimum_arguments\":" << function.inferredMinimumArguments
               << ",\"confidence\":";
        JsonString(output, function.abiConfidence);
        output << ",\"provenance\":";
        JsonString(output, function.abiProvenance);
        output << "},\"instructions\":[";
        for (size_t instructionIndex = 0;
             instructionIndex < function.instructions.size(); ++instructionIndex)
        {
            if (instructionIndex != 0) output << ',';
            const PeInstruction& instruction = function.instructions[instructionIndex];
            output << "{\"rva\":" << instruction.rva << ",\"length\":"
                   << static_cast<unsigned>(instruction.length) << ",\"bytes\":";
            JsonString(output, instruction.bytes);
            output << ",\"text\":"; JsonString(output, instruction.text);
            output << ",\"is_call\":" << (instruction.isCall ? "true" : "false")
                   << ",\"indirect_call\":" << (instruction.indirectCall ? "true" : "false")
                   << ",\"direct_target_rva\":" << instruction.directTargetRva
                   << ",\"annotation\":";
            JsonString(output, instruction.annotation);
            output << ",\"framework_call\":";
            JsonString(output, instruction.frameworkCall);
            output << ",\"framework_slot\":";
            if (instruction.frameworkSlot == UINT32_MAX) output << "null";
            else output << instruction.frameworkSlot;
            output << ",\"framework_call_confidence\":";
            JsonString(output, instruction.frameworkCallConfidence);
            output << ",\"framework_call_provenance\":";
            JsonString(output, instruction.frameworkCallProvenance);
            output << '}';
        }
        output << "]}";
    }
    output << "],\n  \"classifications\":[";
    for (size_t index = 0; index < analysis.classifications.size(); ++index)
    {
        if (index != 0) output << ',';
        const PeClassification& item = analysis.classifications[index];
        output << "{\"name\":"; JsonString(output, item.name);
        output << ",\"evidence\":[";
        for (size_t evidenceIndex = 0; evidenceIndex < item.evidence.size(); ++evidenceIndex)
        {
            if (evidenceIndex != 0) output << ',';
            JsonString(output, item.evidence[evidenceIndex]);
        }
        output << "]}";
    }
    output << "],\n  \"framework_bindings\":[";
    for (size_t index = 0; index < analysis.frameworkBindings.size(); ++index)
    {
        if (index != 0) output << ',';
        const PeFrameworkBinding& binding = analysis.frameworkBindings[index];
        output << "{\"framework\":"; JsonString(output, binding.framework);
        output << ",\"major_version\":" << binding.majorVersion
               << ",\"minor_version\":" << binding.minorVersion
               << ",\"build_version\":" << binding.buildVersion
               << ",\"function_count\":" << binding.functionCount
               << ",\"function_table_rva\":" << binding.functionTableRva
               << ",\"indirect_function_table\":"
               << (binding.indirectFunctionTable ? "true" : "false")
               << ",\"confidence\":";
        JsonString(output, binding.confidence);
        output << ",\"provenance\":";
        JsonString(output, binding.provenance);
        output << '}';
    }
    output << "],\n  \"capabilities\":[";
    for (size_t index = 0; index < analysis.capabilities.size(); ++index)
    {
        if (index != 0) output << ',';
        const PeCapability& item = analysis.capabilities[index];
        output << "{\"name\":"; JsonString(output, item.name);
        output << ",\"state\":"; JsonString(output, item.state);
        output << ",\"confidence\":"; JsonString(output, item.confidence);
        output << ",\"evidence\":[";
        for (size_t evidenceIndex = 0; evidenceIndex < item.evidence.size(); ++evidenceIndex)
        {
            if (evidenceIndex != 0) output << ',';
            output << "{\"source\":";
            JsonString(output, item.evidence[evidenceIndex].source);
            output << ",\"detail\":";
            JsonString(output, item.evidence[evidenceIndex].detail);
            output << '}';
        }
        output << "]}";
    }
    output << "],\n  \"security\":{" 
           << "\"dynamic_base\":" << (analysis.security.dynamicBase ? "true" : "false")
           << ",\"high_entropy_va\":" << (analysis.security.highEntropyVa ? "true" : "false")
           << ",\"nx_compatible\":" << (analysis.security.nxCompatible ? "true" : "false")
           << ",\"control_flow_guard\":" << (analysis.security.controlFlowGuard ? "true" : "false")
           << ",\"safe_seh\":" << (analysis.security.safeSeh ? "true" : "false")
           << ",\"cet_compatible\":" << (analysis.security.cetCompatible ? "true" : "false")
           << ",\"security_cookie_rva\":" << analysis.security.securityCookieRva
           << ",\"guard_check_function_pointer_rva\":"
           << analysis.security.guardCheckFunctionPointerRva
           << ",\"guard_dispatch_function_pointer_rva\":"
           << analysis.security.guardDispatchFunctionPointerRva
           << ",\"guard_flags\":" << analysis.security.guardFlags
           << ",\"guard_function_count\":" << analysis.security.guardFunctionCount
           << ",\"guard_function_rvas\":[";
    for (size_t index = 0; index < analysis.security.guardFunctionRvas.size(); ++index)
    {
        if (index != 0) output << ',';
        output << analysis.security.guardFunctionRvas[index];
    }
    output << "]},\n  \"warnings\":[";
    for (size_t index = 0; index < analysis.warnings.size(); ++index)
    {
        if (index != 0) output << ',';
        JsonString(output, analysis.warnings[index]);
    }
    output << "]\n}\n";
}
}

void AnalysisReport::WriteText(std::ostream& output, const PEAnalysis& analysis)
{
    output << "FuBi static PE analysis\n=======================\n";
    output << "file = " << analysis.path << "\n";
    output << "size = " << analysis.fileSize << " bytes\n";
    output << "sha256 = " << (analysis.sha256.empty() ? "unavailable" : analysis.sha256) << "\n";

    output << "\nPE headers\n==========\n";
    output << "format = " << (analysis.headers.isPe32Plus ? "PE32+" : "PE32") << "\n";
    output << "machine = " << MachineName(analysis.headers.machine)
           << " (" << Hex(analysis.headers.machine) << ")\n";
    output << "preferred_image_base = " << Hex(analysis.headers.preferredImageBase) << "\n";
    output << "entry_point_rva = " << Hex(analysis.headers.entryPointRva) << "\n";
    output << "section_alignment = " << Hex(analysis.headers.sectionAlignment) << "\n";
    output << "file_alignment = " << Hex(analysis.headers.fileAlignment) << "\n";
    output << "subsystem = " << analysis.headers.subsystem << "\n";
    output << "dll_characteristics = " << Hex(analysis.headers.dllCharacteristics) << "\n";
    output << "timestamp = " << analysis.headers.timestamp << "\n";
    output << "image_size = " << Hex(analysis.headers.imageSize) << "\n";
    output << "checksum = " << Hex(analysis.headers.checksum) << "\n";

    output << "\nData directories\n================\n";
    for (const PeDataDirectory& directory : analysis.headers.dataDirectories)
        output << DirectoryName(directory.index) << "  "
               << (directory.usesFileOffset ? "file_offset=" : "rva=") << Hex(directory.rva)
               << " size=" << Hex(directory.size) << "\n";

    output << "\nSecurity\n========\n"
           << "dynamic_base = " << YesNo(analysis.security.dynamicBase) << "\n"
           << "high_entropy_va = " << YesNo(analysis.security.highEntropyVa) << "\n"
           << "nx_compatible = " << YesNo(analysis.security.nxCompatible) << "\n"
           << "control_flow_guard = " << YesNo(analysis.security.controlFlowGuard) << "\n"
           << "safe_seh = " << YesNo(analysis.security.safeSeh) << "\n"
           << "cet_compatible = " << YesNo(analysis.security.cetCompatible) << "\n"
           << "security_cookie_rva = " << Hex(analysis.security.securityCookieRva) << "\n"
           << "guard_check_pointer_rva = "
           << Hex(analysis.security.guardCheckFunctionPointerRva) << "\n"
           << "guard_dispatch_pointer_rva = "
           << Hex(analysis.security.guardDispatchFunctionPointerRva) << "\n"
           << "guard_flags = " << Hex(analysis.security.guardFlags) << "\n"
           << "guard_function_count = " << analysis.security.guardFunctionCount << "\n";

    output << "\nResources and version\n=====================\n";
    output << "types = ";
    if (analysis.resources.types.empty()) output << "unavailable";
    for (size_t index = 0; index < analysis.resources.types.size(); ++index)
    {
        if (index != 0) output << ", ";
        output << analysis.resources.types[index];
    }
    output << "\ncompany = " << (analysis.resources.companyName.empty() ? "unavailable" : analysis.resources.companyName)
           << "\nproduct = " << (analysis.resources.productName.empty() ? "unavailable" : analysis.resources.productName)
           << "\ndescription = " << (analysis.resources.fileDescription.empty() ? "unavailable" : analysis.resources.fileDescription)
           << "\nfile_version = " << (analysis.resources.fileVersion.empty() ? "unavailable" : analysis.resources.fileVersion)
           << "\nproduct_version = " << (analysis.resources.productVersion.empty() ? "unavailable" : analysis.resources.productVersion)
           << "\noriginal_filename = " << (analysis.resources.originalFilename.empty() ? "unavailable" : analysis.resources.originalFilename)
           << "\ninternal_name = " << (analysis.resources.internalName.empty() ? "unavailable" : analysis.resources.internalName)
           << "\n";

    output << "\nSections\n========\n";
    for (const PeSection& section : analysis.sections)
    {
        output << '[' << section.name << "]\n"
               << "  RVA = " << Hex(section.rva) << "\n"
               << "  virtual_size = " << Hex(section.virtualSize) << "\n"
               << "  raw_offset = " << Hex(section.rawOffset) << "\n"
               << "  raw_size = " << Hex(section.rawSize) << "\n"
               << "  characteristics = " << Hex(section.characteristics) << "\n"
               << "  executable = " << YesNo(section.executable) << "\n"
               << "  readable = " << YesNo(section.readable) << "\n"
               << "  writable = " << YesNo(section.writable) << "\n"
               << "  entropy = " << std::fixed << std::setprecision(4) << section.entropy
               << std::defaultfloat << "\n";
    }

    output << "\nRecovered runtime-function boundaries\n"
              "=====================================\n";
    output << "count = " << analysis.functions.size() << "\n";
    for (const PeFunction& function : analysis.functions)
        output << function.name << "  begin=" << Hex(function.beginRva)
               << " end=" << Hex(function.endRva)
               << " size=" << (function.endRva - function.beginRva)
               << " unwind=" << Hex(function.unwindRva)
               << " name_source=" << function.nameSource
               << " name_confidence=" << function.nameConfidence
               << " boundary_source=" << function.boundarySource
               << " boundary_confidence=" << function.boundaryConfidence << "\n";

    output << "\nExports\n=======\n";
    if (analysis.exports.empty()) output << "unavailable\n";
    for (const PeExport& item : analysis.exports)
    {
        output << "ordinal=" << item.ordinal << " rva=" << Hex(item.rva) << " name=";
        if (item.names.empty()) output << '#' << item.ordinal;
        else
        {
            for (size_t index = 0; index < item.names.size(); ++index)
            {
                if (index != 0) output << ", ";
                output << item.names[index];
            }
        }
        if (!item.forwarder.empty()) output << " forwarder=" << item.forwarder;
        output << "\n  signature = " << (item.signature.empty() ? "unknown" : item.signature)
               << "\n  source = " << item.signatureSource << "\n";
    }

    WriteImports(output, analysis.imports, "Imports");
    WriteImports(output, analysis.delayImports, "Delay imports");

    output << "\nClassification\n==============\n";
    if (analysis.classifications.empty()) output << "unavailable\n";
    for (const PeClassification& item : analysis.classifications)
    {
        output << item.name << "\n";
        for (const std::string& evidence : item.evidence)
            output << "  evidence: " << evidence << "\n";
    }

    output << "\nCapability evidence\n===================\n";
    for (const PeCapability& item : analysis.capabilities)
    {
        output << item.name << " = " << item.state
               << " (confidence: " << item.confidence << ")\n";
        for (const PeCapability::Evidence& evidence : item.evidence)
            output << "  evidence [" << evidence.source << "]: "
                   << evidence.detail << "\n";
    }

    output << "\nFramework bindings\n==================\n";
    if (analysis.frameworkBindings.empty()) output << "not observed\n";
    for (const PeFrameworkBinding& binding : analysis.frameworkBindings)
        output << binding.framework << ' ' << binding.majorVersion << '.'
               << binding.minorVersion << '.' << binding.buildVersion
               << " function_count=" << binding.functionCount
               << " function_table_rva=" << Hex(binding.functionTableRva)
               << " indirection=" << (binding.indirectFunctionTable ? "pointer" : "direct")
               << " confidence=" << binding.confidence
               << "\n  provenance: " << binding.provenance << "\n";

    output << "\nDebug information\n=================\n";
    if (analysis.debugEntries.empty()) output << "unavailable\n";
    for (const PeDebugEntry& item : analysis.debugEntries)
    {
        output << "type=" << item.type << " timestamp=" << item.timestamp << "\n";
        if (!item.codeViewFormat.empty()) output << "  codeview=" << item.codeViewFormat << "\n";
        if (!item.pdbGuid.empty()) output << "  pdb_guid=" << item.pdbGuid << "\n";
        if (item.pdbAge != 0) output << "  pdb_age=" << item.pdbAge << "\n";
        if (!item.pdbPath.empty()) output << "  pdb=" << item.pdbPath << "\n";
    }

    WriteStrings(output, analysis);

    if (!analysis.warnings.empty())
    {
        output << "\nWarnings\n========\n";
        for (const std::string& warning : analysis.warnings) output << warning << "\n";
    }
}

void AnalysisReport::WriteStrings(std::ostream& output, const PEAnalysis& analysis)
{
    output << "\nStrings\n=======\n";
    for (const PeString& item : analysis.strings)
        output << "rva=" << Hex(item.rva) << " file_offset=" << Hex(item.fileOffset)
               << " section=" << item.section << " encoding=" << item.encoding
               << " value=\"" << item.value << "\"\n";
}

bool AnalysisReport::WriteTextFile(
    const std::string& path, const PEAnalysis& analysis, std::string& error)
{
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output)
    {
        error = "Unable to open report file: " + path;
        return false;
    }
    WriteText(output, analysis);
    if (!output.good())
    {
        error = "Unable to write complete report file: " + path;
        return false;
    }
    return true;
}

bool AnalysisReport::WriteJsonFile(
    const std::string& path, const PEAnalysis& analysis, std::string& error)
{
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output)
    {
        error = "Unable to open JSON report file: " + path;
        return false;
    }
    WriteJson(output, analysis);
    if (!output.good())
    {
        error = "Unable to write complete JSON report file: " + path;
        return false;
    }
    return true;
}

bool AnalysisReport::WriteDisassembly(
    std::ostream& output, const PEAnalysis& analysis,
    const std::string& nameOrRva, size_t maximumBytes, std::string& error)
{
    const PeFunction* function = FindFunction(analysis, nameOrRva);
    if (!function)
    {
        error = "Function not found: " + nameOrRva;
        return false;
    }

    output << function->name << " @ RVA " << Hex(function->beginRva) << "\n";
    size_t emitted = 0;
    for (const PeInstruction& instruction : function->instructions)
    {
        if (maximumBytes != 0 && emitted >= maximumBytes) break;
        WriteInstruction(output, instruction);
        emitted += instruction.length;
    }
    return true;
}

bool AnalysisReport::WriteCallers(
    std::ostream& output, const PEAnalysis& analysis,
    const std::string& nameOrRva, std::string& error)
{
    const PeFunction* function = FindFunction(analysis, nameOrRva);
    if (!function)
    {
        error = "Function not found: " + nameOrRva;
        return false;
    }
    output << function->name << " @ RVA " << Hex(function->beginRva) << "\ncallers:\n";
    WriteFunctionList(output, analysis, function->callers);
    return true;
}

bool AnalysisReport::WriteCallees(
    std::ostream& output, const PEAnalysis& analysis,
    const std::string& nameOrRva, std::string& error)
{
    const PeFunction* function = FindFunction(analysis, nameOrRva);
    if (!function)
    {
        error = "Function not found: " + nameOrRva;
        return false;
    }
    output << function->name << " @ RVA " << Hex(function->beginRva) << "\ncallees:\n";
    WriteFunctionList(output, analysis, function->callees);
    output << "imported API calls:\n";
    if (function->importedCalls.empty()) output << "    unavailable\n";
    for (const std::string& imported : function->importedCalls)
        output << "    " << imported << "\n";
    return true;
}

bool AnalysisReport::WriteStringXrefs(
    std::ostream& output, const PEAnalysis& analysis, const std::string& value)
{
    bool found = false;
    for (const PeString& item : analysis.strings)
    {
        if (item.value.find(value) == std::string::npos) continue;
        found = true;
        output << "string RVA " << Hex(item.rva) << " \"" << item.value << "\"\n"
               << "referenced by:\n";
        bool referenced = false;
        for (const PeFunction& function : analysis.functions)
        {
            for (const PeInstruction& instruction : function.instructions)
            {
                if (std::find(instruction.referencedStringRvas.begin(),
                              instruction.referencedStringRvas.end(), item.rva) ==
                    instruction.referencedStringRvas.end())
                    continue;
                output << "    " << function.name << " + "
                       << Hex(instruction.rva - function.beginRva) << "\n";
                referenced = true;
            }
        }
        if (!referenced) output << "    unavailable\n";
    }
    if (!found) output << "String not found: " << value << "\n";
    return found;
}

bool AnalysisReport::WriteImportXrefs(
    std::ostream& output, const PEAnalysis& analysis, const std::string& name)
{
    bool found = false;
    output << "import xrefs: " << name << "\n";
    for (const PeFunction& function : analysis.functions)
    {
        for (const PeInstruction& instruction : function.instructions)
        {
            if (instruction.annotation.find(name) == std::string::npos)
                continue;
            output << "    " << function.name << " + "
                   << Hex(instruction.rva - function.beginRva) << " @ "
                   << Hex(instruction.rva) << "\n";
            found = true;
        }
    }
    if (!found) output << "    unavailable\n";
    return found;
}

bool AnalysisReport::WriteFunctionReport(
    std::ostream& output, const PEAnalysis& analysis,
    const std::string& nameOrRva, std::string& error)
{
    const PeFunction* function = FindFunction(analysis, nameOrRva);
    if (!function)
    {
        error = "Function not found: " + nameOrRva;
        return false;
    }

    const PeExport* exported = nullptr;
    for (const PeExport& item : analysis.exports)
        if (item.rva >= function->beginRva && item.rva < function->endRva) exported = &item;

    output << "Function\n--------\n"
           << "name          " << function->name << "\n"
           << "RVA           " << Hex(function->beginRva) << "\n"
           << "file offset   " << Hex(function->fileOffset) << "\n"
           << "section       " << function->section << "\n"
           << "start RVA     " << Hex(function->beginRva) << "\n"
           << "end RVA       " << Hex(function->endRva) << "\n"
           << "size          " << (function->endRva - function->beginRva) << "\n"
           << "unwind RVA    " << Hex(function->unwindRva) << "\n\n";

    output << "Symbol information\n------------------\n"
           << "source        " << function->nameSource << "\n"
           << "name confidence " << function->nameConfidence << "\n"
           << "boundary source " << function->boundarySource << "\n"
           << "boundary confidence " << function->boundaryConfidence << "\n"
           << "prototype     "
           << (exported && !exported->signature.empty() ? exported->signature : "unknown") << "\n"
           << "signature source "
           << (exported ? exported->signatureSource : "unknown") << "\n\n";

    output << "ABI observations\n----------------\n";
    if (function->abiConsumedRegisters.empty()) output << "unavailable\n";
    for (const std::string& reg : function->abiConsumedRegisters)
        output << reg << " consumed before overwrite\n";
    if (function->inferredMinimumArguments != 0)
        output << "inferred minimum args = " << function->inferredMinimumArguments
               << "\nconfidence = " << function->abiConfidence
               << "\nprovenance = " << function->abiProvenance << "\n";
    output << "These are ABI observations, not an exact prototype.\n\n";

    output << "Callers\n-------\n";
    WriteFunctionList(output, analysis, function->callers);
    output << "\nCallees\n-------\n";
    WriteFunctionList(output, analysis, function->callees);

    output << "\nImported API calls\n------------------\n";
    if (function->importedCalls.empty()) output << "    unavailable\n";
    for (const std::string& imported : function->importedCalls)
        output << "    " << imported << "\n";

    output << "\nReferenced strings\n------------------\n";
    if (function->referencedStringRvas.empty()) output << "    unavailable\n";
    for (const uint32_t rva : function->referencedStringRvas)
    {
        const PeString* item = FindStringByRva(analysis, rva);
        output << "    " << Hex(rva);
        if (item) output << " \"" << item->value << "\"";
        output << "\n";
    }

    output << "\nReferenced globals\n------------------\nunavailable\n\n"
              "Disassembly\n-----------\n";
    for (const PeInstruction& instruction : function->instructions)
        WriteInstruction(output, instruction);
    return true;
}
