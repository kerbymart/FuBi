#include "PEAnalyzer.h"

#include <bcrypt.h>
#include <DbgHelp.h>
#include <delayimp.h>
#include <Zydis/Zydis.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace
{
const PeDataDirectory* FindDirectory(const PEImage& image, uint32_t index)
{
    for (const PeDataDirectory& directory : image.Headers().dataDirectories)
    {
        if (directory.index == index && directory.rva != 0 && directory.size != 0)
            return &directory;
    }
    return nullptr;
}

std::string HexBytes(const std::vector<uint8_t>& bytes)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const uint8_t byte : bytes) output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}

std::string CalculateSha256(const std::vector<uint8_t>& bytes)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0;
    DWORD hashSize = 0;
    DWORD resultSize = 0;
    std::vector<uint8_t> object;
    std::vector<uint8_t> digest;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &resultSize, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashSize), sizeof(hashSize), &resultSize, 0) < 0)
    {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    object.resize(objectSize);
    digest.resize(hashSize);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) < 0)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    size_t offset = 0;
    while (offset < bytes.size())
    {
        const ULONG chunk = static_cast<ULONG>(std::min<size_t>(
            bytes.size() - offset, std::numeric_limits<ULONG>::max()));
        if (BCryptHashData(hash, const_cast<PUCHAR>(bytes.data() + offset), chunk, 0) < 0)
        {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return {};
        }
        offset += chunk;
    }

    const bool success = BCryptFinishHash(hash, digest.data(), hashSize, 0) >= 0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return success ? HexBytes(digest) : std::string();
}

std::string Demangle(const std::string& name)
{
    std::array<char, 4096> output = {};
    if (UnDecorateSymbolName(name.c_str(), output.data(),
            static_cast<DWORD>(output.size()), UNDNAME_COMPLETE) == 0 ||
        name == output.data())
    {
        return {};
    }
    return output.data();
}

bool IsPrintableAscii(uint8_t value)
{
    return value >= 0x20 && value <= 0x7E;
}

std::string GuidString(const GUID& guid)
{
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0')
           << std::setw(8) << guid.Data1 << '-'
           << std::setw(4) << guid.Data2 << '-'
           << std::setw(4) << guid.Data3 << '-'
           << std::setw(2) << static_cast<unsigned>(guid.Data4[0])
           << std::setw(2) << static_cast<unsigned>(guid.Data4[1]) << '-';
    for (size_t index = 2; index < 8; ++index)
        output << std::setw(2) << static_cast<unsigned>(guid.Data4[index]);
    return output.str();
}

uint32_t DelayValueToRva(uint32_t value, bool rvaBased, uint64_t imageBase)
{
    if (rvaBased) return value;
    if (value < imageBase || static_cast<uint64_t>(value) - imageBase > UINT32_MAX) return 0;
    return static_cast<uint32_t>(static_cast<uint64_t>(value) - imageBase);
}

void ParseThunkTable(
    const PEImage& image, uint32_t lookupRva, uint32_t iatRva,
    PeImportModule& module, std::vector<std::string>& warnings)
{
    const bool pe64 = image.Headers().isPe32Plus;
    const uint32_t entrySize = pe64 ? sizeof(uint64_t) : sizeof(uint32_t);
    const uint64_t ordinalFlag = pe64 ? IMAGE_ORDINAL_FLAG64 : IMAGE_ORDINAL_FLAG32;
    const size_t maximumEntries = std::min<size_t>(
        image.Bytes().size() / entrySize + 1, 1'000'000);

    for (size_t index = 0; index < maximumEntries; ++index)
    {
        const uint64_t entryRva64 = static_cast<uint64_t>(lookupRva) + index * entrySize;
        if (entryRva64 > UINT32_MAX)
        {
            warnings.push_back("Import thunk RVA overflow");
            return;
        }

        uint64_t value = 0;
        if (pe64)
        {
            if (!image.ReadRva(static_cast<uint32_t>(entryRva64), value))
            {
                warnings.push_back("Truncated import thunk table for " + module.name);
                return;
            }
        }
        else
        {
            uint32_t value32 = 0;
            if (!image.ReadRva(static_cast<uint32_t>(entryRva64), value32))
            {
                warnings.push_back("Truncated import thunk table for " + module.name);
                return;
            }
            value = value32;
        }
        if (value == 0) return;

        PeImport symbol;
        symbol.thunkRva = static_cast<uint32_t>(entryRva64);
        const uint64_t iatEntry = static_cast<uint64_t>(iatRva) + index * entrySize;
        if (iatEntry <= UINT32_MAX) symbol.iatRva = static_cast<uint32_t>(iatEntry);

        if ((value & ordinalFlag) != 0)
        {
            symbol.byOrdinal = true;
            symbol.ordinal = static_cast<uint16_t>(value & 0xFFFF);
        }
        else if (value <= UINT32_MAX)
        {
            const uint32_t importByNameRva = static_cast<uint32_t>(value);
            if (!image.ReadRva(importByNameRva, symbol.hint) ||
                !image.ReadCStringAtRva(importByNameRva + sizeof(uint16_t), symbol.name))
            {
                warnings.push_back("Invalid import-by-name entry for " + module.name);
                return;
            }
        }
        else
        {
            warnings.push_back("Invalid import name RVA for " + module.name);
            return;
        }
        module.symbols.push_back(std::move(symbol));
    }
    warnings.push_back("Import thunk table exceeded safety limit for " + module.name);
}

