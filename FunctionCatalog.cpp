#include "stdafx.h"
#include "FunctionCatalog.h"
#include "PrototypeProfile.h"
#include "DbgHelpDll.h"

#include "PEImage.h"

#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <ostream>
#include <sstream>

#pragma comment(lib, "bcrypt.lib")

namespace
{
constexpr uint32_t kMaximumRuntimeFunctions = 1'000'000;
constexpr uint32_t kMaximumGuardFunctions = 1'000'000;
constexpr DWORD kMaximumCanonicalPath = 1u << 20;

bool CanonicalPath(const std::string& path, std::string& result)
{
    DWORD capacity = MAX_PATH;
    for (;;)
    {
        std::vector<char> buffer(capacity, '\0');
        const DWORD length = GetFullPathNameA(path.c_str(), capacity, buffer.data(), nullptr);
        if (length == 0) return false;
        // GetFullPathNameA reports the path length excluding its NUL terminator
        // when the supplied buffer is sufficient.
        if (length < capacity)
        {
            result.assign(buffer.data(), length);
            return true;
        }
        if (length >= kMaximumCanonicalPath || length == UINT32_MAX) return false;
        capacity = length + 1;
    }
}

std::string Hex(const uint8_t* bytes, size_t count)
{
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (size_t index = 0; index < count; ++index)
        result << std::setw(2) << static_cast<unsigned int>(bytes[index]);
    return result.str();
}

bool Sha256(const std::vector<uint8_t>& bytes, std::string& result)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    bool okay = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
        nullptr, 0) == 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
            &resultLength, 0) == 0;
    std::vector<uint8_t> object(objectLength);
    std::array<uint8_t, 32> digest = {};
    if (okay)
        okay = BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
            nullptr, 0, 0) == 0;
    if (okay && !bytes.empty())
        okay = BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()),
            static_cast<ULONG>(bytes.size()), 0) == 0;
    if (okay)
        okay = BCryptFinishHash(hash, digest.data(), digest.size(), 0) == 0;
    if (hash != nullptr) BCryptDestroyHash(hash);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (okay) result = Hex(digest.data(), digest.size());
    return okay;
}

const PeDataDirectory* Directory(const PEImage& image, uint32_t index)
{
    for (const PeDataDirectory& item : image.Headers().dataDirectories)
        if (item.index == index && item.rva != 0 && item.size != 0) return &item;
    return nullptr;
}

std::string Architecture(const PEImage& image)
{
    if (image.Headers().isPe32Plus && image.Headers().machine == IMAGE_FILE_MACHINE_AMD64)
        return "x64";
    if (!image.Headers().isPe32Plus && image.Headers().machine == IMAGE_FILE_MACHINE_I386)
        return "x86";
    return "unknown";
}

void ReadPdbIdentity(const PEImage& image, ModuleIdentity& module)
{
    const PeDataDirectory* directory = Directory(image, IMAGE_DIRECTORY_ENTRY_DEBUG);
    if (directory == nullptr || directory->size < sizeof(IMAGE_DEBUG_DIRECTORY)) return;
    const uint32_t count = directory->size / sizeof(IMAGE_DEBUG_DIRECTORY);
    for (uint32_t index = 0; index < count; ++index)
    {
        IMAGE_DEBUG_DIRECTORY debug = {};
        const uint64_t offset = static_cast<uint64_t>(index) * sizeof(debug);
        if (offset > UINT32_MAX - directory->rva) return;
        const uint64_t rva = static_cast<uint64_t>(directory->rva) + offset;
        if (rva > UINT32_MAX || !image.ReadRva(static_cast<uint32_t>(rva), debug)) return;
        if (debug.Type != IMAGE_DEBUG_TYPE_CODEVIEW || debug.AddressOfRawData == 0 || debug.SizeOfData < 24) continue;
        uint32_t signature = 0; GUID guid = {}; uint32_t age = 0;
        if (debug.AddressOfRawData > UINT32_MAX - sizeof(signature) - sizeof(guid)) continue;
        if (!image.ReadRva(debug.AddressOfRawData, signature) || signature != 0x53445352 ||
            !image.ReadRva(debug.AddressOfRawData + sizeof(signature), guid) ||
            !image.ReadRva(debug.AddressOfRawData + sizeof(signature) + sizeof(guid), age)) continue;
        std::ostringstream text;
        text << std::hex << std::setfill('0') << std::uppercase
            << std::setw(8) << guid.Data1 << '-' << std::setw(4) << guid.Data2 << '-'
            << std::setw(4) << guid.Data3 << '-';
        for (size_t byte = 0; byte < 2; ++byte) text << std::setw(2) << static_cast<unsigned>(guid.Data4[byte]);
        text << '-';
        for (size_t byte = 2; byte < 8; ++byte) text << std::setw(2) << static_cast<unsigned>(guid.Data4[byte]);
        module.pdbGuid = text.str(); module.pdbAge = age; return;
    }
}

