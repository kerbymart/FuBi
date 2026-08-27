#define BOOST_TEST_MODULE PrototypeProfileTests
#include <boost/test/included/unit_test.hpp>

#include "PrototypeProfile.h"
#include "DbgHelpDll.h"

#include <windows.h>
#include <algorithm>
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

std::string ProfileDocument(const FunctionCatalog& catalog, const std::string& architecture,
    uint32_t rva = 0x1010)
{
    std::ostringstream output;
    output << "{\"schema_version\":1,\"module\":{\"sha256\":\""
        << catalog.Module().sha256 << "\",\"architecture\":\"" << architecture
        << "\"},\"functions\":[{\"rva\":" << rva
        << ",\"selector\":\"NamedExport\",\"abi\":\""
        << (architecture == "x64" ? "x64" : "__cdecl")
        << "\",\"return_type\":{\"kind\":\"integer\",\"width\":32},"
        << "\"parameters\":[{\"kind\":\"integer\",\"width\":32},"
        << "{\"kind\":\"integer\",\"width\":32}],\"variadic\":false}]}";
    return output.str();
}
}

BOOST_AUTO_TEST_CASE(ProfileIdentityAndTypesAreValidated)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    PrototypeProfile profile;
    std::vector<ProfileValidationError> errors;
    BOOST_REQUIRE(ParsePrototypeProfile(ProfileDocument(catalog, catalog.Module().architecture), profile, errors));
    BOOST_CHECK(ValidatePrototypeProfile(profile, catalog, errors));
    BOOST_CHECK_EQUAL(profile.functions.size(), 1U);
    BOOST_CHECK_EQUAL(profile.functions.front().prototype.parameters.size(), 2U);
}

BOOST_AUTO_TEST_CASE(MismatchedIdentityAndDuplicateRvaAreRejected)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    PrototypeProfile profile;
    std::vector<ProfileValidationError> errors;
    BOOST_REQUIRE(ParsePrototypeProfile(ProfileDocument(catalog, "x86"), profile, errors));
    BOOST_CHECK(!ValidatePrototypeProfile(profile, catalog, errors));
    BOOST_CHECK(!errors.empty());
    profile.functions.push_back(profile.functions.front());
    BOOST_CHECK(!ValidatePrototypeProfile(profile, catalog, errors));
    BOOST_CHECK(std::any_of(errors.begin(), errors.end(), [](const ProfileValidationError& item) {
        return item.code == "duplicate-selector";
    }));
}

BOOST_AUTO_TEST_CASE(ConflictingEvidenceIsRetainedAndInferredIsNotCallable)
{
    FunctionRecord record;
    record.callability = Callability::RequiresPrototype;
    PrototypeSpec first;
    first.abi = "x64";
    first.returnType.kind = TypeKind::Integer;
    first.returnType.width = 32;
    BOOST_CHECK(MergePrototypeEvidence(record, first, "profile", PrototypeQuality::UserDeclared));
    BOOST_CHECK(record.callability == Callability::Callable);
    PrototypeSpec second = first;
    second.returnType.width = 64;
    BOOST_CHECK(!MergePrototypeEvidence(record, second, "symbol-inferred", PrototypeQuality::Inferred));
    BOOST_CHECK_EQUAL(record.prototypeConflicts.size(), 1U);
}

BOOST_AUTO_TEST_CASE(ProfileApplicationIsCallableAndTransactional)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    PrototypeProfile profile;
    std::vector<ProfileValidationError> errors;
    BOOST_REQUIRE(ParsePrototypeProfile(ProfileDocument(catalog, catalog.Module().architecture), profile, errors));
    BOOST_REQUIRE(catalog.ApplyProfile(profile, errors));
    BOOST_CHECK(catalog.Find("NamedExport")->callability == Callability::Callable);

    PrototypeProfile conflicting = profile;
    conflicting.functions.front().prototype.returnType.width = 64;
    errors.clear();
    BOOST_CHECK(!catalog.ApplyProfile(conflicting, errors));
    BOOST_CHECK_EQUAL(catalog.Find("NamedExport")->prototype.returnType.width, 32);
}

BOOST_AUTO_TEST_CASE(IncompleteUserPrototypeCannotBecomeCallable)
{
    FunctionRecord record;
    record.callability = Callability::RequiresPrototype;
    PrototypeSpec incomplete;
    incomplete.abi = "x64";
    BOOST_CHECK(!MergePrototypeEvidence(record, incomplete, "profile", PrototypeQuality::UserDeclared));
    BOOST_CHECK(!record.hasPrototype);
    BOOST_CHECK(record.callability == Callability::RequiresPrototype);
}

BOOST_AUTO_TEST_CASE(PdbIdentityAndTypeShapeAreRequiredWhenSupplied)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    PrototypeProfile profile;
    std::vector<ProfileValidationError> errors;
    BOOST_REQUIRE(ParsePrototypeProfile(ProfileDocument(catalog, catalog.Module().architecture), profile, errors));
    profile.module.pdbGuid = "01234567-89AB-CDEF-0123-456789ABCDEF";
    profile.module.pdbAge = 1;
    BOOST_CHECK(!ValidatePrototypeProfile(profile, catalog, errors));
    BOOST_CHECK(std::any_of(errors.begin(), errors.end(), [](const ProfileValidationError& item) {
        return item.code == "pdb-identity-unavailable";
    }));

    FunctionRecord record;
    record.callability = Callability::RequiresPrototype;
    PrototypeSpec invalid;
    invalid.abi = "x64";
    invalid.returnType.kind = TypeKind::Integer;
    invalid.returnType.width = 7;
    BOOST_CHECK(!MergePrototypeEvidence(record, invalid, "profile", PrototypeQuality::UserDeclared));
    BOOST_CHECK(!record.hasPrototype);
}

BOOST_AUTO_TEST_CASE(SymbolProviderRejectsUnavailableOrMismatchedCodeView)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    DbgHelpDll symbols;
    std::vector<SymbolPrototypeEvidence> evidence;
    ModuleIdentity expected = catalog.Module();
    expected.pdbGuid = "01234567-89AB-CDEF-0123-456789ABCDEF";
    expected.pdbAge = 1;
    BOOST_CHECK(!symbols.EnumerateExactFunctionSymbols(FixturePath(), expected, evidence, error));
    BOOST_CHECK(evidence.empty());
    BOOST_CHECK(!error.empty());
}
