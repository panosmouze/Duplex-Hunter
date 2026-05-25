#include <FileScanner.hpp>
#include <FileMatch.hpp>
#include <Config.hpp>
#include <Export.hpp>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main(int argc, char* argv[]) {
    Config cfg;
    if (!cfg.parseArgs(argc, argv)) {
        std::cerr << "Configuration items missing or unknown." << std::endl;
        return 1;
    }

    std::vector<std::shared_ptr<FileScanner>> scanners;

    try {
        for (const std::string& path : cfg.paths) {
            scanners.push_back(std::make_shared<FileScanner>(path, cfg.fastHash, cfg.depth));
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    std::size_t total = 0;
    for (auto& scanner : scanners) {
        scanner->runScan();
        total += scanner->getNumberOfFiles();
    }

    std::size_t totalProgress = 0;
    for (auto& scanner : scanners) {
        scanner->runHashes();
        totalProgress += scanner->getNumberOfFiles();
        std::cout << "Total progress: " << (totalProgress * 100 / total) << "%" << std::endl;
    }

    FileMatch match(scanners);
    match.run();

    try {
        Export exp(cfg);
        auto duplicates = match.getDuplicates();
        exp.saveJson("duplicates.json", duplicates);
        auto unique = match.getUnique();
        exp.saveJson("uniques.json", unique);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << "Error: Exporting results." << std::endl;
        return 1;
    }

    return 0;
}