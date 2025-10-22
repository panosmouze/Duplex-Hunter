#ifndef _CONFIG_HPP_
#define _CONFIG_HPP_

#include <vector>
#include <string>
#include <cstdint>

struct Config {
    std::vector<std::string> paths;
    uint16_t depth = UINT16_MAX;
    std::string exportPath;
    bool fastHash;

    bool parseArgs(int argc, char* argv[]);
};


#endif //_CONFIG_HPP_