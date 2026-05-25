#include "Export.hpp"

Export::Export(const Config& cfg) {
    this->cfg = cfg;
    this->timestamp = makeTimestamp();
    this->path = fs::path(cfg.exportPath) / this->timestamp;

    try {
        fs::create_directories(this->path);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        throw std::runtime_error("Failed to create directory: " + this->path.string());
    }
    exportConfig();
}

Export::~Export() {

}

void Export::exportConfig() {
    json j_cfg;
    j_cfg["timestamp"] = timestamp;
    j_cfg["paths"] = cfg.paths;
    j_cfg["depth"] = cfg.depth;
    j_cfg["fashHash"] = cfg.fastHash;
    saveJson("config.json", j_cfg);
}

std::string Export::makeTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
    return oss.str();
}

void Export::saveJson(const std::string& fileName, const json& j) {
    std::ofstream out(path / fileName);
    if (!out) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    out << j.dump(4);
    out.close();
}