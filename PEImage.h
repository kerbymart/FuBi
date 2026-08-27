#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#ifndef FUBI_MAX_IMAGE_BYTES
#define FUBI_MAX_IMAGE_BYTES 67108864ULL
#endif

struct PeDataDirectory
{
    uint32_t index = 0;
    uint32_t rva = 0;
    uint32_t size = 0;
    bool usesFileOffset = false;
};

struct PeSection
{
    std::string name;
    uint32_t rva = 0;
    uint32_t virtualSize = 0;
    uint32_t rawOffset = 0;
    uint32_t rawSize = 0;
    uint32_t characteristics = 0;
    bool executable = false;
    bool readable = false;
    bool writable = false;
    double entropy = 0.0;
};

struct PeHeaders
{
    bool isPe32Plus = false;
    uint16_t machine = 0;
    uint16_t subsystem = 0;
    uint16_t dllCharacteristics = 0;
    uint32_t timestamp = 0;
    uint32_t entryPointRva = 0;
    uint32_t sectionAlignment = 0;
    uint32_t fileAlignment = 0;
    uint32_t imageSize = 0;
    uint32_t headersSize = 0;
    uint32_t checksum = 0;
    uint64_t preferredImageBase = 0;
    std::vector<PeDataDirectory> dataDirectories;
};

class PEImage
{
public:
    static constexpr uint64_t kMaximumImageBytes = FUBI_MAX_IMAGE_BYTES;

    static bool Load(
        const std::string& path, PEImage& image, std::string& error);
    static bool FromBytes(
        std::vector<uint8_t> bytes, const std::string& sourceName,
        PEImage& image, std::string& error);

    const std::string& SourceName() const { return sourceName_; }
    const std::vector<uint8_t>& Bytes() const { return bytes_; }
    const PeHeaders& Headers() const { return headers_; }
    const std::vector<PeSection>& Sections() const { return sections_; }

    std::optional<uint32_t> RvaToFileOffset(uint32_t rva) const;
    const PeSection* FindSection(uint32_t rva) const;
    bool ReadFile(uint64_t offset, void* destination, size_t size) const;
    bool ReadRva(uint32_t rva, void* destination, size_t size) const;
    bool ReadCStringAtRva(
        uint32_t rva, std::string& value, size_t maximumLength = 4096) const;
    bool ReadCStringAtFileOffset(
        uint32_t offset, std::string& value, size_t maximumLength = 4096) const;

    template <typename T>
    bool ReadFile(uint64_t offset, T& value) const
    {
        return ReadFile(offset, &value, sizeof(T));
    }

    template <typename T>
    bool ReadRva(uint32_t rva, T& value) const
    {
        return ReadRva(rva, &value, sizeof(T));
    }

private:
    bool Parse(std::string& error);
    std::optional<size_t> MappedRvaLength(uint32_t rva) const;

    std::string sourceName_;
    std::vector<uint8_t> bytes_;
    PeHeaders headers_;
    std::vector<PeSection> sections_;
};
