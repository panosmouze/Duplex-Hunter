#include "DuplexHunter.hpp"
#include "DuplexHunterProgressInfo.hpp"
#include "DuplexHunterConfig.hpp"

#include <iostream>
#include <iomanip>

static void printProgress(const DuplexHunterProgressInfo& info)
{
    switch (info.status) {
        case DuplexHunterStatus::Scanning:
            std::cout << "Scanning " << info.path << std::endl;
            break;
        case DuplexHunterStatus::Hashing: {
            int percent = static_cast<int>(info.progress * 100);
            int filled  = percent / 5;
            std::cout << "\033[2K\rHashing  ["
                      << std::string(filled, '#')
                      << std::string(20 - filled, ' ')
                      << "] "
                      << std::setw(3) << percent << "% "
                      << info.path
                      << std::flush;
            break;
        }
        case DuplexHunterStatus::Matching:
            std::cout << std::endl << "Matching Results" << std::endl;
            break;
        case DuplexHunterStatus::Done:
            std::cout << "Done" << std::endl;
            break;
        default:
            break;
    }
}

int main(int argc, char* argv[])
{
    DuplexHunterConfig cfg;
    if (!cfg.parseArgs(argc, argv)) {
        std::cerr << "Configuration items missing or unknown." << std::endl;
        return 1;
    }

    try {
        DuplexHunter hunter(cfg);
        hunter.setProgressCallback(printProgress);
        hunter.run();

        auto duplicates = hunter.getDuplicates();
        auto uniques = hunter.getUnique();
        std::size_t duplicateFiles = 0;
        for (const auto& group : duplicates) {
            duplicateFiles += group.files.size();
        }

        std::cout << duplicates.size() << " duplicate group(s) covering " << duplicateFiles
                   << " file(s), " << uniques.size() << " unique file(s)." << std::endl;

        hunter.exportResults();

    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}