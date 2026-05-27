#include "DuplexHunterConfig.hpp"

#include <iostream>

bool DuplexHunterConfig::parseArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--path" && i + 1 < argc) {
            paths.push_back(argv[++i]);
        } else if (arg == "--depth" && i + 1 < argc) {
            depth = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--export-path" && i + 1 < argc) {
            exportPath = argv[++i];
        } else if (arg == "--enable-fast-hash") {
            fastHash = true;
        } else if (arg == "--help") {
            std::cout << "Usage:\n"
                      << "  --path <path>             Path to scan (can be repeated)\n"
                      << "  --depth <n>               Max recursion depth\n"
                      << "  --export-path             Export path\n"
                      << "  --enable-fast-hash        Enable fast hash\n"
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