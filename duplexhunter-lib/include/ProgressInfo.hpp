#ifndef _DUPLEX_HUNTER_STATUS_HPP
#define _DUPLEX_HUNTER_STATUS_HPP

#include "export.h"
#include <string>

enum class DUPLEX_API DuplexHunterStatus {
    Scanning,
    Hashing,
    Matching,
    Done
};

struct DUPLEX_API ProgressInfo {
    DuplexHunterStatus status;
    std::string path;
    double progress;
};

#endif //_DUPLEX_HUNTER_STATUS_HPP