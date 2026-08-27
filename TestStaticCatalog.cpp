#define BOOST_TEST_MODULE StaticExportCatalogTests
#include <boost/test/included/unit_test.hpp>

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "PEImage.h"
#include "StaticExportCatalog.h"

namespace
{
std::string OutputPath(const char* name)
{
    char executablePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    std::string path(executablePath);
    return path.substr(0, path.find_last_of("\\/") + 1) + name;
}

std::string FixturePath()
{
    return OutputPath("static_fixture.dll");
}

std::string ExportFixturePath()
{
    return OutputPath("export_fixture.dll");
}

const StaticExport* FindOrdinal(const StaticExportCatalog& catalog, uint32_t ordinal)
{
    const auto found = std::find_if(
        catalog.Exports().begin(), catalog.Exports().end(),
        [ordinal](const StaticExport& item) { return item.ordinal == ordinal; });
    return found == catalog.Exports().end() ? nullptr : &*found;
}

std::vector<uint8_t> ReadBytes(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    BOOST_REQUIRE(input.good());
    const std::streamoff size = input.tellg();
    BOOST_REQUIRE(size > 0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    BOOST_REQUIRE(input.read(reinterpret_cast<char*>(bytes.data()), size));
    return bytes;
}

const PeDataDirectory* FindExportDirectory(const PEImage& image)
{
    for (const PeDataDirectory& directory : image.Headers().dataDirectories)
        if (directory.index == IMAGE_DIRECTORY_ENTRY_EXPORT) return &directory;
    return nullptr;
}
}

BOOST_AUTO_TEST_CASE(EnumeratesExportsWithoutLoadingTheTarget)
{
    const std::string markerPath = OutputPath("static_fixture.executed");
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

BOOST_AUTO_TEST_CASE(PreservesNamedOrdinalForwardedAndAliasedExports)
{
    StaticExportCatalog catalog;
    std::string error;
    BOOST_REQUIRE(StaticExportCatalog::Load(ExportFixturePath(), catalog, error));

    const StaticExport* named = FindOrdinal(catalog, 1);
    BOOST_REQUIRE(named != nullptr);
    BOOST_REQUIRE_EQUAL(named->names.size(), 1U);
    BOOST_CHECK_EQUAL(named->names.front(), "NamedExport");

    const StaticExport* ordinalOnly = FindOrdinal(catalog, 2);
    BOOST_REQUIRE(ordinalOnly != nullptr);
    BOOST_CHECK(ordinalOnly->names.empty());

    const StaticExport* forwarded = FindOrdinal(catalog, 3);
    BOOST_REQUIRE(forwarded != nullptr);
    BOOST_CHECK_EQUAL(forwarded->forwarder, "KERNEL32.Sleep");

    const StaticExport* alias = FindOrdinal(catalog, 4);
    BOOST_REQUIRE(alias != nullptr);
    BOOST_REQUIRE_EQUAL(alias->names.size(), 1U);
    BOOST_CHECK_EQUAL(alias->names.front(), "AliasNamedExport");
    BOOST_CHECK_EQUAL(alias->rva, named->rva);
}

BOOST_AUTO_TEST_CASE(RejectsMalformedExportTablesAndCrossSectionReads)
{
    PEImage image;
    std::string error;
    BOOST_REQUIRE(PEImage::Load(ExportFixturePath(), image, error));

    const auto section = std::find_if(
        image.Sections().begin(), image.Sections().end(),
        [](const PeSection& item) { return item.rawSize > 1; });
    BOOST_REQUIRE(section != image.Sections().end());
    uint16_t value = 0;
    BOOST_CHECK(!image.ReadRva(section->rva + section->rawSize - 1, value));

    const PeDataDirectory* exportDirectory = FindExportDirectory(image);
    BOOST_REQUIRE(exportDirectory != nullptr);
    const std::optional<uint32_t> exportOffset = image.RvaToFileOffset(exportDirectory->rva);
    BOOST_REQUIRE(exportOffset.has_value());

    std::vector<uint8_t> bytes = ReadBytes(ExportFixturePath());
    IMAGE_EXPORT_DIRECTORY table = {};
    BOOST_REQUIRE(image.ReadRva(exportDirectory->rva, table));
    table.NumberOfFunctions = 1'000'001;
    std::memcpy(bytes.data() + *exportOffset, &table, sizeof(table));

    const std::string malformedPath = OutputPath("malformed_export_fixture.dll");
    {
        std::ofstream output(malformedPath, std::ios::binary | std::ios::trunc);
        BOOST_REQUIRE(output.good());
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        BOOST_REQUIRE(output.good());
    }

    StaticExportCatalog catalog;
    BOOST_CHECK(!StaticExportCatalog::Load(malformedPath, catalog, error));
    BOOST_CHECK_EQUAL(error, "Export table exceeds safety limit");
    DeleteFileA(malformedPath.c_str());
}
