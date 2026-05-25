#ifndef _EXPORT_HPP_
#define _EXPORT_HPP_

#include "Config.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <iostream>
#include <memory>

namespace fs = std::filesystem;
using json = nlohmann::json;

class Export
{
public:
    Export(const Config& cfg);
    ~Export();
    void saveJson(const std::string& fileName, const json& j);

private:
    void exportConfig();
    std::string makeTimestamp();

private:
    fs::path path;
    Config cfg;
    std::string timestamp;
};


#endif //_EXPORT_HPP_