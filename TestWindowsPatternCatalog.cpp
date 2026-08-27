#define BOOST_TEST_MODULE WindowsPatternCatalogTests
#include <boost/test/included/unit_test.hpp>
#include "WindowsPatternCatalog.h"
#include <windows.h>
#include <algorithm>

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
BOOST_AUTO_TEST_CASE(UnknownAndTruncatedPatternsAreRejected){BOOST_CHECK(!MatchWindowsPattern({0x48,0x83,0xEC},0,"win-x64-stack-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x55,0x8B,0xEC},1,"win-x86-frame-prologue"));BOOST_CHECK(!MatchWindowsPattern({0x55,0x8B,0xEC},0,"unknown"));}
BOOST_AUTO_TEST_CASE(HugeOffsetsAreRejected){BOOST_CHECK(!MatchWindowsPattern({0x48,0x83,0xEC,0x28},static_cast<size_t>(-1),"win-x64-stack-prologue"));}

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
    BOOST_REQUIRE(direct != evidence.end());
    BOOST_CHECK(direct->rva != 0);
    BOOST_CHECK_EQUAL(direct->targetKind, "function");
    BOOST_CHECK(direct->targetRva != 0);
    BOOST_REQUIRE(imported != evidence.end());
    BOOST_CHECK(imported->rva != 0);
    BOOST_CHECK_EQUAL(imported->targetKind, "iat-slot");
    BOOST_CHECK(imported->targetRva != 0);
    BOOST_CHECK(std::all_of(evidence.begin(), evidence.end(), [](const auto& item) {
        return item.provenance == "static-pe-pattern-v1";
    }));
}