void AddRecord(std::map<uint32_t, FunctionRecord>& records, uint32_t rva,
    const std::string& hash, const char* source, bool executable)
{
    FunctionRecord& record = records[rva];
    record.id.moduleSha256 = hash;
    record.id.rva = rva;
    record.startRva = rva;
    record.executable = record.executable || executable;
    if (std::find(record.addressSources.begin(), record.addressSources.end(), source) ==
        record.addressSources.end()) record.addressSources.push_back(source);
    if (std::find(record.boundarySources.begin(), record.boundarySources.end(), source) ==
        record.boundarySources.end()) record.boundarySources.push_back(source);
}

bool AddExports(const PEImage& image, const std::string& hash,
    std::map<uint32_t, FunctionRecord>& records, std::string& error)
{
    StaticExportCatalog exports;
    std::string ignored;
    if (!StaticExportCatalog::Load(image.SourceName(), exports, ignored))
    {
        error = ignored;
        return false;
    }
    for (const StaticExport& item : exports.Exports())
    {
    AddRecord(records, item.rva, hash, "pe-export", image.FindSection(item.rva) != nullptr &&
        image.FindSection(item.rva)->executable);
        FunctionRecord& record = records[item.rva];
        if (record.displayName.empty())
            record.displayName = item.names.empty() ? "#" + std::to_string(item.ordinal) : item.names.front();
        record.aliases.insert(record.aliases.end(), item.names.begin(), item.names.end());
        record.exportNames.insert(record.exportNames.end(), item.names.begin(), item.names.end());
        record.exportOrdinals.push_back(item.ordinal);
        record.forwarder = item.forwarder;
        if (!item.forwarder.empty()) record.forwarders.push_back(item.forwarder);
        record.callability = item.forwarder.empty() ? Callability::RequiresPrototype : Callability::Forwarded;
        record.callabilityReasons = {CallabilityReason(record.callability)};
    }
    return true;
}

void AddRuntimeFunctions(const PEImage& image, const std::string& hash,
    std::map<uint32_t, FunctionRecord>& records)
{
    if (!image.Headers().isPe32Plus) return;
    const PeDataDirectory* directory = Directory(image, IMAGE_DIRECTORY_ENTRY_EXCEPTION);
    if (directory == nullptr || directory->size < sizeof(RUNTIME_FUNCTION)) return;
    const uint32_t count = directory->size / sizeof(RUNTIME_FUNCTION);
    if (count > kMaximumRuntimeFunctions) return;
    for (uint32_t index = 0; index < count; ++index)
    {
        RUNTIME_FUNCTION item = {};
        const uint64_t rva = static_cast<uint64_t>(directory->rva) +
            static_cast<uint64_t>(index) * sizeof(item);
        if (rva > UINT32_MAX || !image.ReadRva(static_cast<uint32_t>(rva), item)) break;
        if (item.BeginAddress == 0 || item.EndAddress <= item.BeginAddress) continue;
        const PeSection* beginSection = image.FindSection(item.BeginAddress);
        const PeSection* endSection = image.FindSection(item.EndAddress - 1);
        if (beginSection == nullptr || beginSection != endSection || !beginSection->executable)
            continue;
        AddRecord(records, item.BeginAddress, hash, "runtime-function", true);
        FunctionRecord& record = records[item.BeginAddress];
        record.endRva = item.EndAddress;
        if (record.displayName.empty()) record.displayName = "sub_" +
            [&] { std::ostringstream stream; stream << std::hex << std::uppercase << item.BeginAddress; return stream.str(); }();
        if (!record.hasPrototype && record.callability != Callability::Forwarded)
        {
            record.callability = Callability::RequiresPrototype;
            record.callabilityReasons = {CallabilityReason(record.callability)};
        }
    }
}