template <typename T>
void AppendUnique(std::vector<T>& values, const T& value)
{
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

std::string InstructionBytes(const uint8_t* bytes, size_t length)
{
    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0');
    for (size_t index = 0; index < length; ++index)
    {
        if (index != 0) output << ' ';
        output << std::setw(2) << static_cast<unsigned>(bytes[index]);
    }
    return output.str();
}

std::string ImportLabel(const PeImportModule& module, const PeImport& symbol)
{
    return module.name + "!" +
        (symbol.byOrdinal ? ("#" + std::to_string(symbol.ordinal)) : symbol.name);
}

int ArgumentRegisterIndex(ZydisMachineMode mode, ZydisRegister value)
{
    const ZydisRegister enclosing = ZydisRegisterGetLargestEnclosing(mode, value);
    if (enclosing == ZYDIS_REGISTER_RCX) return 0;
    if (enclosing == ZYDIS_REGISTER_RDX) return 1;
    if (enclosing == ZYDIS_REGISTER_R8) return 2;
    if (enclosing == ZYDIS_REGISTER_R9) return 3;
    return -1;
}

uint32_t VaToRva(uint64_t va, const PEImage& image)
{
    const uint64_t base = image.Headers().preferredImageBase;
    if (va < base || va - base > UINT32_MAX) return 0;
    return static_cast<uint32_t>(va - base);
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool HasImportNamed(const PEAnalysis& analysis, const std::string& expected)
{
    const auto contains = [&expected](const std::vector<PeImportModule>& modules)
    {
        for (const PeImportModule& module : modules)
            for (const PeImport& symbol : module.symbols)
                if (Lower(symbol.name) == expected) return true;
        return false;
    };
    return contains(analysis.imports) || contains(analysis.delayImports);
}

bool HasExportNamed(const PEAnalysis& analysis, const std::string& expected)
{
    for (const PeExport& item : analysis.exports)
        for (const std::string& name : item.names)
            if (Lower(name) == expected) return true;
    return false;
}

std::string RvaLabel(uint32_t rva)
{
    std::ostringstream value;
    value << "RVA 0x" << std::hex << std::uppercase << rva;
    return value.str();
}

std::string ReadUtf16Label(const PEImage& image, uint32_t rva)
{
    std::string value;
    for (size_t index = 0; index < 128; ++index)
    {
        wchar_t character = 0;
        const uint64_t characterRva = static_cast<uint64_t>(rva) + index * sizeof(character);
        if (characterRva > UINT32_MAX ||
            !image.ReadRva(static_cast<uint32_t>(characterRva), character) || character == 0)
            break;
        if (character < 0x20 || character > 0x7E) return {};
        value.push_back(static_cast<char>(character));
    }
    return value;
}

const char* UmdfUsbFunctionName(uint32_t slot)
{
    static const char* names[] = {
        "WdfUsbTargetDeviceCreate",
        "WdfUsbTargetDeviceCreateWithParameters",
        "WdfUsbTargetDeviceRetrieveInformation",
        "WdfUsbTargetDeviceGetDeviceDescriptor",
        "WdfUsbTargetDeviceRetrieveConfigDescriptor",
        "WdfUsbTargetDeviceQueryString",
        "WdfUsbTargetDeviceAllocAndQueryString",
        "WdfUsbTargetDeviceFormatRequestForString",
        "WdfUsbTargetDeviceGetNumInterfaces",
        "WdfUsbTargetDeviceSelectConfig",
        "WdfUsbTargetDeviceSendControlTransferSynchronously",
        "WdfUsbTargetDeviceFormatRequestForControlTransfer",
        "WdfUsbTargetDeviceResetPortSynchronously",
        "WdfUsbTargetDeviceQueryUsbCapability",
        "WdfUsbTargetPipeGetInformation",
        "WdfUsbTargetPipeIsInEndpoint",
        "WdfUsbTargetPipeIsOutEndpoint",
        "WdfUsbTargetPipeGetType",
        "WdfUsbTargetPipeSetNoMaximumPacketSizeCheck",
        "WdfUsbTargetPipeWriteSynchronously",
        "WdfUsbTargetPipeFormatRequestForWrite",
        "WdfUsbTargetPipeReadSynchronously",
        "WdfUsbTargetPipeFormatRequestForRead",
        "WdfUsbTargetPipeConfigContinuousReader",
        "WdfUsbTargetPipeAbortSynchronously",
        "WdfUsbTargetPipeFormatRequestForAbort",
        "WdfUsbTargetPipeResetSynchronously",
        "WdfUsbTargetPipeFormatRequestForReset",
        "WdfUsbInterfaceGetInterfaceNumber",
        "WdfUsbInterfaceGetNumEndpoints",
        "WdfUsbInterfaceGetDescriptor",
        "WdfUsbInterfaceGetNumSettings",
        "WdfUsbInterfaceSelectSetting",
        "WdfUsbInterfaceGetEndpointInformation",
        "WdfUsbTargetDeviceGetInterface",
        "WdfUsbInterfaceGetConfiguredSettingIndex",
        "WdfUsbInterfaceGetNumConfiguredPipes",
        "WdfUsbInterfaceGetConfiguredPipe"
    };
    return slot >= 202 && slot < 202 + _countof(names) ? names[slot - 202] : nullptr;
}

const char* KmdfUsbFunctionName(uint32_t slot)
{
    static const char* names[] = {
        "WdfUsbTargetDeviceCreate",
        "WdfUsbTargetDeviceRetrieveInformation",
        "WdfUsbTargetDeviceGetDeviceDescriptor",
        "WdfUsbTargetDeviceRetrieveConfigDescriptor",
        "WdfUsbTargetDeviceQueryString",
        "WdfUsbTargetDeviceAllocAndQueryString",
        "WdfUsbTargetDeviceFormatRequestForString",
        "WdfUsbTargetDeviceGetNumInterfaces",
        "WdfUsbTargetDeviceSelectConfig",
        "WdfUsbTargetDeviceWdmGetConfigurationHandle",
        "WdfUsbTargetDeviceRetrieveCurrentFrameNumber",
        "WdfUsbTargetDeviceSendControlTransferSynchronously",
        "WdfUsbTargetDeviceFormatRequestForControlTransfer",
        "WdfUsbTargetDeviceIsConnectedSynchronous",
        "WdfUsbTargetDeviceResetPortSynchronously",
        "WdfUsbTargetDeviceCyclePortSynchronously",
        "WdfUsbTargetDeviceFormatRequestForCyclePort",
        "WdfUsbTargetDeviceSendUrbSynchronously",
        "WdfUsbTargetDeviceFormatRequestForUrb",
        "WdfUsbTargetPipeGetInformation",
        "WdfUsbTargetPipeIsInEndpoint",
        "WdfUsbTargetPipeIsOutEndpoint",
        "WdfUsbTargetPipeGetType",
        "WdfUsbTargetPipeSetNoMaximumPacketSizeCheck",
        "WdfUsbTargetPipeWriteSynchronously",
        "WdfUsbTargetPipeFormatRequestForWrite",
        "WdfUsbTargetPipeReadSynchronously",
        "WdfUsbTargetPipeFormatRequestForRead",
        "WdfUsbTargetPipeConfigContinuousReader",
        "WdfUsbTargetPipeAbortSynchronously",
        "WdfUsbTargetPipeFormatRequestForAbort",
        "WdfUsbTargetPipeResetSynchronously",
        "WdfUsbTargetPipeFormatRequestForReset",
        "WdfUsbTargetPipeSendUrbSynchronously",
        "WdfUsbTargetPipeFormatRequestForUrb",
        "WdfUsbInterfaceGetInterfaceNumber",
        "WdfUsbInterfaceGetNumEndpoints",
        "WdfUsbInterfaceGetDescriptor",
        "WdfUsbInterfaceSelectSetting",
        "WdfUsbInterfaceGetEndpointInformation",
        "WdfUsbTargetDeviceGetInterface",
        "WdfUsbInterfaceGetConfiguredSettingIndex",
        "WdfUsbInterfaceGetNumConfiguredPipes",
        "WdfUsbInterfaceGetConfiguredPipe",
        "WdfUsbTargetPipeWdmGetPipeHandle"
    };
    if (slot >= 322 && slot < 322 + _countof(names)) return names[slot - 322];
    if (slot == 386) return "WdfUsbInterfaceGetNumSettings";
    switch (slot)
    {
    case 421: return "WdfUsbTargetDeviceCreateWithParameters";
    case 422: return "WdfUsbTargetDeviceQueryUsbCapability";
    case 423: return "WdfUsbTargetDeviceCreateUrb";
    case 424: return "WdfUsbTargetDeviceCreateIsochUrb";
    default: return nullptr;
    }
}

const char* WdfFunctionName(const PeFrameworkBinding& binding, uint32_t slot)
{
    if (binding.majorVersion == 2) return UmdfUsbFunctionName(slot);
    if (binding.majorVersion == 1) return KmdfUsbFunctionName(slot);
    return nullptr;
}

struct WdfDispatchReference
{
    size_t bindingIndex = 0;
    uint32_t slot = 0;
    uint32_t expectedConsumerRva = 0;
};

void AnnotateWdfCall(
    PeInstruction& instruction, PeFunction& function,
    const PeFrameworkBinding& binding, uint32_t slot,
    const std::string& dispatchProvenance)
{
    const char* functionName = WdfFunctionName(binding, slot);
    instruction.frameworkCall = functionName
        ? functionName : ("slot " + std::to_string(slot));
    instruction.frameworkSlot = slot;
    instruction.frameworkCallConfidence = functionName ? "high" : "medium";
    instruction.frameworkCallProvenance = binding.provenance + "; " + dispatchProvenance;
    instruction.annotation = binding.framework + "!" + instruction.frameworkCall +
        " [slot " + std::to_string(slot) + "]";
    AppendUnique(function.importedCalls, instruction.annotation);
}

std::string ResourceTypeName(uint32_t identifier)
{
    switch (identifier)
    {
    case 1: return "CURSOR";
    case 2: return "BITMAP";
    case 3: return "ICON";
    case 4: return "MENU";
    case 5: return "DIALOG";
    case 6: return "STRING";
    case 9: return "ACCELERATOR";
    case 10: return "RCDATA";
    case 11: return "MESSAGETABLE";
    case 12: return "GROUP_CURSOR";
    case 14: return "GROUP_ICON";
    case 16: return "VERSION";
    case 24: return "MANIFEST";
    default: return "#" + std::to_string(identifier);
    }
}
}

PEAnalyzer::PEAnalyzer(size_t minimumStringLength)
    : minimumStringLength_(std::max<size_t>(minimumStringLength, 2))
{
}

bool PEAnalyzer::AnalyzeFile(
    const std::string& path, PEAnalysis& analysis, std::string& error) const
{
    PEImage image;
    if (!PEImage::Load(path, image, error)) return false;
    if (!AnalyzeImage(image, analysis, error)) return false;
    ParseVersionInfo(path, analysis);
    Classify(analysis);
    return true;
}

bool PEAnalyzer::AnalyzeImage(
    const PEImage& image, PEAnalysis& analysis, std::string& error) const
{
    (void)error;
    analysis = {};
    analysis.path = image.SourceName();
    analysis.fileSize = image.Bytes().size();
    analysis.sha256 = CalculateSha256(image.Bytes());
    analysis.headers = image.Headers();
    analysis.sections = image.Sections();
    ParseExports(image, analysis);
    ParseImports(image, analysis);
    ParseDelayImports(image, analysis);
    ParseStrings(image, analysis);
    ParseDebug(image, analysis);
    ParseRuntimeFunctions(image, analysis);
    ParseLoadConfig(image, analysis);
    ParseFrameworkBindings(image, analysis);
    DisassembleFunctions(image, analysis);
    BuildCallGraph(analysis);
    ParseResources(image, analysis);
    Classify(analysis);
    return true;
}

void PEAnalyzer::ParseExports(const PEImage& image, PEAnalysis& analysis) const
{
    const PeDataDirectory* directory = FindDirectory(image, IMAGE_DIRECTORY_ENTRY_EXPORT);
    if (!directory) return;

    IMAGE_EXPORT_DIRECTORY table = {};
    if (!image.ReadRva(directory->rva, table))
    {
        analysis.warnings.push_back("Invalid export directory");
        return;
    }
    if (table.NumberOfFunctions > 1'000'000 || table.NumberOfNames > 1'000'000)
    {
        analysis.warnings.push_back("Export table exceeds safety limit");
        return;
    }

    std::unordered_map<uint32_t, std::vector<std::string>> names;
    for (uint32_t index = 0; index < table.NumberOfNames; ++index)
    {
        const uint64_t nameEntryRva = static_cast<uint64_t>(table.AddressOfNames) +
            static_cast<uint64_t>(index) * sizeof(uint32_t);
        const uint64_t ordinalEntryRva = static_cast<uint64_t>(table.AddressOfNameOrdinals) +
            static_cast<uint64_t>(index) * sizeof(uint16_t);
        uint32_t nameRva = 0;
        uint16_t functionIndex = 0;
        if (nameEntryRva > UINT32_MAX || ordinalEntryRva > UINT32_MAX ||
            !image.ReadRva(static_cast<uint32_t>(nameEntryRva), nameRva) ||
            !image.ReadRva(static_cast<uint32_t>(ordinalEntryRva), functionIndex))
        {
            analysis.warnings.push_back("Truncated export name table");
            break;
        }
        std::string name;
        if (functionIndex < table.NumberOfFunctions && image.ReadCStringAtRva(nameRva, name))
            names[functionIndex].push_back(std::move(name));
    }

    const uint64_t exportEnd = static_cast<uint64_t>(directory->rva) + directory->size;
    for (uint32_t index = 0; index < table.NumberOfFunctions; ++index)
    {
        const uint64_t functionEntryRva = static_cast<uint64_t>(table.AddressOfFunctions) +
            static_cast<uint64_t>(index) * sizeof(uint32_t);
        uint32_t functionRva = 0;
        if (functionEntryRva > UINT32_MAX ||
            !image.ReadRva(static_cast<uint32_t>(functionEntryRva), functionRva))
        {
            analysis.warnings.push_back("Truncated export address table");
            break;
        }
        if (functionRva == 0) continue;

        PeExport item;
        item.ordinal = table.Base + index;
        item.rva = functionRva;
        item.names = names[index];
        if (functionRva >= directory->rva && functionRva < exportEnd)
            image.ReadCStringAtRva(functionRva, item.forwarder);

        for (const std::string& name : item.names)
        {
            item.signature = Demangle(name);
            if (!item.signature.empty())
            {
                item.signatureSource = "export-decoration";
                break;
            }
        }
        analysis.exports.push_back(std::move(item));
    }
}

void PEAnalyzer::ParseImports(const PEImage& image, PEAnalysis& analysis) const
{
    const PeDataDirectory* directory = FindDirectory(image, IMAGE_DIRECTORY_ENTRY_IMPORT);
    if (!directory) return;

    const size_t maximum = std::min<size_t>(
        directory->size / sizeof(IMAGE_IMPORT_DESCRIPTOR) + 1, 100'000);
    for (size_t index = 0; index < maximum; ++index)
    {
        IMAGE_IMPORT_DESCRIPTOR descriptor = {};
        const uint64_t rva = static_cast<uint64_t>(directory->rva) +
            index * sizeof(descriptor);
        if (rva > UINT32_MAX || !image.ReadRva(static_cast<uint32_t>(rva), descriptor))
        {
            analysis.warnings.push_back("Truncated import directory");
            return;
        }
        if (descriptor.Name == 0 && descriptor.FirstThunk == 0 &&
            descriptor.OriginalFirstThunk == 0) return;

        PeImportModule module;
        if (!image.ReadCStringAtRva(descriptor.Name, module.name))
        {
            analysis.warnings.push_back("Invalid imported module name");
            return;
        }
        const uint32_t lookup = descriptor.OriginalFirstThunk != 0
            ? descriptor.OriginalFirstThunk : descriptor.FirstThunk;
        ParseThunkTable(image, lookup, descriptor.FirstThunk, module, analysis.warnings);
        analysis.imports.push_back(std::move(module));
    }
    analysis.warnings.push_back("Import directory exceeded safety limit");
}

void PEAnalyzer::ParseDelayImports(const PEImage& image, PEAnalysis& analysis) const
{
    const PeDataDirectory* directory = FindDirectory(image, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
    if (!directory) return;

    const size_t maximum = std::min<size_t>(
        directory->size / sizeof(ImgDelayDescr) + 1, 100'000);
    for (size_t index = 0; index < maximum; ++index)
    {
        ImgDelayDescr descriptor = {};
        const uint64_t rva = static_cast<uint64_t>(directory->rva) + index * sizeof(descriptor);
        if (rva > UINT32_MAX || !image.ReadRva(static_cast<uint32_t>(rva), descriptor))
        {
            analysis.warnings.push_back("Truncated delay-import directory");
            return;
        }
        if (descriptor.rvaDLLName == 0 && descriptor.rvaIAT == 0 && descriptor.rvaINT == 0)
            return;

        const bool rvaBased = (descriptor.grAttrs & dlattrRva) != 0;
        const uint64_t imageBase = image.Headers().preferredImageBase;
        const uint32_t nameRva = DelayValueToRva(descriptor.rvaDLLName, rvaBased, imageBase);
        const uint32_t iatRva = DelayValueToRva(descriptor.rvaIAT, rvaBased, imageBase);
        const uint32_t intRva = DelayValueToRva(descriptor.rvaINT, rvaBased, imageBase);

        PeImportModule module;
        if (nameRva == 0 || iatRva == 0 || intRva == 0 ||
            !image.ReadCStringAtRva(nameRva, module.name))
        {
            analysis.warnings.push_back("Invalid delay-import descriptor");
            return;
        }
        ParseThunkTable(image, intRva, iatRva, module, analysis.warnings);
        analysis.delayImports.push_back(std::move(module));
    }
    analysis.warnings.push_back("Delay-import directory exceeded safety limit");
}

void PEAnalyzer::ParseStrings(const PEImage& image, PEAnalysis& analysis) const
{
    const std::vector<uint8_t>& bytes = image.Bytes();
    for (const PeSection& section : image.Sections())
    {
        if (section.rawSize == 0 || section.rawOffset >= bytes.size()) continue;
        const size_t begin = section.rawOffset;
        const size_t end = std::min<size_t>(bytes.size(), begin + section.rawSize);

        size_t cursor = begin;
        while (cursor < end)
        {
            const size_t start = cursor;
            while (cursor < end && IsPrintableAscii(bytes[cursor])) ++cursor;
            if (cursor - start >= minimumStringLength_)
            {
                PeString item;
                item.fileOffset = static_cast<uint32_t>(start);
                item.rva = section.rva + static_cast<uint32_t>(start - begin);
                item.section = section.name;
                item.encoding = "ASCII";
                item.value.assign(reinterpret_cast<const char*>(bytes.data() + start), cursor - start);
                analysis.strings.push_back(std::move(item));
            }
            cursor = start == cursor ? cursor + 1 : cursor;
        }

        cursor = begin;
        while (cursor + 1 < end)
        {
            const size_t start = cursor;
            std::string value;
            while (cursor + 1 < end && IsPrintableAscii(bytes[cursor]) && bytes[cursor + 1] == 0)
            {
                value.push_back(static_cast<char>(bytes[cursor]));
                cursor += 2;
            }
            if (value.size() >= minimumStringLength_)
            {
                PeString item;
                item.fileOffset = static_cast<uint32_t>(start);
                item.rva = section.rva + static_cast<uint32_t>(start - begin);
                item.section = section.name;
                item.encoding = "UTF-16LE";
                item.value = std::move(value);
                analysis.strings.push_back(std::move(item));
            }
            cursor = start == cursor ? cursor + 1 : cursor;
        }
    }
}

void PEAnalyzer::ParseDebug(const PEImage& image, PEAnalysis& analysis) const
{
    const PeDataDirectory* directory = FindDirectory(image, IMAGE_DIRECTORY_ENTRY_DEBUG);
    if (!directory) return;

    const size_t count = std::min<size_t>(
        directory->size / sizeof(IMAGE_DEBUG_DIRECTORY), 100'000);
    for (size_t index = 0; index < count; ++index)
    {
        IMAGE_DEBUG_DIRECTORY raw = {};
        const uint64_t rva = static_cast<uint64_t>(directory->rva) + index * sizeof(raw);
        if (rva > UINT32_MAX || !image.ReadRva(static_cast<uint32_t>(rva), raw))
        {
            analysis.warnings.push_back("Truncated debug directory");
            return;
        }

        PeDebugEntry entry;
        entry.type = raw.Type;
        entry.timestamp = raw.TimeDateStamp;
        if (raw.Type == IMAGE_DEBUG_TYPE_CODEVIEW && raw.SizeOfData >= 4)
        {
            std::array<char, 4> signature = {};
            if (image.ReadFile(raw.PointerToRawData, signature.data(), signature.size()))
            {
                entry.codeViewFormat.assign(signature.data(), signature.size());
                if (entry.codeViewFormat == "RSDS" && raw.SizeOfData >= 24)
                {
                    GUID guid = {};
                    image.ReadFile(raw.PointerToRawData + 4, guid);
                    image.ReadFile(raw.PointerToRawData + 20, entry.pdbAge);
                    image.ReadCStringAtFileOffset(
                        raw.PointerToRawData + 24, entry.pdbPath, raw.SizeOfData - 24);
                    entry.pdbGuid = GuidString(guid);
                }
                else if (entry.codeViewFormat == "NB10" && raw.SizeOfData >= 16)
                {
                    image.ReadFile(raw.PointerToRawData + 12, entry.pdbAge);
                    image.ReadCStringAtFileOffset(
                        raw.PointerToRawData + 16, entry.pdbPath, raw.SizeOfData - 16);
                }
            }
        }
        else if (raw.Type == 20 && raw.SizeOfData >= sizeof(uint32_t))
        {
            uint32_t extendedDllCharacteristics = 0;
            if (image.ReadFile(raw.PointerToRawData, extendedDllCharacteristics))
                analysis.security.cetCompatible =
                    (extendedDllCharacteristics & 0x00000001U) != 0;
        }
        analysis.debugEntries.push_back(std::move(entry));
    }
}

void PEAnalyzer::ParseRuntimeFunctions(const PEImage& image, PEAnalysis& analysis) const
{
    if (!image.Headers().isPe32Plus) return;
    const PeDataDirectory* directory = FindDirectory(image, IMAGE_DIRECTORY_ENTRY_EXCEPTION);
    if (!directory) return;

    struct RuntimeEntry
    {
        uint32_t begin;
        uint32_t end;
        uint32_t unwind;
    };
    static_assert(sizeof(RuntimeEntry) == 12, "Unexpected runtime-function entry size");

    const size_t count = std::min<size_t>(directory->size / sizeof(RuntimeEntry), 1'000'000);
    for (size_t index = 0; index < count; ++index)
    {
        const uint64_t entryRva = static_cast<uint64_t>(directory->rva) +
            index * sizeof(RuntimeEntry);
        RuntimeEntry raw = {};
        if (entryRva > UINT32_MAX ||
            !image.ReadRva(static_cast<uint32_t>(entryRva), raw))
        {
            analysis.warnings.push_back("Truncated x64 exception directory");
            return;
        }
        if (raw.begin == 0 || raw.end <= raw.begin || raw.end > image.Headers().imageSize)
        {
            analysis.warnings.push_back("Invalid runtime-function boundary");
            continue;
        }
        analysis.runtimeFunctions.push_back({raw.begin, raw.end, raw.unwind});
    }
}

void PEAnalyzer::ParseFrameworkBindings(const PEImage& image, PEAnalysis& analysis) const
{
    const bool hasWdfEntry = HasExportNamed(analysis, "fxdriverentryum");
    const bool hasWdfBindImport = HasImportNamed(analysis, "wdfversionbind") ||
        HasImportNamed(analysis, "wdfversionbindclass");
    if (!hasWdfEntry && !hasWdfBindImport) return;

    const bool pe64 = image.Headers().isPe32Plus;
    const uint32_t minimumSize = pe64 ? 48 : 32;
    const uint32_t componentOffset = pe64 ? 8 : 4;
    const uint32_t versionOffset = pe64 ? 16 : 8;
    const uint32_t functionCountOffset = pe64 ? 28 : 20;
    const uint32_t functionTableOffset = pe64 ? 32 : 24;
    const uint32_t alignment = pe64 ? 8 : 4;

    for (const PeSection& section : image.Sections())
    {
        if (section.rawSize < minimumSize) continue;
        const uint64_t sectionEnd = static_cast<uint64_t>(section.rva) + section.rawSize;
        for (uint64_t candidate = section.rva;
             candidate + minimumSize <= sectionEnd && candidate <= UINT32_MAX;
             candidate += alignment)
        {
            const uint32_t rva = static_cast<uint32_t>(candidate);
            uint32_t size = 0;
            uint32_t major = 0;
            uint32_t minor = 0;
            uint32_t build = 0;
            uint32_t functionCount = 0;
            if (!image.ReadRva(rva, size) || size < minimumSize || size > 256 ||
                !image.ReadRva(rva + versionOffset, major) ||
                !image.ReadRva(rva + versionOffset + 4, minor) ||
                !image.ReadRva(rva + versionOffset + 8, build) ||
                !image.ReadRva(rva + functionCountOffset, functionCount) ||
                (major != 1 && major != 2) || minor > 100 || build > 1'000'000 ||
                functionCount < 100 || functionCount > 4096)
                continue;

            uint64_t componentVa = 0;
            uint64_t functionTableVa = 0;
            if (pe64)
            {
                if (!image.ReadRva(rva + componentOffset, componentVa) ||
                    !image.ReadRva(rva + functionTableOffset, functionTableVa))
                    continue;
            }
            else
            {
                uint32_t componentVa32 = 0;
                uint32_t functionTableVa32 = 0;
                if (!image.ReadRva(rva + componentOffset, componentVa32) ||
                    !image.ReadRva(rva + functionTableOffset, functionTableVa32))
                    continue;
                componentVa = componentVa32;
                functionTableVa = functionTableVa32;
            }

            const uint32_t componentRva = VaToRva(componentVa, image);
            const uint32_t functionTableRva = VaToRva(functionTableVa, image);
            if (componentRva == 0 || functionTableRva == 0 ||
                !image.FindSection(componentRva) || !image.FindSection(functionTableRva))
                continue;
            const std::string component = ReadUtf16Label(image, componentRva);
            if (component.empty()) continue;

            const bool duplicate = std::any_of(
                analysis.frameworkBindings.begin(), analysis.frameworkBindings.end(),
                [functionTableRva](const PeFrameworkBinding& binding)
                {
                    return binding.functionTableRva == functionTableRva;
                });
            if (duplicate) continue;

            PeFrameworkBinding binding;
            binding.framework = major == 2 ? "UMDF" : "KMDF";
            binding.majorVersion = major;
            binding.minorVersion = minor;
            binding.buildVersion = build;
            binding.functionCount = functionCount;
            binding.functionTableRva = functionTableRva;
            binding.indirectFunctionTable = major >= 2 || minor >= 15;
            binding.confidence = "high";
            binding.provenance = "WDF_BIND_INFO at " + RvaLabel(rva) +
                ", component \"" + component + "\"";
            analysis.frameworkBindings.push_back(std::move(binding));
        }
    }
}

void PEAnalyzer::DisassembleFunctions(const PEImage& image, PEAnalysis& analysis) const
{
    std::unordered_map<uint32_t, std::string> importsByIat;
    const auto indexImports = [&importsByIat](const std::vector<PeImportModule>& modules)
    {
        for (const PeImportModule& module : modules)
            for (const PeImport& symbol : module.symbols)
                importsByIat[symbol.iatRva] = ImportLabel(module, symbol);
    };
    indexImports(analysis.imports);
    indexImports(analysis.delayImports);

    std::unordered_map<uint32_t, const PeString*> stringsByRva;
    for (const PeString& item : analysis.strings) stringsByRva[item.rva] = &item;

    analysis.functions.clear();
    analysis.functions.reserve(analysis.runtimeFunctions.size());
    for (const PeRuntimeFunction& runtime : analysis.runtimeFunctions)
    {
        PeFunction function;
        function.beginRva = runtime.beginRva;
        function.endRva = runtime.endRva;
        function.unwindRva = runtime.unwindRva;
        const std::optional<uint32_t> functionOffset = image.RvaToFileOffset(runtime.beginRva);
        if (functionOffset.has_value()) function.fileOffset = *functionOffset;
        const PeSection* functionSection = image.FindSection(runtime.beginRva);
        if (functionSection) function.section = functionSection->name;
        function.name = "sub_" + [&runtime]()
        {
            std::ostringstream value;
            value << std::hex << std::uppercase << runtime.beginRva;
            return value.str();
        }();

        for (const PeExport& item : analysis.exports)
        {
            if (item.rva < runtime.beginRva || item.rva >= runtime.endRva) continue;
            if (!item.names.empty())
            {
                function.name = item.names.front();
                function.nameSource = "pe-export";
                function.nameConfidence = "high";
                function.aliases = item.names;
            }
        }
        analysis.functions.push_back(std::move(function));
    }

    // Leaf functions may not have x64 unwind records. Preserve exported entry
    // points as heuristic boundaries when .pdata does not contain them.
    for (const PeExport& item : analysis.exports)
    {
        if (item.names.empty() || std::any_of(
                analysis.functions.begin(), analysis.functions.end(),
                [&item](const PeFunction& function)
                {
                    return item.rva >= function.beginRva && item.rva < function.endRva;
                }))
            continue;

        const PeSection* section = image.FindSection(item.rva);
        if (!section || !section->executable) continue;
        uint64_t end = static_cast<uint64_t>(section->rva) +
            std::max(section->virtualSize, section->rawSize);
        for (const PeRuntimeFunction& runtime : analysis.runtimeFunctions)
            if (runtime.beginRva > item.rva) end = std::min<uint64_t>(end, runtime.beginRva);
        for (const PeExport& other : analysis.exports)
            if (other.rva > item.rva) end = std::min<uint64_t>(end, other.rva);
        if (end <= item.rva || end > UINT32_MAX) continue;

        PeFunction function;
        function.beginRva = item.rva;
        function.endRva = static_cast<uint32_t>(end);
        function.name = item.names.front();
        function.nameSource = "pe-export";
        function.nameConfidence = "high";
        function.boundarySource = "export-heuristic";
        function.boundaryConfidence = "low";
        function.aliases = item.names;
        const std::optional<uint32_t> offset = image.RvaToFileOffset(item.rva);
        if (offset.has_value()) function.fileOffset = *offset;
        function.section = section->name;
        analysis.functions.push_back(std::move(function));
    }

    for (const uint32_t guardRva : analysis.security.guardFunctionRvas)
    {
        if (std::any_of(
                analysis.functions.begin(), analysis.functions.end(),
                [guardRva](const PeFunction& function)
                {
                    return guardRva >= function.beginRva && guardRva < function.endRva;
                }))
            continue;
        const PeSection* section = image.FindSection(guardRva);
        if (!section || !section->executable) continue;
        uint64_t end = static_cast<uint64_t>(section->rva) +
            std::max(section->virtualSize, section->rawSize);
        for (const PeRuntimeFunction& runtime : analysis.runtimeFunctions)
            if (runtime.beginRva > guardRva) end = std::min<uint64_t>(end, runtime.beginRva);
        for (const uint32_t other : analysis.security.guardFunctionRvas)
            if (other > guardRva) end = std::min<uint64_t>(end, other);
        if (end <= guardRva || end > UINT32_MAX) continue;

        PeFunction function;
        function.beginRva = guardRva;
        function.endRva = static_cast<uint32_t>(end);
        std::ostringstream name;
        name << "guard_" << std::hex << std::uppercase << guardRva;
        function.name = name.str();
        function.nameSource = "heuristic";
        function.boundarySource = "guard-cf";
        function.boundaryConfidence = "low";
        const std::optional<uint32_t> offset = image.RvaToFileOffset(guardRva);
        if (offset.has_value()) function.fileOffset = *offset;
        function.section = section->name;
        analysis.functions.push_back(std::move(function));
    }

    std::sort(
        analysis.functions.begin(), analysis.functions.end(),
        [](const PeFunction& left, const PeFunction& right)
        {
            return left.beginRva < right.beginRva;
        });

    ZydisDecoder decoder;
    ZydisFormatter formatter;
    const ZydisMachineMode machineMode = image.Headers().isPe32Plus
        ? ZYDIS_MACHINE_MODE_LONG_64 : ZYDIS_MACHINE_MODE_LEGACY_32;
    const ZydisStackWidth stackWidth = image.Headers().isPe32Plus
        ? ZYDIS_STACK_WIDTH_64 : ZYDIS_STACK_WIDTH_32;
    if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, machineMode, stackWidth)) ||
        !ZYAN_SUCCESS(ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL)))
    {
        analysis.warnings.push_back("Unable to initialize Zydis");
        return;
    }

    for (PeFunction& function : analysis.functions)
    {
        const std::optional<uint32_t> offset = image.RvaToFileOffset(function.beginRva);
        if (!offset.has_value()) continue;
        const uint64_t requestedSize = static_cast<uint64_t>(function.endRva) - function.beginRva;
        if (requestedSize > 1'000'000 || requestedSize > image.Bytes().size() - *offset)
        {
            analysis.warnings.push_back("Function bytes exceed file bounds at RVA " +
                std::to_string(function.beginRva));
            continue;
        }

        size_t consumed = 0;
        size_t decodedCount = 0;
        bool argumentConsumed[4] = {};
        bool argumentOverwritten[4] = {};
        std::unordered_map<ZydisRegister, size_t> wdfTableRegisters;
        std::unordered_map<ZydisRegister, WdfDispatchReference> wdfFunctionRegisters;
        std::unordered_map<uint32_t, std::unordered_map<ZydisRegister, size_t>>
            wdfTableRegistersAtBranchTarget;
        std::unordered_map<uint32_t,
            std::unordered_map<ZydisRegister, WdfDispatchReference>>
            wdfFunctionRegistersAtBranchTarget;
        while (consumed < requestedSize)
        {
            ZydisDecodedInstruction decoded = {};
            ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT] = {};
            const uint8_t* instructionBytes = image.Bytes().data() + *offset + consumed;
            const size_t remaining = static_cast<size_t>(requestedSize) - consumed;
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(
                    &decoder, instructionBytes, remaining, &decoded, operands)))
            {
                ++consumed;
                continue;
            }

            PeInstruction instruction;
            instruction.rva = function.beginRva + static_cast<uint32_t>(consumed);
            const auto savedTables = wdfTableRegistersAtBranchTarget.find(instruction.rva);
            if (savedTables != wdfTableRegistersAtBranchTarget.end())
                for (const auto& entry : savedTables->second)
                    wdfTableRegisters[entry.first] = entry.second;
            const auto savedFunctions = wdfFunctionRegistersAtBranchTarget.find(instruction.rva);
            if (savedFunctions != wdfFunctionRegistersAtBranchTarget.end())
                for (const auto& entry : savedFunctions->second)
                    wdfFunctionRegisters[entry.first] = entry.second;
            instruction.length = decoded.length;
            instruction.bytes = InstructionBytes(instructionBytes, decoded.length);
            std::array<char, 512> text = {};
            if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(
                    &formatter, &decoded, operands, decoded.operand_count_visible,
                    text.data(), text.size(), instruction.rva, nullptr)))
                instruction.text = text.data();

            instruction.isCall = decoded.meta.category == ZYDIS_CATEGORY_CALL;
            if (image.Headers().isPe32Plus && decodedCount < 24)
            {
                for (uint8_t operandIndex = 0; operandIndex < decoded.operand_count; ++operandIndex)
                {
                    const ZydisDecodedOperand& operand = operands[operandIndex];
                    if (operand.type == ZYDIS_OPERAND_TYPE_REGISTER)
                    {
                        const int argument = ArgumentRegisterIndex(machineMode, operand.reg.value);
                        if (argument >= 0)
                        {
                            if ((operand.actions & ZYDIS_OPERAND_ACTION_MASK_READ) != 0 &&
                                !argumentOverwritten[argument])
                                argumentConsumed[argument] = true;
                            if ((operand.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0)
                                argumentOverwritten[argument] = true;
                        }
                    }
                    else if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY)
                    {
                        for (const ZydisRegister reg : {operand.mem.base, operand.mem.index})
                        {
                            const int argument = ArgumentRegisterIndex(machineMode, reg);
                            if (argument >= 0 && !argumentOverwritten[argument])
                                argumentConsumed[argument] = true;
                        }
                    }
                }
            }
            bool resolvedCall = false;
            if (instruction.isCall && decoded.operand_count_visible >= 1 &&
                operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER)
            {
                const ZydisRegister targetRegister = ZydisRegisterGetLargestEnclosing(
                    machineMode, operands[0].reg.value);
                const auto target = wdfFunctionRegisters.find(targetRegister);
                if (target != wdfFunctionRegisters.end() &&
                    target->second.expectedConsumerRva == instruction.rva)
                {
                    const WdfDispatchReference reference = target->second;
                    AnnotateWdfCall(
                        instruction, function,
                        analysis.frameworkBindings[reference.bindingIndex], reference.slot,
                        "function-table slot loaded into call target register");
                    resolvedCall = true;
                }
            }
            for (uint8_t operandIndex = 0;
                 operandIndex < decoded.operand_count_visible; ++operandIndex)
            {
                const ZydisDecodedOperand& operand = operands[operandIndex];
                if (operand.type != ZYDIS_OPERAND_TYPE_IMMEDIATE &&
                    operand.type != ZYDIS_OPERAND_TYPE_MEMORY)
                    continue;

                ZyanU64 absolute = 0;
                if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(
                        &decoded, &operand, instruction.rva, &absolute)) || absolute > UINT32_MAX)
                    continue;
                const uint32_t targetRva = static_cast<uint32_t>(absolute);

                if (instruction.isCall && operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                {
                    instruction.directTargetRva = targetRva;
                    resolvedCall = true;
                }
                if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY)
                {
                    const auto imported = importsByIat.find(targetRva);
                    if (imported != importsByIat.end())
                    {
                        instruction.annotation = imported->second;
                        AppendUnique(function.importedCalls, imported->second);
                        if (instruction.isCall) resolvedCall = true;
                    }

                    if (instruction.isCall)
                    {
                        const ZydisRegister cfgTarget = image.Headers().isPe32Plus
                            ? ZYDIS_REGISTER_RAX : ZYDIS_REGISTER_EAX;
                        const auto pending = wdfFunctionRegisters.find(cfgTarget);
                        if (pending != wdfFunctionRegisters.end() &&
                            pending->second.expectedConsumerRva == instruction.rva)
                        {
                            const WdfDispatchReference reference = pending->second;
                            AnnotateWdfCall(
                                instruction, function,
                                analysis.frameworkBindings[reference.bindingIndex], reference.slot,
                                "function-table slot carried through Control Flow Guard dispatch");
                            resolvedCall = true;
                        }
                    }

                    if (instruction.isCall && instruction.frameworkCall.empty())
                    {
                        const uint32_t pointerSize = image.Headers().isPe32Plus ? 8 : 4;
                        const PeFrameworkBinding* binding = nullptr;
                        uint32_t slot = UINT32_MAX;

                        const ZydisRegister base = ZydisRegisterGetLargestEnclosing(
                            machineMode, operand.mem.base);
                        const auto registerBinding = wdfTableRegisters.find(base);
                        if (registerBinding != wdfTableRegisters.end() &&
                            operand.mem.index == ZYDIS_REGISTER_NONE &&
                            operand.mem.disp.value >= 0 &&
                            static_cast<uint64_t>(operand.mem.disp.value) % pointerSize == 0)
                        {
                            binding = &analysis.frameworkBindings[registerBinding->second];
                            slot = static_cast<uint32_t>(
                                static_cast<uint64_t>(operand.mem.disp.value) / pointerSize);
                        }
                        else
                        {
                            for (const PeFrameworkBinding& candidate : analysis.frameworkBindings)
                            {
                                if (candidate.indirectFunctionTable ||
                                    targetRva < candidate.functionTableRva) continue;
                                const uint32_t difference = targetRva - candidate.functionTableRva;
                                if (difference % pointerSize != 0) continue;
                                const uint32_t candidateSlot = difference / pointerSize;
                                if (candidateSlot >= candidate.functionCount) continue;
                                binding = &candidate;
                                slot = candidateSlot;
                                break;
                            }
                        }

                        if (binding && slot < binding->functionCount)
                        {
                            AnnotateWdfCall(
                                instruction, function, *binding, slot,
                                "dispatch offset mapped using pointer size and framework version");
                            resolvedCall = true;
                        }
                    }
                }

                const auto string = stringsByRva.find(targetRva);
                if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY && string != stringsByRva.end())
                {
                    instruction.referencedStringRvas.push_back(targetRva);
                    AppendUnique(function.referencedStringRvas, targetRva);
                    if (!instruction.annotation.empty()) instruction.annotation += "; ";
                    instruction.annotation += "string \"" + string->second->value + "\"";
                }
            }
            instruction.indirectCall = instruction.isCall && !resolvedCall;
            if (instruction.indirectCall)
            {
                if (!instruction.annotation.empty()) instruction.annotation += "; ";
                instruction.annotation += "indirect call - unresolved";
            }

            if (decoded.operand_count_visible >= 1 &&
                operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                (operands[0].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0)
            {
                const ZydisRegister destination = ZydisRegisterGetLargestEnclosing(
                    machineMode, operands[0].reg.value);
                size_t sourceTableBindingIndex = SIZE_MAX;
                if (decoded.operand_count_visible >= 2 &&
                    operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY)
                {
                    const ZydisRegister sourceBase = ZydisRegisterGetLargestEnclosing(
                        machineMode, operands[1].mem.base);
                    const auto sourceTable = wdfTableRegisters.find(sourceBase);
                    if (sourceTable != wdfTableRegisters.end())
                        sourceTableBindingIndex = sourceTable->second;
                }
                wdfTableRegisters.erase(destination);
                wdfFunctionRegisters.erase(destination);

                if (decoded.operand_count_visible >= 2 &&
                    (decoded.mnemonic == ZYDIS_MNEMONIC_MOV ||
                     decoded.mnemonic == ZYDIS_MNEMONIC_LEA) &&
                    operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
                {
                    const ZydisRegister source = ZydisRegisterGetLargestEnclosing(
                        machineMode, operands[1].reg.value);
                    const auto sourceBinding = wdfTableRegisters.find(source);
                    if (sourceBinding != wdfTableRegisters.end())
                        wdfTableRegisters[destination] = sourceBinding->second;
                    const auto sourceFunction = wdfFunctionRegisters.find(source);
                    if (sourceFunction != wdfFunctionRegisters.end())
                    {
                        WdfDispatchReference reference = sourceFunction->second;
                        reference.expectedConsumerRva = instruction.rva + decoded.length;
                        wdfFunctionRegisters[destination] = reference;
                    }
                }
                else if (decoded.operand_count_visible >= 2 &&
                    (decoded.mnemonic == ZYDIS_MNEMONIC_MOV ||
                     decoded.mnemonic == ZYDIS_MNEMONIC_LEA) &&
                    operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY)
                {
                    const uint32_t pointerSize = image.Headers().isPe32Plus ? 8 : 4;
                    if (sourceTableBindingIndex != SIZE_MAX &&
                        operands[1].mem.index == ZYDIS_REGISTER_NONE &&
                        operands[1].mem.disp.value >= 0 &&
                        static_cast<uint64_t>(operands[1].mem.disp.value) % pointerSize == 0)
                    {
                        const uint32_t slot = static_cast<uint32_t>(
                            static_cast<uint64_t>(operands[1].mem.disp.value) / pointerSize);
                        const PeFrameworkBinding& binding =
                            analysis.frameworkBindings[sourceTableBindingIndex];
                        if (slot < binding.functionCount)
                            wdfFunctionRegisters[destination] = {
                                sourceTableBindingIndex, slot,
                                instruction.rva + decoded.length};
                    }

                    ZyanU64 sourceAddress = 0;
                    if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(
                            &decoded, &operands[1], instruction.rva, &sourceAddress)) &&
                        sourceAddress <= UINT32_MAX)
                    {
                        for (size_t bindingIndex = 0;
                             bindingIndex < analysis.frameworkBindings.size(); ++bindingIndex)
                        {
                            const PeFrameworkBinding& binding =
                                analysis.frameworkBindings[bindingIndex];
                            if (static_cast<uint32_t>(sourceAddress) ==
                                binding.functionTableRva)
                            {
                                wdfTableRegisters[destination] = bindingIndex;
                                break;
                            }
                        }
                    }
                }
            }
            if (instruction.isCall) wdfFunctionRegisters.clear();

            if (decoded.meta.category == ZYDIS_CATEGORY_COND_BR ||
                decoded.meta.category == ZYDIS_CATEGORY_UNCOND_BR)
            {
                for (uint8_t operandIndex = 0;
                     operandIndex < decoded.operand_count_visible; ++operandIndex)
                {
                    if (operands[operandIndex].type != ZYDIS_OPERAND_TYPE_IMMEDIATE) continue;
                    ZyanU64 branchTarget = 0;
                    if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(
                            &decoded, &operands[operandIndex], instruction.rva,
                            &branchTarget)) || branchTarget > UINT32_MAX)
                        continue;
                    wdfTableRegistersAtBranchTarget[static_cast<uint32_t>(branchTarget)] =
                        wdfTableRegisters;
                    wdfFunctionRegistersAtBranchTarget[static_cast<uint32_t>(branchTarget)] =
                        wdfFunctionRegisters;
                    break;
                }
                if (decoded.meta.category == ZYDIS_CATEGORY_UNCOND_BR)
                {
                    wdfTableRegisters.clear();
                    wdfFunctionRegisters.clear();
                }
            }
            function.instructions.push_back(std::move(instruction));
            consumed += decoded.length;
            ++decodedCount;
        }

        static const char* argumentNames[] = {"RCX", "RDX", "R8", "R9"};
        for (uint32_t index = 0; index < 4; ++index)
        {
            if (!argumentConsumed[index]) continue;
            function.abiConsumedRegisters.push_back(argumentNames[index]);
            function.inferredMinimumArguments = index + 1;
        }
        if (function.inferredMinimumArguments != 0)
        {
            function.abiConfidence = "low";
            function.abiProvenance = "x64-register-use-heuristic";
        }
    }
}

