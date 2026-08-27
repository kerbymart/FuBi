#define BOOST_TEST_MODULE CallContractTests
#include <boost/test/included/unit_test.hpp>

#include "CallContract.h"
#include "InvocationEngine.h"

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
}

BOOST_AUTO_TEST_CASE(ValidTypedRequestAndDeterministicResult)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId = "call-1";
    request.selector = "NamedExport";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = catalog.Module().architecture == "x64" ? "x64" : "__cdecl";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    request.prototypeOverride.parameters = {{TypeKind::Integer, 32}, {TypeKind::Integer, 32}};
    request.arguments = {{{TypeKind::Integer, 32}, "1"}, {{TypeKind::Integer, 32}, "2"}};
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(ValidateCallRequest(request, catalog, diagnostics));
    std::ostringstream first, second;
    WriteCallRequestJson(first, request);
    WriteCallRequestJson(second, request);
    BOOST_CHECK_EQUAL(first.str(), second.str());
    CallRequest parsed;
    BOOST_REQUIRE(ParseCallRequestJson(first.str(), parsed, diagnostics));
    BOOST_CHECK_EQUAL(parsed.correlationId, request.correlationId);
    BOOST_CHECK_EQUAL(parsed.arguments.size(), request.arguments.size());
    CallResult result;
    result.correlationId = request.correlationId;
    result.resolvedModule = catalog.Module();
    result.durationMs = 12;
    result.returnType = request.prototypeOverride.returnType;
    result.prototypeUsed = request.prototypeOverride;
    result.outputValues = request.arguments;
    result.diagnostics = diagnostics;
    std::ostringstream resultJson;
    WriteCallResultJson(resultJson, result);
    BOOST_CHECK(resultJson.str().find("not-executed") != std::string::npos);
    CallResult parsedResult;
    BOOST_REQUIRE(ParseCallResultJson(resultJson.str(), parsedResult, diagnostics));
    BOOST_CHECK_EQUAL(parsedResult.correlationId, result.correlationId);
    BOOST_CHECK_EQUAL(parsedResult.durationMs, result.durationMs);
    BOOST_CHECK_EQUAL(parsedResult.outputValues.size(), result.outputValues.size());
}

BOOST_AUTO_TEST_CASE(ValidationRejectsBadRangeCountAndDisplayOnlyPrototype)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId = "call-2";
    request.selector = "NamedExport";
    request.arguments = {{{TypeKind::Integer, 8}, "256"}};
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const CallDiagnostic& item) { return item.code == "prototype-required"; }));
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::Inferred;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    request.prototypeOverride.parameters = {{TypeKind::Integer, 8}};
    diagnostics.clear();
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const CallDiagnostic& item) { return item.code == "prototype-not-invocation-grade"; }));
}

BOOST_AUTO_TEST_CASE(X64InvocationLoadsOnlyForExplicitCallAndMarshalsArguments)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));

    CallRequest request;
    request.correlationId = "invoke-1";
    request.selector = "?AddNumbers@@YAHHH@Z";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    request.prototypeOverride.parameters = {{TypeKind::Integer, 32}, {TypeKind::Integer, 32}};
    request.arguments = {{{TypeKind::Integer, 32}, "19"}, {{TypeKind::Integer, 32}, "23"}};

    CallResult result;
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), request, catalog, result, error), error);
    BOOST_CHECK(result.success);
    BOOST_CHECK_EQUAL(result.status, "completed");
    BOOST_CHECK_EQUAL(result.returnValue, "42");
    BOOST_CHECK_EQUAL(result.resolvedModule.sha256, catalog.Module().sha256);
#else
    BOOST_TEST_MESSAGE("x64 invocation test skipped for non-x64 build");
#endif
}

BOOST_AUTO_TEST_CASE(X64InvocationRejectsUnsupportedTypeBeforeLoad)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId = "invoke-2";
    request.selector = "?AddNumbers@@YAHHH@Z";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    request.prototypeOverride.parameters = {{TypeKind::Floating, 64}, {TypeKind::Integer, 32}};
    request.arguments = {{{TypeKind::Floating, 64}, "1.0"}, {{TypeKind::Integer, 32}, "2"}};
    CallResult result;
    BOOST_CHECK(!InvokeX64Export(FixturePath(), request, catalog, result, error));
    BOOST_CHECK_EQUAL(result.status, "validation-failed");
    BOOST_CHECK(!result.diagnostics.empty());
#endif
}

BOOST_AUTO_TEST_CASE(InternalInvocationRejectsForgedAuthorizationBeforeLoad)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    const FunctionRecord* internal = nullptr;
    for (const FunctionRecord& candidate : catalog.Functions())
        if (candidate.exportNames.empty() && candidate.executable) { internal = &candidate; break; }
    BOOST_REQUIRE(internal != nullptr);
    CallRequest request;
    request.correlationId = "internal-forged";
    std::ostringstream selector;
    selector << "0x" << std::hex << internal->startRva;
    request.selector = selector.str();
    request.allowInternal = true;
    request.authorizationProvenance = "profile:forged";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    CallResult result;
    BOOST_CHECK(!InvokeX64Export(FixturePath(), request, catalog, result, error));
    BOOST_CHECK_EQUAL(result.status, "validation-failed");
#endif
}

BOOST_AUTO_TEST_CASE(RequestParserRejectsTrailingUnknownAndDuplicateFields)
{
    std::vector<CallDiagnostic> diagnostics;
    CallRequest request;
    BOOST_CHECK(!ParseCallRequestJson("{\"schema_version\":1,\"correlation_id\":\"x\",\"selector\":\"y\"} trailing", request, diagnostics));
    BOOST_CHECK(!diagnostics.empty());
    diagnostics.clear();
    BOOST_CHECK(!ParseCallRequestJson("{\"schema_version\":1,\"correlation_id\":\"x\",\"selector\":\"y\",\"extra\":1}", request, diagnostics));
    diagnostics.clear();
    BOOST_CHECK(!ParseCallRequestJson("{\"schema_version\":1,\"correlation_id\":\"x\",\"selector\":\"y\",\"selector\":\"z\"}", request, diagnostics));
    diagnostics.clear();
    BOOST_CHECK(!ParseCallRequestJson("{\"schema_version\":1,\"correlation_id\":\"x\",\"selector\":\"y\",\"arguments\":{}}", request, diagnostics));
    diagnostics.clear();
    BOOST_CHECK(!ParseCallRequestJson("{\"schema_version\":1,\"correlation_id\":\"x\",\"selector\":\"y\",\"arguments\":[}", request, diagnostics));
    diagnostics.clear();
    BOOST_CHECK(!ParseCallRequestJson("{\"schema_version\":1,\"correlation_id\":\"x\",\"selector\":\"y\",\"timeout_ms\":1e+}", request, diagnostics));
    diagnostics.clear();
    BOOST_CHECK(!ParseCallRequestJson("{\"schema_version\":1,\"correlation_id\":\"x\",\"selector\":\"y\",\"timeout_ms\":1abc}", request, diagnostics));
    diagnostics.clear();
    BOOST_CHECK(!ParseCallRequestJson("{\"schema_version\":1,\"correlation_id\":\"x\\u12G4\",\"selector\":\"y\"}", request, diagnostics));
}