void AddGuardFunctions(const PEImage& image, const std::string& hash,
    std::map<uint32_t, FunctionRecord>& records)
{
    const PeDataDirectory* directory = Directory(image, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG);
    if (directory == nullptr || !image.Headers().isPe32Plus) return;
    IMAGE_LOAD_CONFIG_DIRECTORY64 config = {};
    const size_t readable = std::min<size_t>(directory->size, sizeof(config));
    if (!image.ReadRva(directory->rva, &config, readable) ||
        config.GuardCFFunctionTable == 0 || config.GuardCFFunctionCount == 0 ||
        config.GuardCFFunctionCount > kMaximumGuardFunctions) return;
    const uint64_t tableRva = config.GuardCFFunctionTable - image.Headers().preferredImageBase;
    if (tableRva > UINT32_MAX) return;
    for (uint64_t index = 0; index < config.GuardCFFunctionCount; ++index)
    {
        uint32_t entry = 0;
        const uint64_t entryRva = tableRva + index * sizeof(uint32_t);
        if (entryRva > UINT32_MAX || !image.ReadRva(static_cast<uint32_t>(entryRva), entry)) break;
        entry &= ~static_cast<uint32_t>(0x3);
        if (entry == 0) continue;
        const PeSection* section = image.FindSection(entry);
        if (section == nullptr || !section->executable) continue;
        AddRecord(records, entry, hash, "guard-cf", true);
        FunctionRecord& record = records[entry];
        if (record.displayName.empty()) record.displayName = "sub_" +
            [&] { std::ostringstream stream; stream << std::hex << std::uppercase << entry; return stream.str(); }();
        if (record.callability == Callability::NotAddressable)
        {
            record.callability = Callability::RequiresPrototype;
            record.callabilityReasons = {CallabilityReason(record.callability)};
        }
    }
}

void WriteJsonString(std::ostream& output, const std::string& value)
{
    output << '"';
    for (const char character : value)
    {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') output << '\\' << character;
        else if (character == '\b') output << "\\b";
        else if (character == '\f') output << "\\f";
        else if (character == '\n') output << "\\n";
        else if (character == '\r') output << "\\r";
        else if (character == '\t') output << "\\t";
        else if (byte < 0x20) output << "\\u00" << std::hex << std::setw(2)
            << std::setfill('0') << static_cast<unsigned int>(byte) << std::dec;
        else output << character;
    }
    output << '"';
}
}

const char* CallabilityName(Callability value)
{
    switch (value)
    {
    case Callability::Callable: return "callable";
    case Callability::RequiresPrototype: return "requires-prototype";
    case Callability::UnsupportedAbi: return "unsupported-abi";
    case Callability::UnsafeInternal: return "unsafe-internal";
    case Callability::FrameworkManaged: return "framework-managed";
    case Callability::Forwarded: return "forwarded";
    case Callability::NotAddressable: return "not-addressable";
    case Callability::ArchitectureMismatch: return "architecture-mismatch";
    }
    return "not-addressable";
}

const char* CallabilityReason(Callability value)
{
    return CallabilityName(value);
}