void PEAnalyzer::BuildCallGraph(PEAnalysis& analysis) const
{
    std::unordered_map<uint32_t, size_t> functionByRva;
    for (size_t index = 0; index < analysis.functions.size(); ++index)
        functionByRva[analysis.functions[index].beginRva] = index;

    for (size_t callerIndex = 0; callerIndex < analysis.functions.size(); ++callerIndex)
    {
        PeFunction& caller = analysis.functions[callerIndex];
        for (const PeInstruction& instruction : caller.instructions)
        {
            if (!instruction.isCall || instruction.directTargetRva == 0) continue;
            const auto calleeIndex = functionByRva.find(instruction.directTargetRva);
            if (calleeIndex == functionByRva.end()) continue;
            PeFunction& callee = analysis.functions[calleeIndex->second];
            AppendUnique(caller.callees, callee.beginRva);
            AppendUnique(callee.callers, caller.beginRva);
        }
    }
}

void PEAnalyzer::ParseLoadConfig(const PEImage& image, PEAnalysis& analysis) const
{
    const uint16_t characteristics = image.Headers().dllCharacteristics;
    analysis.security.dynamicBase =
        (characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) != 0;
    analysis.security.highEntropyVa =
        (characteristics & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA) != 0;
    analysis.security.nxCompatible =
        (characteristics & IMAGE_DLLCHARACTERISTICS_NX_COMPAT) != 0;
    analysis.security.controlFlowGuard =
        (characteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF) != 0;

    const PeDataDirectory* directory = FindDirectory(image, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG);
    if (!directory) return;

    uint64_t guardTableVa = 0;
    if (image.Headers().isPe32Plus)
    {
        IMAGE_LOAD_CONFIG_DIRECTORY64 config = {};
        const size_t readSize = std::min<size_t>(directory->size, sizeof(config));
        if (!image.ReadRva(directory->rva, &config, readSize))
        {
            analysis.warnings.push_back("Invalid load-config directory");
            return;
        }
        analysis.security.securityCookieRva = VaToRva(config.SecurityCookie, image);
        analysis.security.guardCheckFunctionPointerRva =
            VaToRva(config.GuardCFCheckFunctionPointer, image);
        analysis.security.guardDispatchFunctionPointerRva =
            VaToRva(config.GuardCFDispatchFunctionPointer, image);
        analysis.security.guardFlags = config.GuardFlags;
        analysis.security.guardFunctionCount = config.GuardCFFunctionCount;
        guardTableVa = config.GuardCFFunctionTable;
    }
    else
    {
        IMAGE_LOAD_CONFIG_DIRECTORY32 config = {};
        const size_t readSize = std::min<size_t>(directory->size, sizeof(config));
        if (!image.ReadRva(directory->rva, &config, readSize))
        {
            analysis.warnings.push_back("Invalid load-config directory");
            return;
        }
        analysis.security.securityCookieRva = VaToRva(config.SecurityCookie, image);
        analysis.security.guardCheckFunctionPointerRva =
            VaToRva(config.GuardCFCheckFunctionPointer, image);
        analysis.security.guardDispatchFunctionPointerRva =
            VaToRva(config.GuardCFDispatchFunctionPointer, image);
        analysis.security.guardFlags = config.GuardFlags;
        analysis.security.guardFunctionCount = config.GuardCFFunctionCount;
        analysis.security.safeSeh = config.SEHandlerTable != 0 && config.SEHandlerCount != 0;
        guardTableVa = config.GuardCFFunctionTable;
    }

    const uint32_t guardTableRva = VaToRva(guardTableVa, image);
    const uint64_t count = std::min<uint64_t>(analysis.security.guardFunctionCount, 1'000'000);
    const uint32_t metadataBytes = (analysis.security.guardFlags >> 28) & 0xF;
    const uint32_t entrySize = sizeof(uint32_t) + metadataBytes;
    for (uint64_t index = 0; guardTableRva != 0 && index < count; ++index)
    {
        const uint64_t entryRva = static_cast<uint64_t>(guardTableRva) + index * entrySize;
        uint32_t functionRva = 0;
        if (entryRva > UINT32_MAX ||
            !image.ReadRva(static_cast<uint32_t>(entryRva), functionRva))
        {
            analysis.warnings.push_back("Truncated Guard CF function table");
            break;
        }
        analysis.security.guardFunctionRvas.push_back(functionRva);
    }
}

