#include "FileSizeFilter.hpp"

#include <unordered_map>

std::vector<std::vector<FileEntry*>> FileSizeFilter::apply(const std::vector<FileEntry*>& candidates) {
    std::unordered_map<uintmax_t, std::vector<FileEntry*>> sizeToEntries;

    for (FileEntry* entry : candidates) {
        sizeToEntries[entry->getSize()].push_back(entry);
    }

    std::vector<std::vector<FileEntry*>> groups;
    groups.reserve(sizeToEntries.size());
    for (auto& [size, entries] : sizeToEntries) {
        groups.push_back(std::move(entries));
    }

    return groups;
}