const char* TypeKindName(TypeKind value)
{
    switch (value)
    {
    case TypeKind::Void: return "void"; case TypeKind::Bool: return "bool";
    case TypeKind::Integer: return "integer"; case TypeKind::Floating: return "floating";
    case TypeKind::String: return "string"; case TypeKind::Pointer: return "pointer";
    case TypeKind::Structure: return "structure"; default: return "unknown";
    }
}

const char* PrototypeQualityName(PrototypeQuality value)
{
    switch (value)
    { case PrototypeQuality::ExactSymbol: return "exact-symbol"; case PrototypeQuality::UserDeclared: return "user-declared";
      case PrototypeQuality::Inferred: return "inferred"; default: return "unknown"; }
}

bool FunctionCatalog::Load(const std::string& path, FunctionCatalog& catalog, std::string& error)
{
    PEImage image;
    if (!PEImage::Load(path, image, error)) return false;
    std::string hash;
    if (!Sha256(image.Bytes(), hash)) { error = "Unable to calculate module SHA-256"; return false; }
    FunctionCatalog candidate;
    if (!CanonicalPath(image.SourceName(), candidate.module_.canonicalPath))
    {
        error = "Unable to canonicalize module path";
        return false;
    }
    candidate.module_.sha256 = hash;
    candidate.module_.architecture = Architecture(image);
    candidate.module_.timestamp = image.Headers().timestamp;
    candidate.module_.imageSize = image.Headers().imageSize;
    candidate.module_.preferredImageBase = image.Headers().preferredImageBase;
    ReadPdbIdentity(image, candidate.module_);
    std::map<uint32_t, FunctionRecord> records;
    if (!AddExports(image, hash, records, error)) return false;
    AddRuntimeFunctions(image, hash, records);
    AddGuardFunctions(image, hash, records);
    for (auto& pair : records)
    {
        FunctionRecord record = std::move(pair.second);
        if (record.displayName.empty()) record.displayName = "sub_" + std::to_string(record.startRva);
        if (!record.executable && record.callability != Callability::Forwarded)
        {
            record.callability = Callability::NotAddressable;
            record.callabilityReasons = {CallabilityReason(record.callability)};
        }
        else if (record.callability == Callability::NotAddressable)
        {
            record.callability = Callability::RequiresPrototype;
            record.callabilityReasons = {CallabilityReason(record.callability)};
        }
        std::sort(record.aliases.begin(), record.aliases.end());
        std::sort(record.exportNames.begin(), record.exportNames.end());
        std::sort(record.exportOrdinals.begin(), record.exportOrdinals.end());
        record.aliases.erase(std::unique(record.aliases.begin(), record.aliases.end()), record.aliases.end());
        record.exportNames.erase(std::unique(record.exportNames.begin(), record.exportNames.end()), record.exportNames.end());
        record.exportOrdinals.erase(std::unique(record.exportOrdinals.begin(), record.exportOrdinals.end()), record.exportOrdinals.end());
        std::sort(record.forwarders.begin(), record.forwarders.end());
        record.forwarders.erase(std::unique(record.forwarders.begin(), record.forwarders.end()), record.forwarders.end());
        if (!record.exportNames.empty())
        {
            record.id.exportName = record.exportNames.front();
            record.displayName = record.exportNames.front();
        }
        if (!record.exportOrdinals.empty()) record.id.ordinal = record.exportOrdinals.front();
        std::sort(record.addressSources.begin(), record.addressSources.end());
        std::sort(record.boundarySources.begin(), record.boundarySources.end());
        candidate.functions_.push_back(std::move(record));
    }
    catalog = std::move(candidate);
    return true;
}

