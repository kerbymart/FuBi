#define BOOST_TEST_MODULE CallContractTests
#include <boost/test/included/unit_test.hpp>

#include "CallContract.h"
#include "InvocationEngine.h"
#include "ProcessInvocation.h"

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

BOOST_AUTO_TEST_CASE(WorkerSelectionRejectsUnknownArchitectureBeforeLaunch)
{
    std::string workerPath = "stale-worker-path";
    std::string error;
    BOOST_CHECK(!SelectInvocationWorker("arm64", workerPath, error));
    BOOST_CHECK(workerPath.empty());
    BOOST_CHECK_EQUAL(error, "unsupported target architecture");
}

BOOST_AUTO_TEST_CASE(WorkerSelectionValidatesSupportedArchitectures)
{
    std::string workerPath;
    std::string error;
#if defined(_M_X64)
    BOOST_CHECK(SelectInvocationWorker("x64", workerPath, error));
    BOOST_CHECK(!workerPath.empty());
    BOOST_CHECK(workerPath.find("FubiInvocationWorker.exe") != std::string::npos);

    workerPath.clear();
    error.clear();
    BOOST_CHECK(!SelectInvocationWorker("x86", workerPath, error));
    BOOST_CHECK(workerPath.empty());
    BOOST_CHECK(error.find("unavailable") != std::string::npos ||
        error.find("wrong architecture") != std::string::npos);
#else
    BOOST_CHECK(SelectInvocationWorker("x86", workerPath, error));
    BOOST_CHECK(!workerPath.empty());
    BOOST_CHECK(workerPath.find("FubiInvocationWorker_x86.exe") != std::string::npos);
    workerPath.clear();
    error.clear();
    BOOST_CHECK(!SelectInvocationWorker("x64", workerPath, error));
#endif
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

BOOST_AUTO_TEST_CASE(NormalizedX64FrameRejectsInvalidAdapterInputs)
{
    NativeCallFrameX64 frame;
    uintptr_t returned = 0;
    std::string error;

    BOOST_CHECK(!InvokeNativeCallX64(frame, returned, error));
    BOOST_CHECK_EQUAL(error, "x64 native adapter requires a target address");

    frame.targetAddress = 1;
    frame.argumentCount = 9;
    error.clear();
    BOOST_CHECK(!InvokeNativeCallX64(frame, returned, error));
    BOOST_CHECK_EQUAL(error, "x64 native adapter supports at most eight arguments");
}

BOOST_AUTO_TEST_CASE(SessionActionsRoundTripWithoutCallSelectors)
{
    CallRequest hello;
    std::vector<CallDiagnostic> diagnostics;
    BOOST_REQUIRE(ParseCallRequestJson(
        "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"hello-1\"}",
        hello, diagnostics));
    BOOST_CHECK_EQUAL(hello.action, "hello");
    BOOST_CHECK(hello.selector.empty());

    CallResult response;
    response.action = "list";
    response.correlationId = "list-1";
    response.status = "completed";
    std::ostringstream encoded;
    WriteCallResultJson(encoded, response);
    CallResult decoded;
    BOOST_REQUIRE(ParseCallResultJson(encoded.str(), decoded, diagnostics));
    BOOST_CHECK_EQUAL(decoded.action, "list");
    BOOST_CHECK_EQUAL(decoded.correlationId, response.correlationId);

    CallRequest unknown;
    diagnostics.clear();
    BOOST_CHECK(!ParseCallRequestJson(
        "{\"schema_version\":1,\"action\":\"unknown\",\"correlation_id\":\"bad\"}",
        unknown, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(),
        [](const CallDiagnostic& item) { return item.code == "unsupported-action"; }));
}

BOOST_AUTO_TEST_CASE(ByteBufferContractsValidateHexAndBounds)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId = "bytes-1";
    request.selector = "NamedExport";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = catalog.Module().architecture == "x64" ? "x64" : "__cdecl";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    TypeSpec bytes;
    bytes.kind = TypeKind::Bytes;
    bytes.width = 8;
    bytes.pointerDepth = 1;
    request.prototypeOverride.parameters = {bytes};
    request.arguments = {{{bytes, "0011aaff"}}};
    request.arguments[0].bufferSize = 4;
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(ValidateCallRequest(request, catalog, diagnostics));

    request.arguments[0].value = "0g";
    diagnostics.clear();
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(),
        [](const CallDiagnostic& item) { return item.code == "invalid-argument-value"; }));

    request.arguments[0].value = "00";
    request.arguments[0].type.elementCount = UINT32_MAX;
    diagnostics.clear();
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(),
        [](const CallDiagnostic& item) { return item.code == "buffer-size-overflow"; }));
}

BOOST_AUTO_TEST_CASE(RejectsTruncatedAndOversizedRequests)
{
    CallRequest request;
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(!ParseCallRequestJson("{\"schema_version\":1,\"action\":\"call\"",
        request, diagnostics));
    BOOST_CHECK(!diagnostics.empty());

    diagnostics.clear();
    const std::string oversized(4 * 1024 * 1024 + 1, 'x');
    BOOST_CHECK(!ParseCallRequestJson(oversized, request, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "size-limit";
    }));
}

