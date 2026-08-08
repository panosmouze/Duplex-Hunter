#include "FileMatch.hpp"
#include "FileSizeFilter.hpp"
#include "FileHashFilter.hpp"

void to_json(nlohmann::json& j, const FileMatchGroup& group) {
    j = nlohmann::json{
        {"size", group.size},
        {"hash", group.hash.has_value() ? nlohmann::json(*group.hash) : nlohmann::json(nullptr)},
        {"files", group.files}
    };
}

FileMatch::FileMatch(FileCollector& collector, bool fastHash)
    : collector(collector)
{
    sizeFilter = std::make_unique<FileSizeFilter>();
    hashFilter = std::make_unique<FileHashFilter>(fastHash);
}

FileMatch::~FileMatch() {

}

void FileMatch::setProgressCallback(std::function<void(const std::string& path, double progress)> cb) {
    progressCallback = std::move(cb);
}

void FileMatch::run() {
    match();
}

void FileMatch::match() {
    std::vector<FileEntry*> allEntries;
    for (auto it = collector.getBeginIterator(); it != collector.getEndIterator(); ++it) {
        allEntries.push_back(it->get());
    }

    std::vector<FileEntry*> hashCandidates;
    for (auto& group : sizeFilter->apply(allEntries)) {
        if (group.size() == 1) {
            FileEntry* entry = group.front();
            uniqueGroups.push_back({ entry->getSize(), std::nullopt, { entry->getPath().string() } });
        } else {
            hashCandidates.insert(hashCandidates.end(), group.begin(), group.end());
        }
    }

    std::size_t total = hashCandidates.size();
    std::size_t processed = 0;
    auto* hashFilterImpl = static_cast<FileHashFilter*>(hashFilter.get());
    hashFilterImpl->setProgressCallback([this, &processed, total](FileEntry& entry) {
        ++processed;
        if (progressCallback && total > 0) {
            progressCallback(entry.getPath().string(), static_cast<double>(processed) / total);
        }
    });

    for (auto& group : hashFilter->apply(hashCandidates)) {
        std::vector<std::string> paths;
        paths.reserve(group.size());
        for (FileEntry* entry : group) paths.push_back(entry->getPath().string());

        uint64_t hash = group.front()->getHash();
        uintmax_t size = group.front()->getSize();
        hashToPath[hash] = paths;

        FileMatchGroup matchGroup{ size, hash, paths };
        if (group.size() == 1) {
            uniqueGroups.push_back(std::move(matchGroup));
        } else {
            duplicateGroups.push_back(std::move(matchGroup));
        }
    }
}

std::vector<FileMatchGroup> FileMatch::getDuplicates() {
    return duplicateGroups;
}

std::vector<FileMatchGroup> FileMatch::getUnique() {
    return uniqueGroups;
}

std::vector<std::string> FileMatch::hashToFiles(uint64_t hash) {
    auto it = hashToPath.find(hash);
    if (it == hashToPath.end()) return {};
    return it->second;
}
