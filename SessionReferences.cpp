#include "stdafx.h"
#include "SessionReferences.h"

#include <limits>
#include <utility>

namespace
{
const std::string kPrefix = "opaque:session-";
}

std::string SessionReferences::Issue(uint64_t value)
{
    // A monotonically increasing identifier is deterministic for a session
    // and cannot expose the underlying pointer or handle value.
    if (nextId_ == 0)
        return {};
    const std::string reference = kPrefix + std::to_string(nextId_++);
    values_.emplace(reference, Entry{value, false, {}});
    return reference;
}

std::string SessionReferences::IssueHandle(uint64_t value, HandleIdentity identity)
{
    if (nextId_ == 0 || (identity.width != 32 && identity.width != 64) ||
        (identity.ownership != "borrowed" && identity.ownership != "owned")) return {};
    const std::string reference = kPrefix + std::to_string(nextId_++);
    values_.emplace(reference, Entry{value, true, std::move(identity)});
    return reference;
}

bool SessionReferences::IsWellFormed(const std::string& reference)
{
    if (reference.size() <= kPrefix.size() || reference.compare(0, kPrefix.size(), kPrefix) != 0)
        return false;
    const std::string digits = reference.substr(kPrefix.size());
    if (digits.empty() || (digits.size() > 1 && digits.front() == '0'))
        return false;
    uint64_t value = 0;
    for (const char digit : digits)
    {
        if (digit < '0' || digit > '9')
            return false;
        const uint64_t number = static_cast<uint64_t>(digit - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - number) / 10)
            return false;
        value = value * 10 + number;
    }
    return value != 0;
}

bool SessionReferences::Resolve(const std::string& reference, uint64_t& value) const
{
    if (!IsWellFormed(reference))
        return false;
    const auto found = values_.find(reference);
    if (found == values_.end())
        return false;
    if (found->second.handle) return false;
    value = found->second.value;
    return true;
}

bool SessionReferences::ResolveHandle(const std::string& reference, uint64_t& value,
    const std::string& moduleSha256, const std::string& architecture) const
{
    if (!IsWellFormed(reference)) return false;
    const auto found = values_.find(reference);
    if (found == values_.end() || !found->second.handle ||
        found->second.identity.moduleSha256 != moduleSha256 ||
        found->second.identity.architecture != architecture) return false;
    value = found->second.value;
    return true;
}

bool SessionReferences::Release(const std::string& reference)
{
    if (!IsWellFormed(reference))
        return false;
    const auto found = values_.find(reference);
    if (found == values_.end() || found->second.handle) return false;
    values_.erase(found);
    return true;
}

bool SessionReferences::ReleaseHandle(const std::string& reference,
    const std::string& moduleSha256, const std::string& architecture)
{
    if (!IsWellFormed(reference)) return false;
    const auto found = values_.find(reference);
    if (found == values_.end() || !found->second.handle ||
        found->second.identity.moduleSha256 != moduleSha256 ||
        found->second.identity.architecture != architecture ||
        !found->second.identity.release) return false;
    if (!found->second.identity.release(found->second.value)) return false;
    values_.erase(found);
    return true;
}
