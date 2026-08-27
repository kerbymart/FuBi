#define BOOST_TEST_MODULE PrototypeProfileTests
#include <boost/test/included/unit_test.hpp>

#include "PrototypeProfile.h"
#include "DbgHelpDll.h"
#include "CallContract.h"

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
    uint32_t rva = 0)
{
    if (rva == 0)
    {
        const FunctionRecord* named = catalog.Find("NamedExport");
        rva = named == nullptr ? 0 : named->startRva;
    }
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
        return item.code == "pdb-identity-mismatch";
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

BOOST_AUTO_TEST_CASE(SymbolProviderMapsCompleteFixtureTypeGraph)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    BOOST_REQUIRE_MESSAGE(!catalog.Module().pdbGuid.empty(),
        "the controlled fixture must carry CodeView identity for this test");

    DbgHelpDll symbols;
    std::vector<SymbolPrototypeEvidence> evidence;
    BOOST_REQUIRE_MESSAGE(symbols.EnumerateExactFunctionSymbols(
        FixturePath(), catalog.Module(), evidence, error), error);
    const auto match = std::find_if(evidence.begin(), evidence.end(),
        [](const SymbolPrototypeEvidence& item) { return item.name == "NamedExport"; });
    BOOST_REQUIRE(match != evidence.end());
    BOOST_CHECK_EQUAL(match->prototype.source, "dbghelp-pdb-type-graph");
    BOOST_CHECK(match->prototype.quality == PrototypeQuality::ExactSymbol);
    BOOST_CHECK(match->prototype.abi == "x64" || match->prototype.abi == "__cdecl");
    BOOST_CHECK(match->prototype.returnType.kind == TypeKind::Integer);
    BOOST_CHECK_EQUAL(match->prototype.returnType.width, 32U);
    BOOST_CHECK(match->prototype.parameters.empty());

    const auto pointerMatch = std::find_if(evidence.begin(), evidence.end(),
        [](const SymbolPrototypeEvidence& item) { return item.name == "PointerEcho"; });
    BOOST_REQUIRE(pointerMatch != evidence.end());
    BOOST_CHECK(pointerMatch->prototype.quality == PrototypeQuality::ExactSymbol);
    BOOST_CHECK(pointerMatch->prototype.returnType.kind == TypeKind::Pointer);
    BOOST_CHECK_EQUAL(pointerMatch->prototype.returnType.pointerDepth, 1U);
    BOOST_REQUIRE_EQUAL(pointerMatch->prototype.parameters.size(), 1U);
    BOOST_CHECK(pointerMatch->prototype.parameters.front().kind == TypeKind::Pointer);
    BOOST_CHECK_EQUAL(pointerMatch->prototype.parameters.front().pointerDepth, 1U);
}

BOOST_AUTO_TEST_CASE(ExactSymbolEvidenceMakesMatchingRecordCallable)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    const FunctionRecord* original = catalog.Find("NamedExport");
    BOOST_REQUIRE(original != nullptr);
    SymbolPrototypeEvidence evidence;
    evidence.module = catalog.Module();
    evidence.rva = original->startRva;
    evidence.name = "NamedExport";
    evidence.prototype.quality = PrototypeQuality::ExactSymbol;
    evidence.prototype.abi = catalog.Module().architecture == "x64" ? "x64" : "__cdecl";
    evidence.prototype.returnType.kind = TypeKind::Integer;
    evidence.prototype.returnType.width = 32;
    evidence.prototype.parameters.push_back({TypeKind::Integer, 32});
    std::vector<SymbolPrototypeEvidence> symbols = {evidence};
    BOOST_REQUIRE(catalog.ApplySymbolEvidence(symbols, error));
    BOOST_CHECK(catalog.Find("NamedExport")->callability == Callability::Callable);
    BOOST_CHECK(catalog.Find("NamedExport")->prototype.quality == PrototypeQuality::ExactSymbol);
    BOOST_CHECK_EQUAL(catalog.Find("NamedExport")->id.symbol, "NamedExport");
}

