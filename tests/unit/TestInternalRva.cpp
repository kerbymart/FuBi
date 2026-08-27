#define BOOST_TEST_MODULE InternalRvaTests
#include <boost/test/included/unit_test.hpp>

#include "CallContract.h"
#include "FunctionCatalog.h"
#include "InvocationEngine.h"
#include "PEImage.h"
#include "PrototypeProfile.h"

#include <windows.h>
#include <algorithm>
#include <cstring>
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

std::string RvaSelector(uint32_t rva)
{
    std::ostringstream value;
    value << "0x" << std::hex << rva;
    return value.str();
}

bool FindInternalRva(const std::string& path, const FunctionCatalog& catalog,
    uint32_t& rva, std::string& error)
{
    PEImage image;
    if (!PEImage::Load(path, image, error)) return false;
    const FunctionRecord* caller = catalog.Find("CallInternalAdd");
    if (caller == nullptr) { error = "internal fixture caller export was not found"; return false; }
    const auto fileOffset = image.RvaToFileOffset(caller->startRva);
    const PeSection* section = image.FindSection(caller->startRva);
    if (!fileOffset || section == nullptr || !section->executable)
    {
        error = "internal fixture caller is not executable";
        return false;
    }
    const size_t sectionEnd = std::min<size_t>(section->rawOffset + section->rawSize,
        image.Bytes().size());
    const size_t callerEnd = caller->endRva > caller->startRva
        ? std::min<size_t>(section->rawOffset + (caller->endRva - section->rva), sectionEnd)
        : std::min<size_t>(*fileOffset + 64, sectionEnd);
    for (size_t offset = *fileOffset; offset + 5 <= callerEnd; ++offset)
    {
        if (image.Bytes()[offset] != 0xE8 && image.Bytes()[offset] != 0xE9) continue;
        int32_t displacement = 0;
        std::memcpy(&displacement, image.Bytes().data() + offset + 1, sizeof(displacement));
        const int64_t target = static_cast<int64_t>(section->rva) +
            static_cast<int64_t>(offset - section->rawOffset) + 5 + displacement;
        if (target < 0 || target > UINT32_MAX) continue;
        const FunctionRecord* candidate = catalog.Find(RvaSelector(static_cast<uint32_t>(target)));
        if (candidate != nullptr && candidate->exportNames.empty() && candidate->executable)
        {
            rva = candidate->startRva;
            return true;
        }
    }
    error = "internal fixture direct-call target was not found";
    return false;
}

CallRequest RequestFor(const FunctionCatalog& catalog, uint32_t rva)
{
    CallRequest request;
    request.correlationId = "internal-rva";
    request.selector = RvaSelector(rva);
    request.allowInternal = true;
    request.authorizationProvenance = "profile:" + catalog.Module().sha256;
    request.moduleSha256 = catalog.Module().sha256;
    request.modulePath = catalog.Module().canonicalPath;
    request.moduleTimestamp = catalog.Module().timestamp;
    request.moduleImageSize = catalog.Module().imageSize;
    request.modulePreferredImageBase = catalog.Module().preferredImageBase;
    request.modulePdbGuid = catalog.Module().pdbGuid;
    request.modulePdbAge = catalog.Module().pdbAge;
    request.arguments = {{{TypeKind::Integer, 32, true}, "19"},
        {{TypeKind::Integer, 32, true}, "23"}};
    return request;
}
}

BOOST_AUTO_TEST_CASE(HashPinnedInternalRvaInvocationRequiresPolicyAndIdentity)
{
#if defined(_M_X64)
    const std::string fixture = FixturePath();
    DeleteFileA("static_fixture.executed");
    FunctionCatalog catalog;
    std::string error;
    BOOST_REQUIRE(FunctionCatalog::Load(fixture, catalog, error));
    BOOST_CHECK_EQUAL(GetFileAttributesA("static_fixture.executed"), INVALID_FILE_ATTRIBUTES);
    uint32_t rva = 0;
    BOOST_REQUIRE_MESSAGE(FindInternalRva(fixture, catalog, rva, error), error);
    const FunctionRecord* record = catalog.Find(RvaSelector(rva));
    BOOST_REQUIRE(record != nullptr);
    BOOST_CHECK(record->exportNames.empty());
    BOOST_CHECK(record->executable);

    PrototypeProfile profile;
    profile.schemaVersion = 1;
    profile.module = catalog.Module();
    ProfileFunction declaration;
    declaration.rva = rva;
    declaration.prototype.abi = "x64";
    declaration.prototype.returnType = {TypeKind::Integer, 32, true};
    declaration.prototype.parameters = {{TypeKind::Integer, 32, true},
        {TypeKind::Integer, 32, true}};
    profile.functions.push_back(declaration);
    std::vector<ProfileValidationError> profileErrors;
    BOOST_REQUIRE(catalog.ApplyProfile(profile, profileErrors));

    CallRequest request = RequestFor(catalog, rva);
    CallResult result;
    BOOST_REQUIRE_MESSAGE(InvokeX64Export(fixture, request, catalog, result, error), error);
    BOOST_CHECK(result.success);
    BOOST_CHECK_EQUAL(result.returnValue, "42");

    request.allowInternal = false;
    std::vector<CallDiagnostic> diagnostics;
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "internal-policy-required";
    }));

    request.allowInternal = true;
    request.moduleSha256.assign(catalog.Module().sha256.size(), '0');
    diagnostics.clear();
    BOOST_CHECK(!ValidateCallRequest(request, catalog, diagnostics));
    BOOST_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "module-identity-required";
    }));

    PrototypeProfile duplicateProfile = profile;
    duplicateProfile.functions.push_back(declaration);
    profileErrors.clear();
    BOOST_CHECK(!ValidatePrototypeProfile(duplicateProfile, catalog, profileErrors));
    BOOST_CHECK(std::any_of(profileErrors.begin(), profileErrors.end(), [](const auto& item) {
        return item.code == "duplicate-selector";
    }));

    PrototypeProfile ambiguousProfile = profile;
    ambiguousProfile.functions.front().selector = "CallInternalAdd";
    profileErrors.clear();
    BOOST_CHECK(!ValidatePrototypeProfile(ambiguousProfile, catalog, profileErrors));
    BOOST_CHECK(std::any_of(profileErrors.begin(), profileErrors.end(), [](const auto& item) {
        return item.code == "ambiguous-selector";
    }));

    const auto nonExecutable = std::find_if(catalog.Functions().begin(), catalog.Functions().end(),
        [](const auto& item) { return !item.executable && item.forwarder.empty(); });
    BOOST_REQUIRE(nonExecutable != catalog.Functions().end());
    PrototypeProfile nonExecutableProfile = profile;
    nonExecutableProfile.functions.front().rva = nonExecutable->startRva;
    profileErrors.clear();
    BOOST_CHECK(!ValidatePrototypeProfile(nonExecutableProfile, catalog, profileErrors));
    BOOST_CHECK(std::any_of(profileErrors.begin(), profileErrors.end(), [](const auto& item) {
        return item.code == "non-executable-rva";
    }));
#else
    BOOST_TEST_MESSAGE("internal RVA test skipped for non-x64 build");
#endif
}
