#include "stdafx.h"

#include "StaticExportCatalog.h"

#include "PEImage.h"

#include <iomanip>
#include <ostream>
#include <unordered_map>

namespace
{
const PeDataDirectory* FindExportDirectory(const PEImage& image)
{
    for (const PeDataDirectory& directory : image.Headers().dataDirectories)
    {
        if (directory.index == IMAGE_DIRECTORY_ENTRY_EXPORT && directory.rva != 0 &&
            directory.size >= sizeof(IMAGE_EXPORT_DIRECTORY))
        {
            return &directory;
        }
    }
    return nullptr;
}

std::string DisplayName(const StaticExport& item)
{
    return item.names.empty() ? "#" + std::to_string(item.ordinal) : item.names.front();
}
}

bool StaticExportCatalog::Load(
    const std::string& path, StaticExportCatalog& catalog, std::string& error)
{
    PEImage image;
    if (!PEImage::Load(path, image, error)) return false;

    StaticExportCatalog candidate;
    candidate.modulePath_ = image.SourceName();

    const PeDataDirectory* directory = FindExportDirectory(image);
    if (!directory)
    {
        catalog = std::move(candidate);
        return true;
    }

    IMAGE_EXPORT_DIRECTORY table = {};
    if (!image.ReadRva(directory->rva, table))
    {
        error = "Invalid export directory";
        return false;
    }
    if (table.NumberOfFunctions > 1'000'000 || table.NumberOfNames > 1'000'000)
    {
        error = "Export table exceeds safety limit";
        return false;
    }

    std::unordered_map<uint32_t, std::vector<std::string>> namesByIndex;
    for (uint32_t index = 0; index < table.NumberOfNames; ++index)
    {
        const uint64_t nameEntryRva = static_cast<uint64_t>(table.AddressOfNames) +
            static_cast<uint64_t>(index) * sizeof(uint32_t);
        const uint64_t ordinalEntryRva = static_cast<uint64_t>(table.AddressOfNameOrdinals) +
            static_cast<uint64_t>(index) * sizeof(uint16_t);
        uint32_t nameRva = 0;
        uint16_t functionIndex = 0;
        if (nameEntryRva > UINT32_MAX || ordinalEntryRva > UINT32_MAX ||
            !image.ReadRva(static_cast<uint32_t>(nameEntryRva), nameRva) ||
            !image.ReadRva(static_cast<uint32_t>(ordinalEntryRva), functionIndex))
        {
            error = "Truncated export name table";
            return false;
        }
        if (functionIndex >= table.NumberOfFunctions) continue;

        std::string name;
        if (!image.ReadCStringAtRva(nameRva, name))
        {
            error = "Invalid export name";
            return false;
        }
        namesByIndex[functionIndex].push_back(std::move(name));
    }

    const uint64_t exportEnd = static_cast<uint64_t>(directory->rva) + directory->size;
    if (exportEnd > UINT32_MAX + uint64_t{1})
    {
        error = "Export directory range overflow";
        return false;
    }

    candidate.exports_.reserve(table.NumberOfFunctions);
    for (uint32_t index = 0; index < table.NumberOfFunctions; ++index)
    {
        const uint64_t functionEntryRva = static_cast<uint64_t>(table.AddressOfFunctions) +
            static_cast<uint64_t>(index) * sizeof(uint32_t);
        uint32_t functionRva = 0;
        if (functionEntryRva > UINT32_MAX ||
            !image.ReadRva(static_cast<uint32_t>(functionEntryRva), functionRva))
        {
            error = "Truncated export address table";
            return false;
        }
        if (functionRva == 0) continue;

        StaticExport item;
        item.ordinal = table.Base + index;
        item.rva = functionRva;
        item.names = std::move(namesByIndex[index]);
        if (functionRva >= directory->rva && static_cast<uint64_t>(functionRva) < exportEnd &&
            !image.ReadCStringAtRva(functionRva, item.forwarder))
        {
            error = "Invalid export forwarder";
            return false;
        }
        candidate.exports_.push_back(std::move(item));
    }

    catalog = std::move(candidate);
    return true;
}

void StaticExportCatalog::WriteText(std::ostream& output) const
{
    output << "FuBi static export catalog\n"
           << "module = " << modulePath_ << "\n"
           << "export_count = " << exports_.size() << "\n";

    for (const StaticExport& item : exports_)
    {
        output << "\n[export]\n"
               << "ordinal = " << item.ordinal << "\n"
               << "rva = 0x" << std::hex << std::uppercase << item.rva
               << std::dec << std::nouppercase << "\n"
               << "name = " << DisplayName(item) << "\n"
               << "aliases = ";
        if (item.names.empty())
        {
            output << "<ordinal-only>";
        }
        else
        {
            for (size_t index = 0; index < item.names.size(); ++index)
            {
                if (index != 0) output << ", ";
                output << item.names[index];
            }
        }
        output << "\nforwarder = "
               << (item.forwarder.empty() ? "<none>" : item.forwarder) << "\n";
    }
}
