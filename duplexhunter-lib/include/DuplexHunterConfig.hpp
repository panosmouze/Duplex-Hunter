#ifndef _DUPLEX_HUNTER_CONFIG_HPP_
#define _DUPLEX_HUNTER_CONFIG_HPP_

#include "export.h"
#include <vector>
#include <string>
#include <cstdint>

struct DUPLEXHUNTER_API DuplexHunterConfig {
    std::vector<std::string> paths;
    uint16_t depth = UINT16_MAX;
    std::string exportPath;
    bool fastHash;

    bool parseArgs(int argc, char* argv[]);
};


#endif //_DUPLEX_HUNTER_CONFIG_HPP_