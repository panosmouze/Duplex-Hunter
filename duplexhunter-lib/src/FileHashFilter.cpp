#include "FileHashFilter.hpp"

#include <unordered_map>

FileHashFilter::FileHashFilter(bool fastHash) : fastHash(fastHash) {

}

void FileHashFilter::setProgressCallback(std::function<void(FileEntry&)> cb) {
    progressCallback = std::move(cb);
}

std::vector<std::vector<FileEntry*>> FileHashFilter::apply(const std::vector<FileEntry*>& candidates) {
    std::unordered_map<uint64_t, std::vector<FileEntry*>> hashToEntries;

    for (FileEntry* entry : candidates) {
        entry->calcHash(fastHash);
        hashToEntries[entry->getHash()].push_back(entry);
        if (progressCallback) progressCallback(*entry);
    }

    std::vector<std::vector<FileEntry*>> groups;
    groups.reserve(hashToEntries.size());
    for (auto& [hash, entries] : hashToEntries) {
        groups.push_back(std::move(entries));
    }

    return groups;
}