void FunctionCatalog::WriteText(std::ostream& output, bool callableOnly) const
{
    size_t exportCount = 0;
    for (const FunctionRecord& record : functions_)
        exportCount += record.exportOrdinals.size();
    output << "FuBi function catalog\nversion = " << kSchemaVersion << "\n"
        << "module = " << module_.canonicalPath << "\nsha256 = " << module_.sha256
        << "\narchitecture = " << module_.architecture << "\nexport_count = " << exportCount << "\nfunction_count = ";
    size_t count = 0; for (const FunctionRecord& record : functions_) if (!callableOnly || record.callability == Callability::Callable) ++count;
    output << count << "\n";
    for (const FunctionRecord& record : functions_)
    {
        if (callableOnly && record.callability != Callability::Callable) continue;
        output << "\n[function]\nrva = 0x" << std::hex << std::uppercase << record.startRva << std::dec
            << "\nend_rva = 0x" << std::hex << std::uppercase << record.endRva << std::dec
            << "\nname = " << record.displayName << "\nordinals = ";
        for (size_t index = 0; index < record.exportOrdinals.size(); ++index) { if (index) output << ", "; output << record.exportOrdinals[index]; }
        output << "\ncallability = "
            << CallabilityName(record.callability) << "\nreason = " << CallabilityReason(record.callability) << "\naddress_sources = ";
        for (size_t index = 0; index < record.addressSources.size(); ++index) { if (index) output << ", "; output << record.addressSources[index]; }
        output << "\nexport_names = "; for (size_t index = 0; index < record.exportNames.size(); ++index) { if (index) output << ", "; output << record.exportNames[index]; }
        output << "\naliases = "; for (size_t index = 0; index < record.aliases.size(); ++index) { if (index) output << ", "; output << record.aliases[index]; }
        output << "\nboundary_sources = "; for (size_t index = 0; index < record.boundarySources.size(); ++index) { if (index) output << ", "; output << record.boundarySources[index]; }
        output << "\nexecutable = " << (record.executable ? "true" : "false")
               << "\nprototype_source = " << (record.hasPrototype ? record.prototype.source : "unknown")
               << "\nprototype_quality = " << PrototypeQualityName(record.prototype.quality)
               << "\nforwarders = ";
        for (size_t index = 0; index < record.forwarders.size(); ++index) { if (index) output << ", "; output << record.forwarders[index]; }
        output << "\nforwarder = " << (record.forwarder.empty() ? "<none>" : record.forwarder) << "\n";
    }
}

void FunctionCatalog::WriteJson(std::ostream& output, bool callableOnly) const
{
    output << "{\"schema_version\":" << kSchemaVersion << ",\"module\":{";
    output << "\"path\":"; WriteJsonString(output, module_.canonicalPath); output << ",\"sha256\":"; WriteJsonString(output, module_.sha256);
    output << ",\"architecture\":"; WriteJsonString(output, module_.architecture);
    output << ",\"pdb_guid\":"; WriteJsonString(output, module_.pdbGuid);
    output << ",\"pdb_age\":" << module_.pdbAge << "},\"functions\":[";
    bool first = true;
    for (const FunctionRecord& record : functions_)
    {
        if (callableOnly && record.callability != Callability::Callable) continue;
        if (!first) output << ','; first = false;
        output << "{\"rva\":" << record.startRva << ",\"end_rva\":" << record.endRva << ",\"name\":"; WriteJsonString(output, record.displayName);
        output << ",\"export_names\":[";
        for (size_t index = 0; index < record.exportNames.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.exportNames[index]); }
        output << "],\"export_ordinals\":[";
        for (size_t index = 0; index < record.exportOrdinals.size(); ++index) { if (index) output << ','; output << record.exportOrdinals[index]; }
        output << "],\"aliases\":[";
        for (size_t index = 0; index < record.aliases.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.aliases[index]); }
        output << "],\"forwarders\":[";
        for (size_t index = 0; index < record.forwarders.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.forwarders[index]); }
        output << "],\"address_sources\":[";
        for (size_t index = 0; index < record.addressSources.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.addressSources[index]); }
        output << "],\"boundary_sources\":[";
        for (size_t index = 0; index < record.boundarySources.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.boundarySources[index]); }
        output << "],\"executable\":" << (record.executable ? "true" : "false")
               << ",\"prototype\":{\"has_prototype\":" << (record.hasPrototype ? "true" : "false") << ",\"source\":";
        WriteJsonString(output, record.hasPrototype ? record.prototype.source : "unknown");
        output << ",\"quality\":"; WriteJsonString(output, PrototypeQualityName(record.prototype.quality));
        output << ",\"conflicts\":[";
        for (size_t index = 0; index < record.prototypeConflicts.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.prototypeConflicts[index]); }
        output << "]";
        output << "},\"callability\":"; WriteJsonString(output, CallabilityName(record.callability));
        output << ",\"reason\":"; WriteJsonString(output, CallabilityReason(record.callability));
        output << ",\"forwarder\":"; WriteJsonString(output, record.forwarder); output << "}";
    }
    output << "]}\n";
}

