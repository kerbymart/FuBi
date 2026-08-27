#define BOOST_TEST_MODULE FunctionCatalogTests
#include <boost/test/included/unit_test.hpp>

#include "FunctionCatalog.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace
{
std::string FixturePath()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string value(path);
    return value.substr(0, value.find_last_of("\\/") + 1) + "export_fixture.dll";
}
}

BOOST_AUTO_TEST_CASE(MergesAliasesAndOrdinalsByRva)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    const FunctionRecord* named = catalog.Find("NamedExport");
    BOOST_REQUIRE(named != nullptr);
    BOOST_CHECK(std::find(named->aliases.begin(), named->aliases.end(), "NamedExport") != named->aliases.end());
    BOOST_CHECK(std::find(named->aliases.begin(), named->aliases.end(), "AliasNamedExport") != named->aliases.end());
    BOOST_CHECK_EQUAL(named->exportOrdinals.size(), 3U);
    BOOST_CHECK_EQUAL(named->exportOrdinals[0], 1U);
    BOOST_CHECK_EQUAL(named->exportOrdinals[1], 2U);
    BOOST_CHECK_EQUAL(named->exportOrdinals[2], 4U);
    std::ostringstream text;
    catalog.WriteText(text);
    BOOST_CHECK(text.str().find("export_count = 10") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(SelectorsAreExplicitAndJsonIsDeterministic)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    BOOST_CHECK(catalog.Find("#1") != nullptr);
    BOOST_CHECK(catalog.Find("0x1010") != nullptr);
    std::ostringstream first;
    std::ostringstream second;
    catalog.WriteJson(first);
    catalog.WriteJson(second);
    BOOST_CHECK_EQUAL(first.str(), second.str());
    BOOST_CHECK(catalog.Find("#-1") == nullptr);
    BOOST_CHECK(catalog.Find(" 0x1010") == nullptr);
    BOOST_CHECK(catalog.Find("0x100000000") == nullptr);
}

BOOST_AUTO_TEST_CASE(ForwardersAreNotCallable)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    const FunctionRecord* forwarded = catalog.Find("ForwardedSleep");
    BOOST_REQUIRE(forwarded != nullptr);
    BOOST_CHECK(forwarded->callability == Callability::Forwarded);
}

BOOST_AUTO_TEST_CASE(StaticCandidatesAreExecutable)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    for (const FunctionRecord& record : catalog.Functions())
        if (record.forwarder.empty()) BOOST_CHECK(record.executable);
}
