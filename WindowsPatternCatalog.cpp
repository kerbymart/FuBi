#include "stdafx.h"
#include "WindowsPatternCatalog.h"
#include "PEImage.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace
{
constexpr char kDirectCall[] = "win-x64-direct-call-rel32-v1";
constexpr char kImportCall[] = "win-x64-rip-relative-iat-call-v1";

bool HasBytes(const std::vector<uint8_t>& bytes, size_t offset, size_t count)
{
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

bool ReadRel32(const std::vector<uint8_t>& bytes, size_t offset, int32_t& value)
{
    if (!HasBytes(bytes, offset, sizeof(value))) return false;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return true;
}

bool IsCallPattern(const std::vector<uint8_t>& bytes, size_t offset,
    const std::string& id)
{
    if (id == kDirectCall)
    {
        return HasBytes(bytes, offset, 5) && bytes[offset] == 0xe8;
    }
    if (id == kImportCall)
    {
        return HasBytes(bytes, offset, 6) && bytes[offset] == 0xff &&
            bytes[offset + 1] == 0x15;
    }
    return false;
}

bool CalculateTargetRva(uint32_t instructionRva, uint32_t instructionSize,
    const std::vector<uint8_t>& bytes, size_t displacementOffset,
    uint32_t imageSize, uint32_t& targetRva)
{
    int32_t displacement = 0;
    if (!ReadRel32(bytes, displacementOffset, displacement)) return false;
    const int64_t next = static_cast<int64_t>(instructionRva) + instructionSize;
    const int64_t target = next + static_cast<int64_t>(displacement);
    if (target < 0 || target > std::numeric_limits<uint32_t>::max() ||
        (imageSize != 0 && target >= imageSize)) return false;
    targetRva = static_cast<uint32_t>(target);
    return true;
}
}

bool MatchWindowsPattern(const std::vector<uint8_t>& bytes, size_t offset,
    const std::string& id)
{
    if (offset > bytes.size()) return false;
    if (id == "win-x64-stack-prologue")
    {
        return HasBytes(bytes, offset, 4) && bytes[offset] == 0x48 &&
            bytes[offset + 1] == 0x83 && bytes[offset + 2] == 0xec &&
            bytes[offset + 3] == 0x28;
    }
    if (id == "win-x86-frame-prologue")
    {
        return HasBytes(bytes, offset, 3) && bytes[offset] == 0x55 &&
            bytes[offset + 1] == 0x8b && bytes[offset + 2] == 0xec;
    }
    return IsCallPattern(bytes, offset, id);
}

bool ScanWindowsCallPatterns(const std::string& path,
    std::vector<WindowsPatternEvidence>& out, std::string& error)
{
    constexpr size_t kMaximumEvidence = 100000;
    PEImage image;
    if (!PEImage::Load(path, image, error)) return false;
    out.clear();
    for (const PeSection& section : image.Sections())
    {
        if (!section.executable || section.rawSize == 0 ||
            section.rawOffset >= image.Bytes().size()) continue;
        const uint32_t limit = std::min(section.rawSize,
            static_cast<uint32_t>(image.Bytes().size() - section.rawOffset));
        for (uint32_t i = 0; i < limit; ++i)
        {
            const size_t offset = section.rawOffset + i;
            if (i > std::numeric_limits<uint32_t>::max() - section.rva)
            {
                error = "Windows pattern instruction RVA arithmetic overflow";
                out.clear();
                return false;
            }
            const uint32_t instructionRva = section.rva + i;
            const char* pattern = nullptr;
            uint32_t targetRva = instructionRva;
            uint32_t instructionSize = 0;
            size_t displacementOffset = 0;
            if (image.Headers().isPe32Plus && MatchWindowsPattern(
                    image.Bytes(), offset, kDirectCall))
            {
                pattern = kDirectCall;
                instructionSize = 5;
                displacementOffset = offset + 1;
            }
            else if (image.Headers().isPe32Plus && MatchWindowsPattern(
                         image.Bytes(), offset, kImportCall))
            {
                pattern = kImportCall;
                instructionSize = 6;
                displacementOffset = offset + 2;
            }
            else
            {
                const std::string legacy = image.Headers().isPe32Plus
                    ? "win-x64-stack-prologue" : "win-x86-frame-prologue";
                if (!MatchWindowsPattern(image.Bytes(), offset, legacy)) continue;
                pattern = legacy.c_str();
            }
            if (instructionSize != 0 && !CalculateTargetRva(instructionRva,
                    instructionSize, image.Bytes(), displacementOffset,
                    image.Headers().imageSize, targetRva)) continue;
            if (out.size() >= kMaximumEvidence)
            {
                error = "Windows pattern evidence exceeds the 100000-record limit";
                out.clear();
                return false;
            }
            out.push_back({targetRva, pattern, "static-pe-pattern-v1"});
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.rva < b.rva;
    });
    return true;
}
