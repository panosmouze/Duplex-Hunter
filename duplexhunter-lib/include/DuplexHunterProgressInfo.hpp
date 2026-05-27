#ifndef _DUPLEX_HUNTER_PROGRESS_INFO_HPP
#define _DUPLEX_HUNTER_PROGRESS_INFO_HPP

#include "export.h"
#include <string>

enum class DUPLEXHUNTER_API DuplexHunterStatus {
    Scanning,
    Hashing,
    Matching,
    Done
};

struct DUPLEXHUNTER_API DuplexHunterProgressInfo {
    DuplexHunterStatus status;
    std::string path;
    double progress;
};

#endif //_DUPLEX_HUNTER_PROGRESS_INFO_HPP