void PEAnalyzer::ParseResources(const PEImage& image, PEAnalysis& analysis) const
{
    const PeDataDirectory* directory = FindDirectory(image, IMAGE_DIRECTORY_ENTRY_RESOURCE);
    if (!directory) return;

    IMAGE_RESOURCE_DIRECTORY root = {};
    if (!image.ReadRva(directory->rva, root))
    {
        analysis.warnings.push_back("Invalid resource directory");
        return;
    }
    const uint32_t count = static_cast<uint32_t>(root.NumberOfNamedEntries) +
        root.NumberOfIdEntries;
    if (count > 100'000)
    {
        analysis.warnings.push_back("Resource directory exceeds safety limit");
        return;
    }

    const uint64_t entriesRva = static_cast<uint64_t>(directory->rva) + sizeof(root);
    for (uint32_t index = 0; index < count; ++index)
    {
        const uint64_t entryRva = entriesRva +
            static_cast<uint64_t>(index) * sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY);
        IMAGE_RESOURCE_DIRECTORY_ENTRY entry = {};
        if (entryRva > UINT32_MAX ||
            !image.ReadRva(static_cast<uint32_t>(entryRva), entry))
        {
            analysis.warnings.push_back("Truncated resource type table");
            return;
        }

        std::string type;
        if (entry.NameIsString)
        {
            const uint64_t nameRva = static_cast<uint64_t>(directory->rva) + entry.NameOffset;
            uint16_t length = 0;
            if (nameRva > UINT32_MAX ||
                !image.ReadRva(static_cast<uint32_t>(nameRva), length) || length > 1024)
                continue;
            for (uint16_t characterIndex = 0; characterIndex < length; ++characterIndex)
            {
                uint16_t character = 0;
                const uint64_t characterRva = nameRva + sizeof(uint16_t) +
                    static_cast<uint64_t>(characterIndex) * sizeof(uint16_t);
                if (characterRva > UINT32_MAX ||
                    !image.ReadRva(static_cast<uint32_t>(characterRva), character))
                    break;
                type.push_back(character <= 0x7F ? static_cast<char>(character) : '?');
            }
        }
        else type = ResourceTypeName(entry.Id);
        if (!type.empty() &&
            std::find(analysis.resources.types.begin(), analysis.resources.types.end(), type) ==
                analysis.resources.types.end())
            analysis.resources.types.push_back(std::move(type));
    }
}