BOOST_AUTO_TEST_CASE(StringContractsRejectNestedPointersAndInvalidUtf8)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId = "string-1";
    request.selector = "NamedExport";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = catalog.Module().architecture == "x64" ? "x64" : "__cdecl";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    TypeSpec stringType;
    stringType.kind = TypeKind::String;
    stringType.width = 8;
    stringType.pointerDepth = 1;
    stringType.encoding = "utf8";
    request.prototypeOverride.parameters = {stringType};
    request.arguments = {{{stringType, "hello"}}};
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(ValidateCallRequest(request, catalog, diagnostics));
    request.arguments[0].value = "\xC3\x28";
    diagnostics.clear();
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    request.arguments[0].value = "hello";
    request.arguments[0].type.pointerDepth = 2;
    diagnostics.clear();
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
}

BOOST_AUTO_TEST_CASE(ExplicitCallOwnsStringAndByteStorage)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId = "storage-1";
    request.selector = "NamedExport";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    TypeSpec stringType;
    stringType.kind = TypeKind::String;
    stringType.width = 8;
    stringType.pointerDepth = 1;
    stringType.encoding = "utf8";
    request.prototypeOverride.parameters = {stringType};
    request.arguments = {{{stringType, "owned text"}}};
    CallResult result;
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), request, catalog, result, error), error);
    BOOST_CHECK_EQUAL(result.returnValue, "42");

    TypeSpec bytes;
    bytes.kind = TypeKind::Bytes;
    bytes.width = 8;
    bytes.pointerDepth = 1;
    request.prototypeOverride.parameters = {bytes};
    request.arguments = {{{bytes, "0011aaff"}}};
    request.arguments[0].bufferSize = 4;
    error.clear();
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), request, catalog, result, error), error);
    BOOST_CHECK_EQUAL(result.returnValue, "42");
#endif
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

BOOST_AUTO_TEST_CASE(X64InvocationCoversStackArgumentsAndScalarWidths)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));

    CallRequest request;
    request.correlationId = "invoke-matrix";
    request.selector = "SumEight";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32, true};
    request.prototypeOverride.parameters.assign(8, {TypeKind::Integer, 32, true});
    for (int value = 1; value <= 8; ++value)
        request.arguments.push_back({{TypeKind::Integer, 32, true}, std::to_string(value)});

    CallResult result;
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), request, catalog, result, error), error);
    BOOST_CHECK_EQUAL(result.returnValue, "36");

    struct WidthCase
    {
        const char* selector;
        uint16_t width;
        const char* expected;
    };
    const WidthCase cases[] = {
        {"ReturnByte", 8, "165"},
        {"ReturnWord", 16, "48879"},
        {"ReturnDword", 32, "3735928559"},
        {"ReturnQword", 64, "18364758544493064720"},
    };
    for (const WidthCase& item : cases)
    {
        CallRequest scalar;
        scalar.correlationId = std::string("scalar-") + item.selector;
        scalar.selector = item.selector;
        scalar.hasPrototypeOverride = true;
        scalar.prototypeOverride.quality = PrototypeQuality::UserDeclared;
        scalar.prototypeOverride.abi = "x64";
        scalar.prototypeOverride.returnType = {TypeKind::Integer, item.width, false};

        CallResult scalarResult;
        BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), scalar, catalog, scalarResult, error), error);
        BOOST_CHECK_EQUAL(scalarResult.returnValue, item.expected);
    }
#else
    BOOST_TEST_MESSAGE("x64 invocation matrix skipped for non-x64 build");
#endif
}

BOOST_AUTO_TEST_CASE(X64InvocationRejectsUnsupportedAggregateBeforeLoad)
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
    request.prototypeOverride.parameters = {{TypeKind::Structure, 64}, {TypeKind::Integer, 32}};
    request.arguments = {{{TypeKind::Structure, 64}, "1"}, {{TypeKind::Integer, 32}, "2"}};
    CallResult result;
    BOOST_CHECK(!InvokeX64Export(FixturePath(), request, catalog, result, error));
    BOOST_CHECK_EQUAL(result.status, "validation-failed");
    BOOST_CHECK(!result.diagnostics.empty());
#endif
}

BOOST_AUTO_TEST_CASE(X64InvocationCapturesFloatingPointArgumentsAndReturns)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId = "float-1";
    request.selector = "AddFloats";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Floating, 32};
    request.prototypeOverride.parameters = {{TypeKind::Floating, 32}, {TypeKind::Floating, 32}};
    request.arguments = {{{TypeKind::Floating, 32}, "1.25"}, {{TypeKind::Floating, 32}, "2.5"}};
    CallResult result;
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), request, catalog, result, error), error);
    BOOST_CHECK_EQUAL(result.status, "completed");
    BOOST_CHECK_EQUAL(result.returnValue, "3.75");

    request.selector = "MultiplyDoubles";
    request.prototypeOverride.returnType = {TypeKind::Floating, 64};
    request.prototypeOverride.parameters = {{TypeKind::Floating, 64}, {TypeKind::Floating, 64}};
    request.arguments = {{{TypeKind::Floating, 64}, "1.5"}, {{TypeKind::Floating, 64}, "4"}};
    error.clear();
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), request, catalog, result, error), error);
    BOOST_CHECK_EQUAL(result.returnValue, "6");
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

