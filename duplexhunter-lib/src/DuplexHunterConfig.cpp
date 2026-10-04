#include "DuplexHunterConfig.hpp"

#include <iostream>

#define DEFAULT_DEPTH UINT16_MAX
#define DEFAULT_FAST_HASH false
#define DEFAULT_EXPORT_PATH "."
#define DEFAULT_ANALYSIS_LEVEL AnalysisLevel::None

const char* analysisLevelName(AnalysisLevel level) {
    switch (level) {
        case AnalysisLevel::Basic:   return "basic";
        case AnalysisLevel::Packets: return "packets";
        case AnalysisLevel::Deep:    return "deep";
        default:                     return "none";
    }
}

static bool parseAnalysisLevel(const std::string& value, AnalysisLevel& level) {
    for (AnalysisLevel l : { AnalysisLevel::Basic, AnalysisLevel::Packets, AnalysisLevel::Deep }) {
        if (value == analysisLevelName(l)) {
            level = l;
            return true;
        }
    }
    return false;
}

bool DuplexHunterConfig::parseArgs(int argc, char* argv[]) {
    depth = DEFAULT_DEPTH;
    fastHash = DEFAULT_FAST_HASH;
    exportPath = std::string(DEFAULT_EXPORT_PATH);
    analysisLevel = DEFAULT_ANALYSIS_LEVEL;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--path" && i + 1 < argc) {
            paths.push_back(argv[++i]);
        } else if (arg == "--depth" && i + 1 < argc) {
            depth = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--export-path" && i + 1 < argc) {
            exportPath = argv[++i];
        } else if (arg == "--upload-url" && i + 1 < argc) {
#ifdef DUPLEXHUNTER_HAS_UPLOAD
            uploadUrl = argv[++i];
#else
            std::cerr << "Error: --upload-url is not available, as Duplex Hunter was built without "
                      << "libcurl and libzip." << std::endl;
            return false;
#endif
        } else if (arg == "--enable-fast-hash") {
            fastHash = true;
        } else if (arg == "--analyze") {
            analysisLevel = AnalysisLevel::Basic;
        } else if (arg.rfind("--analyze=", 0) == 0) {
            std::string value = arg.substr(std::string("--analyze=").size());
            if (!parseAnalysisLevel(value, analysisLevel)) {
                std::cerr << "Error: Unknown analysis level: " << value
                          << " (expected basic, packets or deep)" << std::endl;
                return false;
            }
        } else if (arg == "--help") {
            std::cout << "Usage:\n"
                      << "  --path <path>             Path to scan (can be repeated)\n"
                      << "  --depth <n>               Max recursion depth\n"
                      << "  --export-path             Export path\n"
                      << "  --enable-fast-hash        Enable fast hash\n"
                      << "  --analyze[=<level>]       Gather per-file stats based on the file type\n"
                      << "                            basic:   headers and metadata (default)\n"
                      << "                            packets: also reads all media packets without decoding\n"
                      << "                                     (truncation, bitrate, keyframes, perceptual hashes)\n"
                      << "                            deep:    also decodes media completely\n"
                      << "                                     (decode errors, black frames, audio levels)\n"
                      << "  --upload-url <url>        Zip the results and upload them to the Duplex Hunter\n"
                      << "                            web UI, e.g. http://homelab:8080\n"
                      << "  --help                    Show this help\n";
            return false;
        } else {
            std::cerr << "Error: Unknown argument: " << arg << std::endl;
            return false;
        }
    }

    if (paths.empty()) {
        std::cerr << "Error: At least one path should be specified." << std::endl;
        return false;
    }

    if (exportPath.size() == 0) {
        exportPath = ".";
    }

    return true;
}