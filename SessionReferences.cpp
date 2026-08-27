#include "stdafx.h"
#include "SessionReferences.h"

#include <limits>

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
    values_.emplace(reference, value);
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
    value = found->second;
    return true;
}

bool SessionReferences::Release(const std::string& reference)
{
    if (!IsWellFormed(reference))
        return false;
    return values_.erase(reference) == 1;
}