std::vector<const FunctionRecord*> FunctionCatalog::FindAll(const std::string& selector) const
{
    std::vector<const FunctionRecord*> matches;
    auto parseNumber = [](const std::string& text, int base, uint32_t& value)
    {
        if (text.empty()) return false;
        uint64_t parsed = 0;
        for (const char character : text)
        {
            unsigned digit = 0;
            if (character >= '0' && character <= '9') digit = static_cast<unsigned>(character - '0');
            else if (base == 16 && character >= 'a' && character <= 'f') digit = static_cast<unsigned>(character - 'a' + 10);
            else if (base == 16 && character >= 'A' && character <= 'F') digit = static_cast<unsigned>(character - 'A' + 10);
            else return false;
            if (digit >= static_cast<unsigned>(base) || parsed > (UINT32_MAX - digit) / base) return false;
            parsed = parsed * base + digit;
        }
        value = static_cast<uint32_t>(parsed);
        return true;
    };
    uint32_t numericSelector = 0;
    const bool ordinalSelector = selector.size() > 1 && selector.front() == '#' &&
        parseNumber(selector.substr(1), 10, numericSelector);
    const bool rvaSelector = selector.size() > 2 && selector[0] == '0' &&
        (selector[1] == 'x' || selector[1] == 'X') &&
        parseNumber(selector.substr(2), 16, numericSelector);
    for (const FunctionRecord& record : functions_)
    {
        if (record.displayName == selector || record.id.exportName == selector ||
            std::find(record.aliases.begin(), record.aliases.end(), selector) != record.aliases.end()) matches.push_back(&record);
        if (ordinalSelector && std::find(record.exportOrdinals.begin(), record.exportOrdinals.end(), numericSelector) != record.exportOrdinals.end()) matches.push_back(&record);
        if (selector.size() > 2 && selector[0] == '0' && (selector[1] == 'x' || selector[1] == 'X'))
        {
            if (rvaSelector && numericSelector == record.startRva) matches.push_back(&record);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const FunctionRecord* left, const FunctionRecord* right)
    {
        if (left->startRva != right->startRva) return left->startRva < right->startRva;
        if (left->displayName != right->displayName) return left->displayName < right->displayName;
        return left->id.ordinal < right->id.ordinal;
    });
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    return matches;
}

const FunctionRecord* FunctionCatalog::Find(const std::string& selector) const
{
    const std::vector<const FunctionRecord*> matches = FindAll(selector);
    return matches.size() == 1 ? matches.front() : nullptr;
}

