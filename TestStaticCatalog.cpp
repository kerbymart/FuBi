#define BOOST_TEST_MODULE StaticExportCatalogTests
#include <boost/test/included/unit_test.hpp>

#include <windows.h>

#include <algorithm>
#include <string>

#include "StaticExportCatalog.h"

namespace
{
std::string FixturePath()
{
    char executablePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    std::string path(executablePath);
    return path.substr(0, path.find_last_of("\\/") + 1) + "static_fixture.dll";
}

std::string MarkerPath()
{
    char executablePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    std::string path(executablePath);
    return path.substr(0, path.find_last_of("\\/") + 1) + "static_fixture.executed";
}
}

BOOST_AUTO_TEST_CASE(EnumeratesExportsWithoutLoadingTheTarget)
{
    const std::string markerPath = MarkerPath();
    DeleteFileA(markerPath.c_str());

    StaticExportCatalog catalog;
    std::string error;
    BOOST_REQUIRE(StaticExportCatalog::Load(FixturePath(), catalog, error));
    BOOST_CHECK_EQUAL(GetFileAttributesA(markerPath.c_str()), INVALID_FILE_ATTRIBUTES);

    const auto driverEntry = std::find_if(
        catalog.Exports().begin(), catalog.Exports().end(),
        [](const StaticExport& item)
        {
            return std::find(item.names.begin(), item.names.end(), "FxDriverEntryUm") !=
                item.names.end();
        });
    BOOST_CHECK(driverEntry != catalog.Exports().end());
}
