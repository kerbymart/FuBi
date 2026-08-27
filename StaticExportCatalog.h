#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

struct StaticExport
{
    uint32_t ordinal = 0;
    uint32_t rva = 0;
    std::vector<std::string> names;
    std::string forwarder;
};

class StaticExportCatalog
{
public:
    static bool Load(
        const std::string& path, StaticExportCatalog& catalog, std::string& error);

    const std::string& ModulePath() const { return modulePath_; }
    const std::vector<StaticExport>& Exports() const { return exports_; }
    void WriteText(std::ostream& output) const;

private:
    std::string modulePath_;
    std::vector<StaticExport> exports_;
};
