#include "PEImage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace
{
bool AddWouldOverflow(uint64_t left, uint64_t right)
{
    return right > std::numeric_limits<uint64_t>::max() - left;
}

double CalculateEntropy(
    const std::vector<uint8_t>& bytes, uint32_t offset, uint32_t size)
{
    if (size == 0 || offset >= bytes.size() || size > bytes.size() - offset)
    {
        return 0.0;
    }

    std::array<uint64_t, 256> counts = {};
    for (uint32_t index = 0; index < size; ++index)
    {
        ++counts[bytes[offset + index]];
    }

    double entropy = 0.0;
    for (const uint64_t count : counts)
    {
        if (count == 0) continue;
        const double probability = static_cast<double>(count) / size;
        entropy -= probability * std::log2(probability);
    }
    return entropy;
}

std::string SectionName(const IMAGE_SECTION_HEADER& section)
{
    const char* begin = reinterpret_cast<const char*>(section.Name);
    size_t length = 0;
    while (length < IMAGE_SIZEOF_SHORT_NAME && begin[length] != '\0') ++length;
    return std::string(begin, length);
}
}

bool PEImage::Load(
    const std::string& path, PEImage& image, std::string& error)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        error = "Unable to open PE file: " + path;
        return false;
    }

    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<uint64_t>(length) >
            static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        error = "PE file size is unsupported";
        return false;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() &&
        !input.read(reinterpret_cast<char*>(bytes.data()), length))
    {
        error = "Unable to read complete PE file";
        return false;
    }

    return FromBytes(std::move(bytes), path, image, error);
}

bool PEImage::FromBytes(
    std::vector<uint8_t> bytes, const std::string& sourceName,
    PEImage& image, std::string& error)
{
    PEImage candidate;
    candidate.sourceName_ = sourceName;
    candidate.bytes_ = std::move(bytes);
    if (!candidate.Parse(error))
    {
        return false;
    }
    image = std::move(candidate);
    return true;
}

bool PEImage::ReadFile(uint64_t offset, void* destination, size_t size) const
{
    if (AddWouldOverflow(offset, size) || offset + size > bytes_.size())
    {
        return false;
    }
    if (size != 0) std::memcpy(destination, bytes_.data() + offset, size);
    return true;
}

bool PEImage::ReadRva(uint32_t rva, void* destination, size_t size) const
{
    const std::optional<uint32_t> offset = RvaToFileOffset(rva);
    return offset.has_value() && ReadFile(*offset, destination, size);
}

std::optional<uint32_t> PEImage::RvaToFileOffset(uint32_t rva) const
{
    if (rva < headers_.headersSize && rva < bytes_.size())
    {
        return rva;
    }

    for (const PeSection& section : sections_)
    {
        const uint64_t mappedSize = std::max(section.virtualSize, section.rawSize);
        const uint64_t sectionEnd = static_cast<uint64_t>(section.rva) + mappedSize;
        if (rva < section.rva || rva >= sectionEnd)
        {
            continue;
        }

        const uint64_t delta = static_cast<uint64_t>(rva) - section.rva;
        if (delta >= section.rawSize)
        {
            return std::nullopt;
        }

        const uint64_t offset = static_cast<uint64_t>(section.rawOffset) + delta;
        if (offset >= bytes_.size() || offset > std::numeric_limits<uint32_t>::max())
        {
            return std::nullopt;
        }
        return static_cast<uint32_t>(offset);
    }
    return std::nullopt;
}

const PeSection* PEImage::FindSection(uint32_t rva) const
{
    for (const PeSection& section : sections_)
    {
        const uint64_t end = static_cast<uint64_t>(section.rva) +
            std::max(section.virtualSize, section.rawSize);
        if (rva >= section.rva && rva < end) return &section;
    }
    return nullptr;
}

bool PEImage::ReadCStringAtRva(
    uint32_t rva, std::string& value, size_t maximumLength) const
{
    const std::optional<uint32_t> offset = RvaToFileOffset(rva);
    if (!offset.has_value()) return false;
	return ReadCStringAtFileOffset(*offset, value, maximumLength);
}

bool PEImage::ReadCStringAtFileOffset(
    uint32_t offset, std::string& value, size_t maximumLength) const
{
    value.clear();
    if (offset >= bytes_.size()) return false;
    const size_t available = bytes_.size() - offset;
    const size_t limit = std::min(available, maximumLength);
    for (size_t index = 0; index < limit; ++index)
    {
        const char character = static_cast<char>(bytes_[offset + index]);
        if (character == '\0') return true;
        value.push_back(character);
    }
    value.clear();
    return false;
}

