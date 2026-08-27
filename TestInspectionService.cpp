#define BOOST_TEST_MODULE InspectionServiceTests
#include <boost/test/included/unit_test.hpp>

#include "InspectionService.h"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>

namespace
{
std::string FixturePath(const char* name)
{
    char executablePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    const std::string path(executablePath);
    return path.substr(0, path.find_last_of("\\/") + 1) + name;
}
}

BOOST_AUTO_TEST_CASE(ExportsInspectionIsStaticAndDeterministic)
{
    const std::string marker = FixturePath("static_fixture.executed");
    DeleteFileA(marker.c_str());
    InspectionReport first;
    InspectionReport second;
    std::string error;
    BOOST_REQUIRE(InspectionService::Inspect(FixturePath("static_fixture.dll"), "exports", first, error));
    BOOST_REQUIRE(InspectionService::Inspect(FixturePath("static_fixture.dll"), "exports", second, error));
    BOOST_CHECK_EQUAL(first.exports.size(), second.exports.size());
    BOOST_CHECK_EQUAL(GetFileAttributesA(marker.c_str()), INVALID_FILE_ATTRIBUTES);
    std::ostringstream firstJson;
    std::ostringstream secondJson;
    InspectionService::WriteJson(firstJson, first);
    InspectionService::WriteJson(secondJson, second);
    BOOST_CHECK_EQUAL(firstJson.str(), secondJson.str());
}

BOOST_AUTO_TEST_CASE(ImportsAndRuntimeFunctionsAreBoundedStaticReports)
{
    InspectionReport report;
    std::string error;
    BOOST_REQUIRE(InspectionService::Inspect(FixturePath("static_fixture.dll"), "imports", report, error));
    BOOST_CHECK(!report.imports.empty());

    BOOST_REQUIRE(InspectionService::Inspect(FixturePath("static_fixture.dll"), "runtime-functions", report, error));
#if defined(_WIN64)
    BOOST_CHECK(!report.runtimeFunctions.empty());
    for (const InspectionRuntimeFunction& item : report.runtimeFunctions)
        BOOST_CHECK_LE(item.beginRva, item.endRva);
#else
    BOOST_CHECK(report.runtimeFunctions.empty());
#endif
}

BOOST_AUTO_TEST_CASE(DebugAndWdfModesHaveStableContracts)
{
    InspectionReport debug;
    InspectionReport wdf;
    std::string error;
    BOOST_REQUIRE(InspectionService::Inspect(FixturePath("static_fixture.dll"), "debug", debug, error));
    BOOST_REQUIRE(InspectionService::Inspect(FixturePath("static_fixture.dll"), "wdf-bind", wdf, error));
    std::ostringstream debugJson;
    InspectionService::WriteJson(debugJson, debug);
    BOOST_CHECK(debugJson.str().find("\"debug\"") != std::string::npos);
    for (const WindowsPatternEvidence& item : wdf.wdfBindings)
        BOOST_CHECK_EQUAL(item.targetKind, "wdf-table");
}

BOOST_AUTO_TEST_CASE(RejectsUnknownInspectMode)
{
    InspectionReport report;
    std::string error;
    BOOST_CHECK(!InspectionService::Inspect(FixturePath("static_fixture.dll"), "disassembly", report, error));
    BOOST_CHECK_EQUAL(error, "unsupported inspect mode: disassembly");
}
