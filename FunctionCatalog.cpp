#include "stdafx.h"
#include "FunctionCatalog.h"

#include "PEImage.h"

#include <bcrypt.h>
#include <algorithm>
#include <array>
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

void AddExports(const PEImage& image, const std::string& hash,
    std::map<uint32_t, FunctionRecord>& records)
{
    StaticExportCatalog exports;
    std::string ignored;
    if (!StaticExportCatalog::Load(image.SourceName(), exports, ignored)) return;
    for (const StaticExport& item : exports.Exports())
    {
    AddRecord(records, item.rva, hash, "pe-export", image.FindSection(item.rva) != nullptr &&
        image.FindSection(item.rva)->executable);
        FunctionRecord& record = records[item.rva];
        record.id.ordinal = item.ordinal;
        record.id.exportName = item.names.empty() ? "" : item.names.front();
        record.displayName = item.names.empty() ? "#" + std::to_string(item.ordinal) : item.names.front();
        record.aliases = item.names;
        record.forwarder = item.forwarder;
        record.callability = item.forwarder.empty() ? Callability::RequiresPrototype : Callability::Forwarded;
        record.callabilityReasons = {CallabilityReason(record.callability)};
    }
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
        const PeSection* section = image.FindSection(item.BeginAddress);
        AddRecord(records, item.BeginAddress, hash, "runtime-function",
            section != nullptr && section->executable);
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
        AddRecord(records, entry, hash, "guard-cf",
            section != nullptr && section->executable);
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
        if (character == '"' || character == '\\') output << '\\';
        if (character == '\n') output << "\\n";
        else if (character == '\r') output << "\\r";
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
    char canonicalPath[MAX_PATH] = {};
    if (GetFullPathNameA(image.SourceName().c_str(), MAX_PATH, canonicalPath, nullptr) != 0)
        candidate.module_.canonicalPath = canonicalPath;
    else
        candidate.module_.canonicalPath = image.SourceName();
    candidate.module_.sha256 = hash;
    candidate.module_.architecture = Architecture(image);
    candidate.module_.timestamp = image.Headers().timestamp;
    candidate.module_.imageSize = image.Headers().imageSize;
    candidate.module_.preferredImageBase = image.Headers().preferredImageBase;
    std::map<uint32_t, FunctionRecord> records;
    AddExports(image, hash, records);
    AddRuntimeFunctions(image, hash, records);
    AddGuardFunctions(image, hash, records);
    for (auto& pair : records)
    {
        FunctionRecord record = std::move(pair.second);
        if (record.displayName.empty()) record.displayName = "sub_" + std::to_string(record.startRva);
        if (record.callability == Callability::NotAddressable)
        {
            record.callability = Callability::RequiresPrototype;
            record.callabilityReasons = {CallabilityReason(record.callability)};
        }
        std::sort(record.aliases.begin(), record.aliases.end());
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
        if (std::find(record.addressSources.begin(), record.addressSources.end(), "pe-export") != record.addressSources.end()) ++exportCount;
    output << "FuBi function catalog\nversion = " << kSchemaVersion << "\n"
        << "module = " << module_.canonicalPath << "\nsha256 = " << module_.sha256
        << "\narchitecture = " << module_.architecture << "\nexport_count = " << exportCount << "\nfunction_count = ";
    size_t count = 0; for (const FunctionRecord& record : functions_) if (!callableOnly || record.callability == Callability::Callable) ++count;
    output << count << "\n";
    for (const FunctionRecord& record : functions_)
    {
        if (callableOnly && record.callability != Callability::Callable) continue;
        output << "\n[function]\nrva = 0x" << std::hex << std::uppercase << record.startRva << std::dec
            << "\nname = " << record.displayName << "\nordinal = " << record.id.ordinal << "\ncallability = "
            << CallabilityName(record.callability) << "\nreason = " << CallabilityReason(record.callability) << "\naddress_sources = ";
        for (size_t index = 0; index < record.addressSources.size(); ++index) { if (index) output << ", "; output << record.addressSources[index]; }
        output << "\naliases = "; for (size_t index = 0; index < record.aliases.size(); ++index) { if (index) output << ", "; output << record.aliases[index]; }
        output << "\nforwarder = " << (record.forwarder.empty() ? "<none>" : record.forwarder) << "\n";
    }
}

void FunctionCatalog::WriteJson(std::ostream& output, bool callableOnly) const
{
    output << "{\"schema_version\":" << kSchemaVersion << ",\"module\":{";
    output << "\"path\":"; WriteJsonString(output, module_.canonicalPath); output << ",\"sha256\":"; WriteJsonString(output, module_.sha256);
    output << ",\"architecture\":"; WriteJsonString(output, module_.architecture); output << "},\"functions\":[";
    bool first = true;
    for (const FunctionRecord& record : functions_)
    {
        if (callableOnly && record.callability != Callability::Callable) continue;
        if (!first) output << ','; first = false;
        output << "{\"rva\":" << record.startRva << ",\"name\":"; WriteJsonString(output, record.displayName);
        output << ",\"ordinal\":" << record.id.ordinal << ",\"aliases\":[";
        for (size_t index = 0; index < record.aliases.size(); ++index) { if (index) output << ','; WriteJsonString(output, record.aliases[index]); }
        output << "],\"callability\":"; WriteJsonString(output, CallabilityName(record.callability));
        output << ",\"reason\":"; WriteJsonString(output, CallabilityReason(record.callability));
        output << ",\"forwarder\":"; WriteJsonString(output, record.forwarder); output << "}";
    }
    output << "]}\n";
}

const FunctionRecord* FunctionCatalog::Find(const std::string& selector) const
{
    for (const FunctionRecord& record : functions_)
    {
        if (record.displayName == selector || record.id.exportName == selector) return &record;
        if (selector.size() > 1 && selector[0] == '#' && std::to_string(record.id.ordinal) == selector.substr(1)) return &record;
        if (selector.size() > 2 && selector[0] == '0' && (selector[1] == 'x' || selector[1] == 'X'))
        {
            char* end = nullptr; const unsigned long value = std::strtoul(selector.c_str(), &end, 16);
            if (end != nullptr && *end == '\0' && value == record.startRva) return &record;
        }
    }
    return nullptr;
}