void PEAnalyzer::ParseVersionInfo(const std::string& path, PEAnalysis& analysis) const
{
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeA(path.c_str(), &ignored);
    if (size == 0 || size > 16 * 1024 * 1024) return;
    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoA(path.c_str(), 0, size, data.data())) return;

    struct Translation { WORD language; WORD codePage; };
    Translation* translations = nullptr;
    UINT translationBytes = 0;
    Translation fallback = {0x0409, 0x04B0};
    if (!VerQueryValueA(data.data(), "\\VarFileInfo\\Translation",
            reinterpret_cast<void**>(&translations), &translationBytes) ||
        translationBytes < sizeof(Translation))
        translations = &fallback;

    const auto query = [&data, translations](const char* key)
    {
        char pathBuffer[128] = {};
        std::snprintf(pathBuffer, sizeof(pathBuffer),
            "\\StringFileInfo\\%04x%04x\\%s",
            translations[0].language, translations[0].codePage, key);
        char* value = nullptr;
        UINT length = 0;
        if (!VerQueryValueA(data.data(), pathBuffer,
                reinterpret_cast<void**>(&value), &length) || !value || length == 0)
            return std::string();
        return std::string(value);
    };

    analysis.resources.companyName = query("CompanyName");
    analysis.resources.productName = query("ProductName");
    analysis.resources.fileDescription = query("FileDescription");
    analysis.resources.fileVersion = query("FileVersion");
    analysis.resources.productVersion = query("ProductVersion");
    analysis.resources.originalFilename = query("OriginalFilename");
    analysis.resources.internalName = query("InternalName");
}