BOOST_AUTO_TEST_CASE(RejectsInvalidOpaquePointerArguments)
{
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    const TypeSpec pointer{TypeKind::Pointer, 64, false, 1};
    CallRequest request;
    request.correlationId = "invalid-pointer";
    request.selector = "NamedExport";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = catalog.Module().architecture == "x64" ? "x64" : "__cdecl";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32};
    request.prototypeOverride.parameters = {pointer};
    request.arguments = {{{pointer, "0x1234"}}};
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "invalid-argument-value";
    }));
}

BOOST_AUTO_TEST_CASE(IsolatedInvocationRejectsPointerResultsBeforeWorkerLaunch)
{
    FunctionCatalog catalog;
    std::string error;
    const std::string fixture = FixturePath();
    const std::string marker = "export_fixture.executed";
    DeleteFileA(marker.c_str());
    BOOST_REQUIRE(FunctionCatalog::Load(fixture, catalog, error));
    CallRequest request;
    request.correlationId = "pointer-result";
    request.selector = "PointerEcho";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = catalog.Module().architecture == "x64" ? "x64" : "__cdecl";
    request.prototypeOverride.returnType = {TypeKind::Pointer, 64, false, 1};
    request.prototypeOverride.parameters = {{TypeKind::Pointer, 64, false, 1}};
    request.arguments = {{{TypeKind::Pointer, 64, false, 1}, "opaque:0x1"}};
    CallResult result;
    BOOST_CHECK(!InvokeX64ExportProcess(fixture, request, catalog, result, error));
    BOOST_CHECK_EQUAL(result.status, "validation-failed");
    BOOST_CHECK(result.returnValue.empty());
    BOOST_CHECK(result.outputValues.empty());
    BOOST_REQUIRE_EQUAL(result.diagnostics.size(), 1U);
    BOOST_CHECK_EQUAL(result.diagnostics.front().code, "pointer-result-unsupported");
    BOOST_CHECK_EQUAL(GetFileAttributesA(marker.c_str()), INVALID_FILE_ATTRIBUTES);
}

BOOST_AUTO_TEST_CASE(ProcessWorkerReturnsStructuredResult)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId="worker-1";
    request.selector="?AddNumbers@@YAHHH@Z";
    request.hasPrototypeOverride=true;
    request.prototypeOverride.quality=PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi="x64";
    request.prototypeOverride.returnType={TypeKind::Integer,32};
    request.prototypeOverride.parameters={{TypeKind::Integer,32},{TypeKind::Integer,32}};
    request.arguments={{{TypeKind::Integer,32},"4"},{{TypeKind::Integer,32},"5"}};
    CallResult result;
    BOOST_REQUIRE_MESSAGE(InvokeX64ExportProcess(FixturePath(),request,catalog,result,error),error);
    BOOST_CHECK_EQUAL(result.status,"completed");
    BOOST_CHECK_EQUAL(result.returnValue,"9");
#endif
}

BOOST_AUTO_TEST_CASE(ProcessWorkerReportsCrashExit)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId = "worker-crash";
    request.selector = "CrashProcess";
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32, true};
    CallResult result;
    BOOST_CHECK(!InvokeX64ExportProcess(FixturePath(), request, catalog, result, error));
    BOOST_CHECK_EQUAL(result.status, "worker-crashed");
    BOOST_CHECK(result.hasWorkerExitCode);
    BOOST_CHECK_NE(result.workerExitCode, 0U);
    BOOST_CHECK(std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const CallDiagnostic& item) { return item.code == "worker-crashed"; }));
#endif
}

BOOST_AUTO_TEST_CASE(ProcessWorkerReportsTimeoutAndExit)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    CallRequest request;
    request.correlationId = "worker-timeout";
    request.selector = "HangProcess";
    request.timeoutMs = 100;
    request.hasPrototypeOverride = true;
    request.prototypeOverride.quality = PrototypeQuality::UserDeclared;
    request.prototypeOverride.abi = "x64";
    request.prototypeOverride.returnType = {TypeKind::Integer, 32, true};
    CallResult result;
    BOOST_CHECK(!InvokeX64ExportProcess(FixturePath(), request, catalog, result, error));
    BOOST_CHECK_EQUAL(result.status, "timed-out");
    BOOST_CHECK(result.hasWorkerExitCode);
    BOOST_CHECK_EQUAL(result.workerExitCode, static_cast<uint32_t>(ERROR_TIMEOUT));
#endif
}
