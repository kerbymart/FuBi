#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

// Owns opaque values for one JSONL session. The stored value is only an
// adapter-owned token payload; this class never dereferences or frees it.
class SessionReferences
{
public:
    using HandleReleaseAdapter = std::function<bool(uint64_t)>;
    struct HandleIdentity
    {
        uint16_t width = 0;
        std::string moduleSha256;
        std::string architecture;
        std::string ownership;
        HandleReleaseAdapter release;
    };
    std::string Issue(uint64_t value);
    std::string IssueHandle(uint64_t value, HandleIdentity identity);
    bool Resolve(const std::string& reference, uint64_t& value) const;
    bool ResolveHandle(const std::string& reference, uint64_t& value,
        const std::string& moduleSha256, const std::string& architecture) const;
    bool Release(const std::string& reference);
    bool ReleaseHandle(const std::string& reference, const std::string& moduleSha256,
        const std::string& architecture);
    static bool IsWellFormed(const std::string& reference);

private:
    struct Entry { uint64_t value = 0; bool handle = false; HandleIdentity identity; };
    std::unordered_map<std::string, Entry> values_;
    uint64_t nextId_ = 1;
};
