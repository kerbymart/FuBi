#define BOOST_TEST_MODULE WindowsPatternCatalogTests
#include <boost/test/included/unit_test.hpp>
#include "WindowsPatternCatalog.h"
#include <windows.h>
#include <algorithm>
#include <fstream>

namespace
{
std::string StaticFixturePath()
{
    char executablePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    std::string path(executablePath);
    return path.substr(0, path.find_last_of("\\/") + 1) + "static_fixture.dll";
}
}

BOOST_AUTO_TEST_CASE(RecognizersRequireExactDocumentedBytes){BOOST_CHECK(MatchWindowsPattern({0x48,0x83,0xEC,0x28},0,"win-x64-stack-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x48,0x83,0xEC,0x20},0,"win-x64-stack-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x49,0x83,0xEC,0x28},0,"win-x64-stack-prologue"));BOOST_CHECK(MatchWindowsPattern({0x55,0x8B,0xEC},0,"win-x86-frame-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x55,0x89,0xE5},0,"win-x86-frame-prologue"));}
BOOST_AUTO_TEST_CASE(RecognizesExactX64CallForms){BOOST_CHECK(MatchWindowsPattern({0xE8,0x10,0x00,0x00,0x00},0,"win-x64-direct-call-rel32-v1"));BOOST_CHECK(MatchWindowsPattern({0xFF,0x15,0x10,0x00,0x00,0x00},0,"win-x64-rip-relative-iat-call-v1"));BOOST_CHECK(!MatchWindowsPattern({0xE9,0x10,0x00,0x00,0x00},0,"win-x64-direct-call-rel32-v1"));BOOST_CHECK(!MatchWindowsPattern({0xFF,0x25,0x10,0x00,0x00,0x00},0,"win-x64-rip-relative-iat-call-v1"));}
BOOST_AUTO_TEST_CASE(RecognizesExactWdfAndCfgForms){BOOST_CHECK(MatchWindowsPattern({0x48,0x8B,0x05,0x10,0x00,0x00,0x00,0x48,0x8B,0x04,0xC8,0xFF,0xD0},0,"win-x64-wdf-table-call-v1"));BOOST_CHECK(MatchWindowsPattern({0x48,0x8B,0x05,0x10,0x00,0x00,0x00,0xFF,0xD0},0,"win-x64-msvc-cfg-dispatch-v1"));BOOST_CHECK(!MatchWindowsPattern({0x48,0x8B,0x05,0x10,0x00,0x00,0x00,0x48,0x8B,0x04,0xC8,0xFF,0xE0},0,"win-x64-wdf-table-call-v1"));BOOST_CHECK(!MatchWindowsPattern({0x48,0x8B,0x05,0x10,0x00,0x00,0x00,0xFF,0xE0},0,"win-x64-msvc-cfg-dispatch-v1"));}
BOOST_AUTO_TEST_CASE(CallFormsRejectTruncatedAndUnknownBytes){BOOST_CHECK(!MatchWindowsPattern({0xE8,0x10,0x00,0x00},0,"win-x64-direct-call-rel32-v1"));BOOST_CHECK(!MatchWindowsPattern({0xFF,0x15,0x10,0x00,0x00},0,"win-x64-rip-relative-iat-call-v1"));BOOST_CHECK(!MatchWindowsPattern({0xE8,0x10,0x00,0x00,0x00},1,"win-x64-direct-call-rel32-v1"));BOOST_CHECK(!MatchWindowsPattern({0xE8,0x10,0x00,0x00,0x00},0,"unknown"));}
BOOST_AUTO_TEST_CASE(WdfAndCfgFormsRejectTruncatedBytes){BOOST_CHECK(!MatchWindowsPattern({0x48,0x8B,0x05,0x10,0x00,0x00},0,"win-x64-wdf-table-call-v1"));BOOST_CHECK(!MatchWindowsPattern({0x48,0x8B,0x05,0x10,0x00,0x00,0x00,0x48,0x8B,0x04,0xC8,0xFF},0,"win-x64-wdf-table-call-v1"));BOOST_CHECK(!MatchWindowsPattern({0x48,0x8B,0x05,0x10,0x00,0x00,0x00,0xFF},0,"win-x64-msvc-cfg-dispatch-v1"));}
BOOST_AUTO_TEST_CASE(UnknownAndTruncatedPatternsAreRejected){BOOST_CHECK(!MatchWindowsPattern({0x48,0x83,0xEC},0,"win-x64-stack-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x55,0x8B,0xEC},1,"win-x86-frame-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x55,0x8B,0xEC},0,"unknown"));}
BOOST_AUTO_TEST_CASE(HugeOffsetsAreRejected){BOOST_CHECK(!MatchWindowsPattern({0x48,0x83,0xEC,0x28},static_cast<size_t>(-1),"win-x64-stack-prologue"));}

BOOST_AUTO_TEST_CASE(ScanIsDeterministicAndDoesNotExecuteTheFixture)
{
    const std::string markerPath = StaticFixturePath().substr(
        0, StaticFixturePath().find_last_of("\\/")) + "\\static_fixture.executed";
    DeleteFileA(markerPath.c_str());

    std::vector<WindowsPatternEvidence> first;
    std::vector<WindowsPatternEvidence> second;
    std::string firstError;
    std::string secondError;
    BOOST_REQUIRE_MESSAGE(ScanWindowsCallPatterns(StaticFixturePath(), first, firstError), firstError);
    BOOST_REQUIRE_MESSAGE(ScanWindowsCallPatterns(StaticFixturePath(), second, secondError), secondError);
    BOOST_CHECK_EQUAL(GetFileAttributesA(markerPath.c_str()), INVALID_FILE_ATTRIBUTES);
    BOOST_REQUIRE_EQUAL(first.size(), second.size());
    for (size_t index = 0; index < first.size(); ++index)
    {
        BOOST_CHECK_EQUAL(first[index].rva, second[index].rva);
        BOOST_CHECK_EQUAL(first[index].patternId, second[index].patternId);
        BOOST_CHECK_EQUAL(first[index].provenance, second[index].provenance);
        BOOST_CHECK_EQUAL(first[index].targetRva, second[index].targetRva);
        BOOST_CHECK_EQUAL(first[index].targetKind, second[index].targetKind);
    }
    BOOST_CHECK(std::is_sorted(first.begin(), first.end(),
        [](const auto& left, const auto& right) { return left.rva < right.rva; }));
}

BOOST_AUTO_TEST_CASE(TruncatedFixtureIsRejectedWithoutPartialEvidence)
{
    std::ifstream input(StaticFixturePath(), std::ios::binary);
    BOOST_REQUIRE(input.good());
    std::vector<uint8_t> truncated(32, 0);
    BOOST_REQUIRE(input.read(reinterpret_cast<char*>(truncated.data()),
        static_cast<std::streamsize>(truncated.size())));

    const std::string path = StaticFixturePath().substr(
        0, StaticFixturePath().find_last_of("\\/")) + "\\truncated_static_fixture.dll";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        BOOST_REQUIRE(output.good());
        output.write(reinterpret_cast<const char*>(truncated.data()),
            static_cast<std::streamsize>(truncated.size()));
        BOOST_REQUIRE(output.good());
    }

    std::vector<WindowsPatternEvidence> evidence;
    std::string error;
    BOOST_CHECK(!ScanWindowsCallPatterns(path, evidence, error));
    BOOST_CHECK(evidence.empty());
    BOOST_CHECK(!error.empty());
    DeleteFileA(path.c_str());
}

