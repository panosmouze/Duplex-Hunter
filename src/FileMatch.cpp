#include <FileMatch.hpp>


FileMatch::FileMatch(const std::vector<std::shared_ptr<FileScanner>>& scanners) {
    this->scanners = scanners;
}

FileMatch::~FileMatch() {

}

void FileMatch::run() {
    match();
    analyze();
}

void FileMatch::match() {
    for (auto scanner : scanners) {
        for (auto it_file = scanner->getBeginIterator(); it_file != scanner->getEndIterator(); ++it_file) {
            hashToPath[(*it_file)->getHash()].push_back(it_file->get()->getPath());
        }
    }
}

void FileMatch::analyze() {
    for (auto it = hashToPath.begin(); it != hashToPath.end(); ++it) {
        if (it->second.size() == 1) {
            uniqueHash.push_back({it->first, it->second});
        } else {
            duplicateHash.push_back({it->first, it->second});
        }
    }
}

std::vector<std::pair<uint64_t, std::vector<std::string>>> FileMatch::getDuplicates() {
    return duplicateHash;
}

std::vector<std::pair<uint64_t, std::vector<std::string>>> FileMatch::getUnique() {
    return uniqueHash;
}
