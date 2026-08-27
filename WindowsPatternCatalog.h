#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct WindowsPatternEvidence { uint32_t rva=0; std::string patternId; std::string provenance; };
bool MatchWindowsPattern(const std::vector<uint8_t>& bytes, size_t offset, const std::string& patternId);
bool ScanWindowsCallPatterns(const std::string& imagePath, std::vector<WindowsPatternEvidence>& evidence, std::string& error);