BOOST_AUTO_TEST_CASE(ScansControlledPeForDirectAndImportCalls)
{
    std::vector<WindowsPatternEvidence> evidence;
    std::string error;
    BOOST_REQUIRE_MESSAGE(ScanWindowsCallPatterns(StaticFixturePath(), evidence, error), error);

    const auto direct = std::find_if(evidence.begin(), evidence.end(), [](const auto& item) {
        return item.patternId == "win-x64-direct-call-rel32-v1";
    });
    const auto imported = std::find_if(evidence.begin(), evidence.end(), [](const auto& item) {
        return item.patternId == "win-x64-rip-relative-iat-call-v1";
    });
    const auto wdf = std::find_if(evidence.begin(), evidence.end(), [](const auto& item) {
        return item.patternId == "win-x64-wdf-table-call-v1";
    });
    const auto cfg = std::find_if(evidence.begin(), evidence.end(), [](const auto& item) {
        return item.patternId == "win-x64-msvc-cfg-dispatch-v1";
    });
    BOOST_REQUIRE(direct != evidence.end());
    BOOST_CHECK(direct->rva != 0);
    BOOST_CHECK_EQUAL(direct->targetKind, "function");
    BOOST_CHECK(direct->targetRva != 0);
    BOOST_REQUIRE(imported != evidence.end());
    BOOST_CHECK(imported->rva != 0);
    BOOST_CHECK_EQUAL(imported->targetKind, "iat-slot");
    BOOST_CHECK(imported->targetRva != 0);
    BOOST_REQUIRE(wdf != evidence.end());
    BOOST_CHECK(wdf->rva != 0);
    BOOST_CHECK_EQUAL(wdf->targetKind, "wdf-table");
    BOOST_CHECK(wdf->targetRva != 0);
    BOOST_REQUIRE(cfg != evidence.end());
    BOOST_CHECK(cfg->rva != 0);
    BOOST_CHECK_EQUAL(cfg->targetKind, "cfg-dispatch-target");
    BOOST_CHECK(cfg->targetRva != 0);
    BOOST_CHECK(std::all_of(evidence.begin(), evidence.end(), [](const auto& item) {
        return item.provenance == "static-pe-pattern-v1";
    }));
}