void FunctionCatalog::WriteJsonDescribe(std::ostream& output, const FunctionRecord& record) const
{
    output << "{\"schema_version\":" << kSchemaVersion << ",\"module_sha256\":";
    WriteJsonString(output, module_.sha256);
    output << ",\"function\":{\"rva\":" << record.startRva << ",\"name\":";
    WriteJsonString(output, record.displayName);
    output << ",\"end_rva\":" << record.endRva << ",\"export_names\":[";
    for (size_t index = 0; index < record.exportNames.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.exportNames[index]); }
    output << "],\"export_ordinals\":[";
    for (size_t index = 0; index < record.exportOrdinals.size(); ++index) { if (index) output << ','; output << record.exportOrdinals[index]; }
    output << "],\"aliases\":[";
    for (size_t index = 0; index < record.aliases.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.aliases[index]); }
    output << "],\"forwarders\":[";
    for (size_t index = 0; index < record.forwarders.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.forwarders[index]); }
    output << "],\"callability\":";
    WriteJsonString(output, CallabilityName(record.callability));
    output << ",\"reason\":";
    WriteJsonString(output, CallabilityReason(record.callability));
    output << ",\"address_sources\":[";
    for (size_t index = 0; index < record.addressSources.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.addressSources[index]); }
    output << "],\"boundary_sources\":[";
    for (size_t index = 0; index < record.boundarySources.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.boundarySources[index]); }
    output << "],\"executable\":" << (record.executable ? "true" : "false")
           << ",\"prototype\":{\"has_prototype\":" << (record.hasPrototype ? "true" : "false") << ",\"source\":";
    WriteJsonString(output, record.hasPrototype ? record.prototype.source : "unknown");
    output << ",\"quality\":"; WriteJsonString(output, PrototypeQualityName(record.prototype.quality));
    output << ",\"conflicts\":[";
    for (size_t index = 0; index < record.prototypeConflicts.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.prototypeConflicts[index]); }
    output << "]";
    output << "},\"forwarder\":";
    WriteJsonString(output, record.forwarder);
    output << "}}\n";
}

bool FunctionCatalog::ApplyProfile(const PrototypeProfile& profile,
    std::vector<ProfileValidationError>& errors)
{
    if (!ValidatePrototypeProfile(profile, *this, errors)) return false;
    FunctionCatalog candidate = *this;
    std::vector<ProfileValidationError> applyErrors;
    for (const ProfileFunction& item : profile.functions)
    {
        FunctionRecord* record = nullptr;
        for (FunctionRecord& candidateRecord : candidate.functions_)
            if (candidateRecord.startRva == item.rva) { record = &candidateRecord; break; }
        if (record == nullptr) continue;
        if (item.frameworkManaged)
        {
            record->callability = Callability::FrameworkManaged;
            record->callabilityReasons = {CallabilityReason(record->callability)};
        }
        if (!MergePrototypeEvidence(*record, item.prototype, "profile",
            PrototypeQuality::UserDeclared))
        {
            applyErrors.push_back({"prototype-conflict", "functions", "profile conflicts with existing evidence"});
        }
    }
    if (!applyErrors.empty()) { errors.insert(errors.end(), applyErrors.begin(), applyErrors.end()); return false; }
    *this = std::move(candidate);
    return true;
}

bool FunctionCatalog::ApplySymbolEvidence(
    const std::vector<SymbolPrototypeEvidence>& evidence, std::string& error)
{
    FunctionCatalog candidate = *this;
    for (const SymbolPrototypeEvidence& item : evidence)
    {
        FunctionRecord* record = nullptr;
        for (FunctionRecord& candidateRecord : candidate.functions_)
            if (candidateRecord.startRva == item.rva) { record = &candidateRecord; break; }
        if (record == nullptr) { error = "symbol RVA is not in the catalog"; return false; }
        if (item.module.sha256 != module_.sha256 || item.module.architecture != module_.architecture) { error = "symbol module identity mismatch"; return false; }
        if (!record->executable || !record->forwarder.empty()) { error = "symbol RVA is not a callable code address"; return false; }
        if (!MergePrototypeEvidence(*record, item.prototype, "dbghelp-pdb", PrototypeQuality::ExactSymbol))
        { error = "symbol prototype conflicts with existing evidence"; return false; }
        record->id.symbol = item.name;
    }
    *this = std::move(candidate);
    error.clear();
    return true;
}
