#define BOOST_TEST_MODULE StaticPEAnalysisTests
#include <boost/test/included/unit_test.hpp>

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "AnalysisReport.h"
#include "PEAnalyzer.h"
#include "PEImage.h"

namespace
{
std::string ExecutableDirectory()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string value(path);
    return value.substr(0, value.find_last_of("\\/") + 1);
}

std::string FixturePath(const std::string& name)
{
    return ExecutableDirectory() + name;
}

bool HasImport(
    const std::vector<PeImportModule>& modules,
    const std::string& moduleName,
    const std::string& symbolName)
{
    for (const PeImportModule& module : modules)
    {
        if (_stricmp(module.name.c_str(), moduleName.c_str()) != 0) continue;
        return std::any_of(
            module.symbols.begin(), module.symbols.end(),
            [&symbolName](const PeImport& symbol) { return symbol.name == symbolName; });
    }
    return false;
}

bool HasString(const PEAnalysis& analysis, const std::string& value, const std::string& encoding)
{
    return std::any_of(
        analysis.strings.begin(), analysis.strings.end(),
        [&value, &encoding](const PeString& item)
        {
            return item.value.find(value) != std::string::npos && item.encoding == encoding;
        });
}

std::vector<uint8_t> MinimalPe32()
{
    std::vector<uint8_t> bytes(0x400, 0);
    IMAGE_DOS_HEADER dos = {};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = 0x80;
    std::memcpy(bytes.data(), &dos, sizeof(dos));

    const DWORD signature = IMAGE_NT_SIGNATURE;
    std::memcpy(bytes.data() + 0x80, &signature, sizeof(signature));

    IMAGE_FILE_HEADER file = {};
    file.Machine = IMAGE_FILE_MACHINE_I386;
    file.NumberOfSections = 1;
    file.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    std::memcpy(bytes.data() + 0x84, &file, sizeof(file));

    IMAGE_OPTIONAL_HEADER32 optional = {};
    optional.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    optional.ImageBase = 0x400000;
    optional.SectionAlignment = 0x1000;
    optional.FileAlignment = 0x200;
    optional.SizeOfImage = 0x2000;
    optional.SizeOfHeaders = 0x200;
    optional.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    const size_t optionalOffset = 0x84 + sizeof(file);
    std::memcpy(bytes.data() + optionalOffset, &optional, sizeof(optional));

    IMAGE_SECTION_HEADER section = {};
    std::memcpy(section.Name, ".text", 5);
    section.Misc.VirtualSize = 0x100;
    section.VirtualAddress = 0x1000;
    section.SizeOfRawData = 0x200;
    section.PointerToRawData = 0x200;
    section.Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    std::memcpy(bytes.data() + optionalOffset + sizeof(optional), &section, sizeof(section));
    return bytes;
}
}

BOOST_AUTO_TEST_CASE(ParsesPe32AndRejectsMalformedFiles)
{
    PEImage image;
    std::string error;
    BOOST_REQUIRE(PEImage::FromBytes(MinimalPe32(), "synthetic-pe32", image, error));
    BOOST_CHECK(!image.Headers().isPe32Plus);
    BOOST_CHECK_EQUAL(image.Headers().machine, IMAGE_FILE_MACHINE_I386);
    BOOST_REQUIRE(image.RvaToFileOffset(0x1000).has_value());
    BOOST_CHECK_EQUAL(*image.RvaToFileOffset(0x1000), 0x200U);

    PEImage invalid;
    BOOST_CHECK(!PEImage::FromBytes(std::vector<uint8_t>(8, 0), "truncated", invalid, error));

    std::vector<uint8_t> malformed = MinimalPe32();
    const size_t sectionOffset = 0x84 + sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_OPTIONAL_HEADER32);
    IMAGE_SECTION_HEADER section = {};
    std::memcpy(&section, malformed.data() + sectionOffset, sizeof(section));
    section.PointerToRawData = 0x100000;
    std::memcpy(malformed.data() + sectionOffset, &section, sizeof(section));
    BOOST_CHECK(!PEImage::FromBytes(std::move(malformed), "bad-section", invalid, error));
}