void PEAnalyzer::Classify(PEAnalysis& analysis) const
{
    analysis.capabilities = {
        {"IddCx", "not observed", "none", {}},
        {"WDF USB", "not observed", "none", {}},
        {"WinUSB", "not observed", "none", {}},
        {"SetupAPI", "not observed", "none", {}},
        {"file I/O", "not observed", "none", {}},
        {"DeviceIoControl", "not observed", "none", {}},
        {"Direct3D/DXGI", "not observed", "none", {}}
    };

    const auto add = [&analysis](const std::string& name, const std::string& evidence)
    {
        auto found = std::find_if(
            analysis.classifications.begin(), analysis.classifications.end(),
            [&name](const PeClassification& item) { return item.name == name; });
        if (found == analysis.classifications.end())
        {
            analysis.classifications.push_back({name, {evidence}});
            return;
        }
        AppendUnique(found->evidence, evidence);
    };
    const auto capability = [&analysis](
        const std::string& name, const std::string& state,
        const std::string& confidence, const std::string& source,
        const std::string& detail)
    {
        auto found = std::find_if(
            analysis.capabilities.begin(), analysis.capabilities.end(),
            [&name](const PeCapability& item) { return item.name == name; });
        if (found == analysis.capabilities.end()) return;
        const bool strongerState = found->state == "not observed" ||
            found->state == "unknown" || state == "observed";
        if (strongerState)
        {
            found->state = state;
            found->confidence = confidence;
        }
        const bool duplicate = std::any_of(
            found->evidence.begin(), found->evidence.end(),
            [&source, &detail](const PeCapability::Evidence& evidence)
            {
                return evidence.source == source && evidence.detail == detail;
            });
        if (!duplicate) found->evidence.push_back({source, detail});
    };

    for (const PeExport& item : analysis.exports)
        for (const std::string& name : item.names)
            if (name == "FxDriverEntryUm")
                add("probable UMDF/WDF user-mode driver", "export FxDriverEntryUm");

    if (Lower(analysis.resources.fileDescription).find("indirect display driver") !=
        std::string::npos)
    {
        add("probable Indirect Display Driver",
            "version description " + analysis.resources.fileDescription);
        capability("IddCx", "inferred", "medium", "version-resource",
            "file description: " + analysis.resources.fileDescription);
    }

    const auto inspectImports = [&add, &capability](
        const std::vector<PeImportModule>& modules, const std::string& source)
    {
        for (const PeImportModule& module : modules)
        {
            const std::string lowerModule = Lower(module.name);
            if (lowerModule.find("iddcx") != std::string::npos)
            {
                add("probable Indirect Display Driver", "import module " + module.name);
                capability("IddCx", "observed", "high", source,
                    "module " + module.name);
            }
            if (lowerModule.find("wudf") != std::string::npos)
                add("probable UMDF/WDF user-mode driver", "import module " + module.name);
            if (lowerModule.find("winusb") != std::string::npos)
            {
                add("probable WinUSB client", "import module " + module.name);
                capability("WinUSB", "observed", "high", source,
                    "module " + module.name);
            }
            if (lowerModule.find("setupapi") != std::string::npos)
                capability("SetupAPI", "observed", "high", source,
                    "module " + module.name);
            if (lowerModule.find("d3d") != std::string::npos ||
                lowerModule.find("dxgi") != std::string::npos)
                capability("Direct3D/DXGI", "observed", "high", source,
                    "module " + module.name);

            for (const PeImport& symbol : module.symbols)
            {
                const std::string lowerName = Lower(symbol.name);
                if (lowerName.find("iddcx") == 0)
                {
                    add("probable Indirect Display Driver", "import " + symbol.name);
                    capability("IddCx", "observed", "high", source,
                        "symbol " + symbol.name);
                }
                if (lowerName.find("wdfusb") == 0)
                {
                    add("probable WDF USB client", "import " + symbol.name);
                    capability("WDF USB", "observed", "high", source,
                        "symbol " + symbol.name);
                }
                if (lowerName.find("setupdi") == 0)
                {
                    add("uses SetupAPI device discovery", "import " + symbol.name);
                    capability("SetupAPI", "observed", "high", source,
                        "symbol " + symbol.name);
                }
                if (lowerName.find("winusb") == 0)
                {
                    add("probable WinUSB client", "import " + symbol.name);
                    capability("WinUSB", "observed", "high", source,
                        "symbol " + symbol.name);
                }
                if (lowerName == "createfilea" || lowerName == "createfilew" ||
                    lowerName == "readfile" || lowerName == "writefile")
                    capability("file I/O", "observed", "high", source,
                        "symbol " + symbol.name);
                if (lowerName == "deviceiocontrol")
                    capability("DeviceIoControl", "observed", "high", source,
                        "symbol " + symbol.name);
                if (lowerName.find("d3d") == 0 || lowerName.find("dxgi") == 0)
                    capability("Direct3D/DXGI", "observed", "high", source,
                        "symbol " + symbol.name);
            }
        }
    };
    inspectImports(analysis.imports, "pe-import");
    inspectImports(analysis.delayImports, "delay-import");

    for (const PeFunction& function : analysis.functions)
    {
        for (const PeInstruction& instruction : function.instructions)
        {
            if (instruction.frameworkCall.find("WdfUsb") != 0) continue;
            capability("WDF USB", "observed", instruction.frameworkCallConfidence,
                "wdf-function-table", instruction.frameworkCall + " at " +
                RvaLabel(instruction.rva) + "; " + instruction.frameworkCallProvenance);
            add("probable WDF USB client", "recovered call " + instruction.frameworkCall);
        }
    }

    if (!analysis.frameworkBindings.empty())
    {
        for (const std::string& name : {std::string("IddCx"), std::string("WDF USB")})
        {
            auto found = std::find_if(
                analysis.capabilities.begin(), analysis.capabilities.end(),
                [&name](const PeCapability& item) { return item.name == name; });
            if (found == analysis.capabilities.end() || found->state != "not observed") continue;
            found->state = "unknown";
            found->confidence = "none";
            found->evidence.push_back({
                "analysis-limitation",
                "WDF function-table binding was recovered, but no supporting " + name +
                    " dispatch slot was resolved; absence is not evidence of false"});
        }
    }
}
