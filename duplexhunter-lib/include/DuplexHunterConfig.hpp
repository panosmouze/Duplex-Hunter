#ifndef _DUPLEX_HUNTER_CONFIG_HPP_
#define _DUPLEX_HUNTER_CONFIG_HPP_

#include "export.h"
#include <vector>
#include <string>
#include <cstdint>

// Each level includes the work of the levels below it.
enum class DUPLEXHUNTER_API AnalysisLevel {
    None,
    Basic,    // headers and metadata
    Packets,  // reads all packets of media files, without decoding
    Deep      // decodes media files completely
};

DUPLEXHUNTER_API const char* analysisLevelName(AnalysisLevel level);

struct DUPLEXHUNTER_API DuplexHunterConfig {
    std::vector<std::string> paths;
    uint16_t depth = UINT16_MAX;
    std::string exportPath;
    bool fastHash;
    AnalysisLevel analysisLevel = AnalysisLevel::None;

    bool parseArgs(int argc, char* argv[]);
};


#endif //_DUPLEX_HUNTER_CONFIG_HPP_