BOOST_AUTO_TEST_CASE(StaticAnalysisDoesNotExecuteDllMain)
{
    const std::string markerPath = "static_fixture.executed";
    DeleteFileA(markerPath.c_str());

    PEAnalysis analysis;
    std::string error;
    const PEAnalyzer analyzer(5);
    BOOST_REQUIRE(analyzer.AnalyzeFile(FixturePath("static_fixture.dll"), analysis, error));
    BOOST_CHECK_EQUAL(GetFileAttributesA(markerPath.c_str()), INVALID_FILE_ATTRIBUTES);

    BOOST_CHECK(analysis.headers.isPe32Plus);
    BOOST_CHECK_EQUAL(analysis.headers.machine, IMAGE_FILE_MACHINE_AMD64);
    BOOST_CHECK(!analysis.sections.empty());
    BOOST_CHECK(!analysis.imports.empty());
    BOOST_CHECK(HasImport(analysis.delayImports, "USER32.dll", "MessageBoxA"));
    BOOST_CHECK(HasString(analysis, "Trigger ASCII", "ASCII"));
    BOOST_CHECK(HasString(analysis, "NativeUSB UTF16", "UTF-16LE"));
    BOOST_CHECK(std::any_of(
        analysis.debugEntries.begin(), analysis.debugEntries.end(),
        [](const PeDebugEntry& entry) { return entry.codeViewFormat == "RSDS"; }));
    BOOST_CHECK(analysis.security.dynamicBase);
    BOOST_CHECK(analysis.security.nxCompatible);
    BOOST_CHECK(analysis.security.controlFlowGuard);
    BOOST_CHECK(std::find(
        analysis.resources.types.begin(), analysis.resources.types.end(), "VERSION") !=
        analysis.resources.types.end());
    BOOST_CHECK_EQUAL(analysis.resources.companyName, "FuBi Tests");
    BOOST_CHECK(std::any_of(
        analysis.classifications.begin(), analysis.classifications.end(),
        [](const PeClassification& item)
        {
            return item.name == "probable UMDF/WDF user-mode driver";
        }));
    BOOST_CHECK(std::any_of(
        analysis.capabilities.begin(), analysis.capabilities.end(),
        [](const PeCapability& item)
        {
            return item.name == "file I/O" && item.state == "observed" &&
                item.confidence == "high";
        }));
    BOOST_CHECK(std::any_of(
        analysis.capabilities.begin(), analysis.capabilities.end(),
        [](const PeCapability& item)
        {
            return item.name == "WinUSB" && item.state == "not observed";
        }));
    BOOST_CHECK(std::any_of(
        analysis.capabilities.begin(), analysis.capabilities.end(),
        [](const PeCapability& item)
        {
            return item.name == "IddCx" && item.state == "unknown";
        }));
    BOOST_REQUIRE(!analysis.frameworkBindings.empty());
    BOOST_CHECK_EQUAL(analysis.frameworkBindings.front().framework, "UMDF");
    BOOST_CHECK_EQUAL(analysis.frameworkBindings.front().majorVersion, 2U);
    BOOST_CHECK_EQUAL(analysis.frameworkBindings.front().minorVersion, 33U);
    BOOST_CHECK_EQUAL(analysis.frameworkBindings.front().functionCount, 274U);
    BOOST_CHECK(!analysis.runtimeFunctions.empty());
    BOOST_CHECK(!analysis.functions.empty());
    const auto delayedFunction = std::find_if(
        analysis.functions.begin(), analysis.functions.end(),
        [](const PeFunction& function)
        {
            return function.name == "DelayedMessageBoxReference";
        });
    BOOST_REQUIRE(delayedFunction != analysis.functions.end());
    BOOST_CHECK(std::any_of(
        delayedFunction->importedCalls.begin(), delayedFunction->importedCalls.end(),
        [](const std::string& name) { return name.find("MessageBoxA") != std::string::npos; }));

    const auto wdfFunction = std::find_if(
        analysis.functions.begin(), analysis.functions.end(),
        [](const PeFunction& function)
        {
            return function.name == "WdfUsbDispatchFixture";
        });
    BOOST_REQUIRE(wdfFunction != analysis.functions.end());
    BOOST_CHECK_EQUAL(wdfFunction->nameSource, "pe-export");
    BOOST_CHECK_EQUAL(wdfFunction->nameConfidence, "high");
    BOOST_CHECK(std::any_of(
        wdfFunction->instructions.begin(), wdfFunction->instructions.end(),
        [](const PeInstruction& instruction)
        {
            return instruction.frameworkCall == "WdfUsbTargetDeviceCreate" &&
                instruction.frameworkSlot == 202 &&
                instruction.frameworkCallConfidence == "high";
        }));
    BOOST_CHECK(std::any_of(
        analysis.capabilities.begin(), analysis.capabilities.end(),
        [](const PeCapability& item)
        {
            return item.name == "WDF USB" && item.state == "observed" &&
                item.confidence == "high";
        }));

    const std::string jsonPath = FixturePath("static_fixture.analysis.json");
    BOOST_REQUIRE(AnalysisReport::WriteJsonFile(jsonPath, analysis, error));
    std::ifstream json(jsonPath);
    std::ostringstream contents;
    contents << json.rdbuf();
    BOOST_CHECK(contents.str().find("\"delay_imports\"") != std::string::npos);
    BOOST_CHECK(contents.str().find("NativeUSB UTF16") != std::string::npos);
    BOOST_CHECK(contents.str().find("\"instructions\"") != std::string::npos);
    BOOST_CHECK(contents.str().find("\"state\":\"not observed\"") != std::string::npos);
    BOOST_CHECK(contents.str().find("\"present\"") == std::string::npos);
    BOOST_CHECK(contents.str().find("WdfUsbTargetDeviceCreate") != std::string::npos);
    BOOST_CHECK(contents.str().find("\"framework_bindings\"") != std::string::npos);

    std::ostringstream functionReport;
    BOOST_REQUIRE(AnalysisReport::WriteFunctionReport(
        functionReport, analysis, "DelayedMessageBoxReference", error));
    BOOST_CHECK(functionReport.str().find("MessageBoxA") != std::string::npos);
    BOOST_CHECK(functionReport.str().find("Disassembly") != std::string::npos);
    DeleteFileA(jsonPath.c_str());
    DeleteFileA(markerPath.c_str());
}

BOOST_AUTO_TEST_CASE(StaticExportParserPreservesLegacyFixtureCases)
{
    PEAnalysis analysis;
    std::string error;
    const PEAnalyzer analyzer;
    BOOST_REQUIRE(analyzer.AnalyzeFile(FixturePath("export_fixture.dll"), analysis, error));

    BOOST_CHECK(std::any_of(
        analysis.exports.begin(), analysis.exports.end(),
        [](const PeExport& item) { return item.ordinal == 2 && item.names.empty(); }));
    BOOST_CHECK(std::any_of(
        analysis.exports.begin(), analysis.exports.end(),
        [](const PeExport& item) { return item.forwarder == "KERNEL32.Sleep"; }));
    BOOST_CHECK(std::any_of(
        analysis.exports.begin(), analysis.exports.end(),
        [](const PeExport& item)
        {
            return item.signature == "int __cdecl AddNumbers(int,int)" &&
                item.signatureSource == "export-decoration";
        }));
}
