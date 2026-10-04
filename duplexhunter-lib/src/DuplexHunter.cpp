#include "DuplexHunter.hpp"
#include "FileScanner.hpp"
#include "FileMatch.hpp"
#include "FileAnalysis.hpp"
#include "DuplexHunterProgressInfo.hpp"
#include "Export.hpp"
#ifdef DUPLEXHUNTER_HAS_UPLOAD
#include "Upload.hpp"
#endif
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
}

void DuplexHunter::analyze()
{
    FileAnalysis fileAnalysis(config.analysisLevel);

    std::vector<FileMatchGroup> groups = matcher->getDuplicates();
    std::vector<FileMatchGroup> uniques = matcher->getUnique();
    groups.insert(groups.end(), uniques.begin(), uniques.end());

    analysis = nlohmann::json::array();
    std::size_t processed = 0;
    for (const FileMatchGroup& group : groups) {
        // Reported before analyzing, as packet or deep analysis of a single media file can take long.
        if (progressCallback) {
            progressCallback({ DuplexHunterStatus::Analyzing, group.files.front(),
                               static_cast<double>(processed) / groups.size() });
        }
        analysis.push_back(fileAnalysis.analyze(group));
        ++processed;
    }
    if (progressCallback) progressCallback({ DuplexHunterStatus::Analyzing, "", 1.0 });
}

void DuplexHunter::run()
{
    scan();
    match();
    if (config.analysisLevel != AnalysisLevel::None) analyze();

    if (progressCallback) progressCallback({ DuplexHunterStatus::Done, "", 100.0 });
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
    if (!analysis.is_null()) exp.saveJson("analysis.json", analysis);
    exportDir = exp.getPath().string();
}

void DuplexHunter::uploadResults()
{
#ifdef DUPLEXHUNTER_HAS_UPLOAD
    if (exportDir.empty()) {
        throw std::runtime_error("Results must be exported before they are uploaded");
    }

    Upload upload(exportDir, config.uploadUrl);
    if (progressCallback) {
        upload.setProgressCallback([this, &upload](DuplexHunterStatus status, double progress) {
            const std::string& target = status == DuplexHunterStatus::Zipping
                ? upload.getZipPath() : upload.getEndpoint();
            progressCallback({ status, target, progress });
        });
    }

    upload.createZip();
    upload.send();
#else
    throw std::runtime_error("Duplex Hunter was built without upload support (libcurl and libzip)");
#endif
}

std::vector<FileMatchGroup> DuplexHunter::getDuplicates()
{
    return matcher->getDuplicates();
}

std::vector<FileMatchGroup> DuplexHunter::getUnique()
{
    return matcher->getUnique();
}

nlohmann::json DuplexHunter::getAnalysis()
{
    return analysis;
}