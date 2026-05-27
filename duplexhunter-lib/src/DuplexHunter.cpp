#include "DuplexHunter.hpp"
#include "FileScanner.hpp"
#include "FileMatch.hpp"
#include "DuplexHunterProgressInfo.hpp"
#include "Export.hpp"
#include <stdexcept>

DuplexHunter::DuplexHunter(const DuplexHunterConfig& config)
    : config(config)
{}

DuplexHunter::~DuplexHunter() = default;

void DuplexHunter::scan()
{
    scanners.clear();
    totalFiles = 0;

    for (const std::string& path : config.paths) {
    if (progressCallback) progressCallback({ DuplexHunterStatus::Scanning, path, 0.0 });
        auto scanner = std::make_shared<FileScanner>(path, config.fastHash, config.depth);
        scanner->runScan();
        totalFiles += scanner->getNumberOfFiles();
        scanners.push_back(std::move(scanner));
    }
}

void DuplexHunter::hashFiles()
{
    for (auto& scanner : scanners) {
        scanner->runHashes([this, scanner] {
            ++processedFiles;
            if (progressCallback && totalFiles > 0) {
                progressCallback({ DuplexHunterStatus::Hashing,
                                    scanner->getPath(),
                                   static_cast<double>(processedFiles) / totalFiles });
            }
        });
    }
}

void DuplexHunter::match()
{
    progressCallback({ DuplexHunterStatus::Matching, "", 100.0 });

    matcher = std::make_unique<FileMatch>(scanners);
    matcher->run();

    progressCallback({ DuplexHunterStatus::Done, "", 100.0 });
}

void DuplexHunter::run()
{
    scan();
    hashFiles();
    match();
}

void DuplexHunter::setProgressCallback(std::function<void(DuplexHunterProgressInfo)> cb)
{
    progressCallback = std::move(cb);
}

void DuplexHunter::exportResults()
{
    Export exp(config);
    exp.saveJson("duplicates.json", matcher->getDuplicates());
    exp.saveJson("uniques.json", matcher->getUnique());
}