BOOST_AUTO_TEST_CASE(InferredSymbolEvidenceRemainsDisplayOnly)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    const FunctionRecord* original = catalog.Find("NamedExport");
    BOOST_REQUIRE(original != nullptr);
    SymbolPrototypeEvidence evidence;
    evidence.module = catalog.Module();
    evidence.rva = original->startRva;
    evidence.name = "NamedExport";
    evidence.prototype.quality = PrototypeQuality::Inferred;
    evidence.prototype.abi = catalog.Module().architecture == "x64" ? "x64" : "__cdecl";
    evidence.prototype.returnType.kind = TypeKind::Integer;
    evidence.prototype.returnType.width = 32;
    std::string applyError;
    BOOST_REQUIRE(catalog.ApplySymbolEvidence({evidence}, applyError));
    BOOST_CHECK(catalog.Find("NamedExport")->callability == Callability::RequiresPrototype);
}

BOOST_AUTO_TEST_CASE(IncompletePdbTypeGraphRemainsDisplayOnly)
{
    SymbolPrototypeEvidence evidence;
    evidence.prototype.abi = "x64";
    evidence.prototype.quality = PrototypeQuality::Inferred;
    evidence.prototype.source = "dbghelp-type-metadata-display";
    evidence.prototype.returnType = {TypeKind::Integer, 32};
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    evidence.module = catalog.Module();
    evidence.rva = catalog.Find("NamedExport")->startRva;
    evidence.name = "NamedExport";
    BOOST_REQUIRE(catalog.ApplySymbolEvidence({evidence}, error));
    BOOST_CHECK(static_cast<int>(catalog.Find("NamedExport")->callability) == static_cast<int>(Callability::RequiresPrototype));
    BOOST_CHECK(static_cast<int>(catalog.Find("NamedExport")->prototype.quality) == static_cast<int>(PrototypeQuality::Inferred));
}

BOOST_AUTO_TEST_CASE(MismatchedPdbGraphCannotAuthorizeCalls)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    const FunctionRecord* record = catalog.Find("NamedExport");
    BOOST_REQUIRE(record != nullptr);
    SymbolPrototypeEvidence evidence;
    evidence.module = catalog.Module();
    evidence.rva = record->startRva;
    evidence.name = "NamedExport";
    evidence.prototype.abi = catalog.Module().architecture == "x64" ? "x64" : "__cdecl";
    evidence.prototype.quality = PrototypeQuality::Inferred;
    evidence.prototype.source = "dbghelp-type-graph-display";
    evidence.prototype.returnType = {TypeKind::Integer, 32};
    BOOST_REQUIRE(catalog.ApplySymbolEvidence({evidence}, error));
    BOOST_CHECK(static_cast<int>(catalog.Find("NamedExport")->callability) == static_cast<int>(Callability::RequiresPrototype));
}

BOOST_AUTO_TEST_CASE(InternalAuthorizationIsCatalogOwned)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    const FunctionRecord* internal = nullptr;
    for (const auto& item : catalog.Functions()) if (item.exportNames.empty() && item.executable) { internal = &item; break; }
    if (internal == nullptr) return;
    const uint32_t internalRva = internal->startRva;
    PrototypeProfile profile;
    std::vector<ProfileValidationError> profileErrors;
    BOOST_REQUIRE(ParsePrototypeProfile(ProfileDocument(catalog, catalog.Module().architecture), profile, profileErrors));
    profile.functions.front().rva = internalRva;
    profile.functions.front().selector.clear();
    BOOST_REQUIRE(catalog.ApplyProfile(profile, profileErrors));
    CallRequest request;
    request.selector = "0x";
    std::ostringstream selector;
    selector << "0x" << std::hex << internalRva;
    request.selector = selector.str();
    request.correlationId = "internal";
    request.allowInternal = true;
    request.authorizationProvenance = "profile:forged";
    request.moduleSha256 = catalog.Module().sha256;
    request.modulePath = catalog.Module().canonicalPath;
    request.moduleTimestamp = catalog.Module().timestamp;
    request.moduleImageSize = catalog.Module().imageSize;
    request.modulePreferredImageBase = catalog.Module().preferredImageBase;
    request.modulePdbGuid = catalog.Module().pdbGuid;
    request.modulePdbAge = catalog.Module().pdbAge;
    const FunctionRecord* authorized = catalog.Find(request.selector);
    if (authorized != nullptr) for (const auto& type : authorized->prototype.parameters) request.arguments.push_back({type, "1"});
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    request.authorizationProvenance = "profile:" + catalog.Module().sha256;
    diagnostics.clear();
    BOOST_CHECK(ValidateCallRequest(request, catalog, diagnostics));
}
