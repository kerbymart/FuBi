#define BOOST_TEST_MODULE MixedFloatingTests
#include <boost/test/included/unit_test.hpp>

#include "CallContract.h"
#include "FunctionCatalog.h"
#include "InvocationEngine.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace
{
std::string FixturePath()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string value(path);
    return value.substr(0, value.find_last_of("\\/") + 1) + "static_fixture.dll";
}

CallRequest Request(const char* selector, const PrototypeSpec& prototype,
    std::initializer_list<CallArgument> arguments)
{
    CallRequest request;
    request.correlationId = selector;
    request.selector = selector;
    request.hasPrototypeOverride = true;
    request.prototypeOverride = prototype;
    request.arguments.assign(arguments.begin(), arguments.end());
    return request;
}

TypeSpec FloatType(uint16_t width)
{
    return {TypeKind::Floating, width};
}

TypeSpec IntegerType()
{
    return {TypeKind::Integer, 32, true};
}

PrototypeSpec Prototype(TypeSpec returnType, std::initializer_list<TypeSpec> parameters)
{
    PrototypeSpec prototype;
    prototype.quality = PrototypeQuality::UserDeclared;
    prototype.abi = "x64";
    prototype.returnType = returnType;
    prototype.parameters = parameters;
    return prototype;
}
}

BOOST_AUTO_TEST_CASE(MixedIntegerAndFloatingArgumentsUsePositionalRegisters)
{
#if defined(_M_X64)
    FunctionCatalog catalog;
    std::string error;
    DeleteFileA("static_fixture.executed");
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    BOOST_CHECK_EQUAL(GetFileAttributesA("static_fixture.executed"), INVALID_FILE_ATTRIBUTES);
    const PrototypeSpec prototype = Prototype(FloatType(64),
        {IntegerType(), FloatType(32), IntegerType(), FloatType(64)});
    CallRequest request = Request("MixedIntFloat", prototype,
        {CallArgument{IntegerType(), "1"}, CallArgument{FloatType(32), "2.5"},
         CallArgument{IntegerType(), "3"}, CallArgument{FloatType(64), "4.5"}});
    CallResult result;
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), request, catalog, result, error), error);
    BOOST_CHECK(result.success);
    BOOST_CHECK_EQUAL(result.returnValue, "11");

    const PrototypeSpec reverse = Prototype(IntegerType(),
        {FloatType(32), IntegerType(), FloatType(32)});
    request = Request("MixedFloatInt", reverse,
        {CallArgument{FloatType(32), "1.5"}, CallArgument{IntegerType(), "7"},
         CallArgument{FloatType(32), "2.5"}});
    error.clear();
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), request, catalog, result, error), error);
    BOOST_CHECK(result.success);
    BOOST_CHECK_EQUAL(result.returnValue, "47");

    int pointerValue = 10;
    std::ostringstream pointerText;
    pointerText << "opaque:0x" << std::hex << reinterpret_cast<uintptr_t>(&pointerValue);
    const TypeSpec pointer{TypeKind::Pointer, 64, false, 1};
    const PrototypeSpec pointerPrototype = Prototype(FloatType(64),
        {pointer, FloatType(32), IntegerType(), FloatType(64), IntegerType()});
    request = Request("MixedPointerFloat", pointerPrototype,
        {CallArgument{pointer, pointerText.str()}, CallArgument{FloatType(32), "1.5"},
         CallArgument{IntegerType(), "2"}, CallArgument{FloatType(64), "3.25"},
         CallArgument{IntegerType(), "4"}});
    error.clear();
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(FixturePath(), request, catalog, result, error), error);
    BOOST_CHECK_EQUAL(result.returnValue, "20.75");

    std::ostringstream requestJson;
    WriteCallRequestJson(requestJson, request);
    CallRequest parsedRequest;
    std::vector<CallDiagnostic> diagnostics;
    BOOST_REQUIRE(ParseCallRequestJson(requestJson.str(), parsedRequest, diagnostics));
    BOOST_CHECK_EQUAL(parsedRequest.arguments.size(), request.arguments.size());
    BOOST_CHECK_EQUAL(parsedRequest.arguments[1].type.width, 32);
    BOOST_CHECK_EQUAL(parsedRequest.arguments[3].type.width, 64);

    std::ostringstream resultJson;
    WriteCallResultJson(resultJson, result);
    CallResult parsedResult;
    BOOST_REQUIRE(ParseCallResultJson(resultJson.str(), parsedResult, diagnostics));
    BOOST_CHECK_EQUAL(parsedResult.returnValue, result.returnValue);
#else
    BOOST_TEST_MESSAGE("mixed floating ABI test skipped for non-x64 build");
#endif
}

BOOST_AUTO_TEST_CASE(MixedFloatingRequestsRejectNonFiniteAndAggregateTypes)
{
    const TypeSpec floatType = FloatType(32);
    const TypeSpec integerType = IntegerType();
    const PrototypeSpec prototype = Prototype(IntegerType(), {floatType});
    CallRequest request = Request("MixedFloatInt", prototype, {CallArgument{floatType, "nan"}});
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(FixturePath(), catalog, error));
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "invalid-argument-value";
    }));

    TypeSpec aggregate{TypeKind::Structure, 64};
    request = Request("MixedFloatInt", Prototype(IntegerType(), {aggregate}),
        {CallArgument{aggregate, "0"}});
    diagnostics.clear();
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "invalid-argument-value";
    }));
}
