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
    for (const std::string& path : config.paths) {
        if (progressCallback) progressCallback({ DuplexHunterStatus::Scanning, path, 0.0 });
        FileScanner scanner(collector, path, config.depth);
        scanner.runScan();
    }
}

void DuplexHunter::match()
{
    matcher = std::make_unique<FileMatch>(collector, config.fastHash);
    if (progressCallback) {
        matcher->setProgressCallback([this](const std::string& path, double progress) {
            progressCallback({ DuplexHunterStatus::Hashing, path, progress });
        });
    }

    matcher->run();

    if (progressCallback) progressCallback({ DuplexHunterStatus::Matching, "", 100.0 });
    if (progressCallback) progressCallback({ DuplexHunterStatus::Done, "", 100.0 });
}

void DuplexHunter::run()
{
    scan();
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

std::vector<FileMatchGroup> DuplexHunter::getDuplicates()
{
    return matcher->getDuplicates();
}

std::vector<FileMatchGroup> DuplexHunter::getUnique()
{
    return matcher->getUnique();
}