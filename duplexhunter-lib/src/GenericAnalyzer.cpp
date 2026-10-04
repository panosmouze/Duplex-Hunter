#include "GenericAnalyzer.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

// At the basic level, entropy is estimated from evenly spaced samples of the file.
static constexpr std::size_t SAMPLE_COUNT = 4;
static constexpr std::size_t SAMPLE_SIZE = 256 * 1024;

std::string GenericAnalyzer::name() const {
    return "generic";
}

bool GenericAnalyzer::canHandle(const FileType&) const {
    return true;
}

nlohmann::json GenericAnalyzer::analyze(const fs::path& path, const FileType&, AnalysisLevel level) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return { {"error", "Failed to open file"} };
    }

    std::array<uint64_t, 256> histogram{};
    uint64_t total = 0;
    std::vector<char> buffer(64 * 1024);

    auto consume = [&](std::size_t limit) {
        std::size_t remaining = limit;
        while (remaining > 0 && file) {
            file.read(buffer.data(), std::min(buffer.size(), remaining));
            std::streamsize n = file.gcount();
            if (n <= 0) break;
            for (std::streamsize i = 0; i < n; ++i) {
                ++histogram[static_cast<unsigned char>(buffer[i])];
            }
            total += n;
            remaining -= n;
        }
    };

    uintmax_t fileSize = fs::file_size(path);
    bool sampled = level < AnalysisLevel::Packets && fileSize > SAMPLE_COUNT * SAMPLE_SIZE;
    if (sampled) {
        for (std::size_t i = 0; i < SAMPLE_COUNT; ++i) {
            file.clear();
            file.seekg(static_cast<std::streamoff>((fileSize - SAMPLE_SIZE) * i / (SAMPLE_COUNT - 1)));
            consume(SAMPLE_SIZE);
        }
    } else {
        consume(SIZE_MAX);
    }

    nlohmann::json j;
    j["empty"] = fileSize == 0;
    j["entropySampled"] = sampled;
    if (total == 0) {
        j["entropy"] = nullptr;
        return j;
    }

    double entropy = 0.0;
    for (uint64_t count : histogram) {
        if (count == 0) continue;
        double p = static_cast<double>(count) / total;
        entropy -= p * std::log2(p);
    }
    // Bits per byte: ~8 indicates compressed or encrypted data.
    j["entropy"] = std::round(entropy * 1000.0) / 1000.0;
    return j;
}
