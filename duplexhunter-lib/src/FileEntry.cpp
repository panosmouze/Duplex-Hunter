#include "FileEntry.hpp"

#include <xxhash.h>
#include <fstream>

FileEntry::FileEntry(const fs::path& path, uintmax_t size) {
    this->path = path;
    this->size = size;
    this->hash = 0;
}

FileEntry::~FileEntry() {

}

void FileEntry::calcHash(bool fastHash) {
    this->hash = hashFile(path, fastHash);
}

uint64_t FileEntry::getHash() {
    return hash;
}

fs::path FileEntry::getPath() {
    return path;
}

uintmax_t FileEntry::getSize() {
    return size;
}

uint64_t FileEntry::hashFile(const fs::path& filepath, bool fastHash) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return 0;

    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);

    constexpr size_t chunkSize = 4096;
    char buffer[chunkSize];
    auto fileSize = fs::file_size(filepath);

    if (fastHash && fileSize > 2 * chunkSize) {
        std::streampos positions[] = {
            0,
            static_cast<std::streampos>(fileSize / 2),
            static_cast<std::streampos>(fileSize - chunkSize)
        };
        for (auto pos : positions) {
            file.seekg(pos);
            file.read(buffer, chunkSize);
            XXH64_update(state, buffer, file.gcount());
        }
    } else {
        while (file.read(buffer, sizeof(buffer))) {
            XXH64_update(state, buffer, file.gcount());
        }
        if (file.gcount() > 0)
            XXH64_update(state, buffer, file.gcount());

        XXH64_update(state, &fileSize, sizeof(fileSize));
    }

    uint64_t hash = XXH64_digest(state);
    XXH64_freeState(state);

    return hash;
}