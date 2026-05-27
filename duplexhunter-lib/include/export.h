#ifndef __EXPORT_DUPLEXHUNTER_H
#define __EXPORT_DUPLEXHUNTER_H

#if defined(_WIN32)
    #ifdef DUPLEXHUNTER_EXPORTS
        #define DUPLEXHUNTER_API __declspec(dllexport)
    #else
        #define DUPLEXHUNTER_API __declspec(dllimport)
    #endif
#else
    #define DUPLEXHUNTER_API __attribute__((visibility("default")))
#endif

#endif //__EXPORT_DUPLEXHUNTER_H