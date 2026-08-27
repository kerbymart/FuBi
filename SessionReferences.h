#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

// Owns opaque values for one JSONL session. The stored value is only an
// adapter-owned token payload; this class never dereferences or frees it.
class SessionReferences
{
public:
    std::string Issue(uint64_t value);
    bool Resolve(const std::string& reference, uint64_t& value) const;
    bool Release(const std::string& reference);
    static bool IsWellFormed(const std::string& reference);

private:
    std::unordered_map<std::string, uint64_t> values_;
    uint64_t nextId_ = 1;
};