bool PEImage::Parse(std::string& error)
{
    IMAGE_DOS_HEADER dos = {};
    if (!ReadFile(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
    {
        error = "Invalid or truncated DOS header";
        return false;
    }

    const uint64_t ntOffset = static_cast<uint32_t>(dos.e_lfanew);
    DWORD signature = 0;
    IMAGE_FILE_HEADER fileHeader = {};
    if (!ReadFile(ntOffset, signature) || signature != IMAGE_NT_SIGNATURE ||
        !ReadFile(ntOffset + sizeof(signature), fileHeader))
    {
        error = "Invalid or truncated NT headers";
        return false;
    }

    const uint64_t optionalOffset = ntOffset + sizeof(signature) + sizeof(fileHeader);
    WORD magic = 0;
    if (!ReadFile(optionalOffset, magic))
    {
        error = "Missing PE optional header";
        return false;
    }

    headers_ = {};
    headers_.machine = fileHeader.Machine;
    headers_.timestamp = fileHeader.TimeDateStamp;

    DWORD directoryCount = 0;
    const IMAGE_DATA_DIRECTORY* directories = nullptr;
    if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        IMAGE_OPTIONAL_HEADER64 optional = {};
        if (fileHeader.SizeOfOptionalHeader < sizeof(optional) ||
            !ReadFile(optionalOffset, optional))
        {
            error = "Invalid or truncated PE32+ optional header";
            return false;
        }
        headers_.isPe32Plus = true;
        headers_.preferredImageBase = optional.ImageBase;
        headers_.entryPointRva = optional.AddressOfEntryPoint;
        headers_.sectionAlignment = optional.SectionAlignment;
        headers_.fileAlignment = optional.FileAlignment;
        headers_.subsystem = optional.Subsystem;
        headers_.dllCharacteristics = optional.DllCharacteristics;
        headers_.imageSize = optional.SizeOfImage;
        headers_.headersSize = optional.SizeOfHeaders;
        headers_.checksum = optional.CheckSum;
        directoryCount = optional.NumberOfRvaAndSizes;
        directories = optional.DataDirectory;

        const DWORD count = std::min<DWORD>(directoryCount, IMAGE_NUMBEROF_DIRECTORY_ENTRIES);
        for (DWORD index = 0; index < count; ++index)
            headers_.dataDirectories.push_back({
                index, directories[index].VirtualAddress, directories[index].Size,
                index == IMAGE_DIRECTORY_ENTRY_SECURITY});
    }
    else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        IMAGE_OPTIONAL_HEADER32 optional = {};
        if (fileHeader.SizeOfOptionalHeader < sizeof(optional) ||
            !ReadFile(optionalOffset, optional))
        {
            error = "Invalid or truncated PE32 optional header";
            return false;
        }
        headers_.isPe32Plus = false;
        headers_.preferredImageBase = optional.ImageBase;
        headers_.entryPointRva = optional.AddressOfEntryPoint;
        headers_.sectionAlignment = optional.SectionAlignment;
        headers_.fileAlignment = optional.FileAlignment;
        headers_.subsystem = optional.Subsystem;
        headers_.dllCharacteristics = optional.DllCharacteristics;
        headers_.imageSize = optional.SizeOfImage;
        headers_.headersSize = optional.SizeOfHeaders;
        headers_.checksum = optional.CheckSum;
        directoryCount = optional.NumberOfRvaAndSizes;
        directories = optional.DataDirectory;

        const DWORD count = std::min<DWORD>(directoryCount, IMAGE_NUMBEROF_DIRECTORY_ENTRIES);
        for (DWORD index = 0; index < count; ++index)
            headers_.dataDirectories.push_back({
                index, directories[index].VirtualAddress, directories[index].Size,
                index == IMAGE_DIRECTORY_ENTRY_SECURITY});
    }
    else
    {
        error = "Unsupported PE optional-header magic";
        return false;
    }

    const uint64_t sectionOffset = optionalOffset + fileHeader.SizeOfOptionalHeader;
    if (fileHeader.NumberOfSections > 4096 ||
        AddWouldOverflow(sectionOffset,
            static_cast<uint64_t>(fileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER)) ||
        sectionOffset + static_cast<uint64_t>(fileHeader.NumberOfSections) *
            sizeof(IMAGE_SECTION_HEADER) > bytes_.size())
    {
        error = "Invalid or truncated section table";
        return false;
    }

    sections_.clear();
    sections_.reserve(fileHeader.NumberOfSections);
    for (WORD index = 0; index < fileHeader.NumberOfSections; ++index)
    {
        IMAGE_SECTION_HEADER raw = {};
        ReadFile(sectionOffset + static_cast<uint64_t>(index) * sizeof(raw), raw);

        if (raw.SizeOfRawData != 0 &&
            (raw.PointerToRawData > bytes_.size() ||
             raw.SizeOfRawData > bytes_.size() - raw.PointerToRawData))
        {
            error = "Section raw data lies outside the file";
            return false;
        }

        PeSection section;
        section.name = SectionName(raw);
        section.rva = raw.VirtualAddress;
        section.virtualSize = raw.Misc.VirtualSize;
        section.rawOffset = raw.PointerToRawData;
        section.rawSize = raw.SizeOfRawData;
        section.characteristics = raw.Characteristics;
        section.executable = (raw.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        section.readable = (raw.Characteristics & IMAGE_SCN_MEM_READ) != 0;
        section.writable = (raw.Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        section.entropy = CalculateEntropy(bytes_, section.rawOffset, section.rawSize);
        sections_.push_back(section);
    }

    return true;
}
