#ifndef _UPLOAD_HPP_
#define _UPLOAD_HPP_

#include "DuplexHunterProgressInfo.hpp"

#include <string>
#include <functional>

// Zips an export directory as <timestamp>.zip next to it and uploads it to
// the Duplex Hunter web UI, which uses the timestamp as the result's name.
class Upload
{
public:
    Upload(const std::string& exportDir, const std::string& url);
    ~Upload();

    void setProgressCallback(std::function<void(DuplexHunterStatus, double)> cb);

    void createZip();
    void send();

    const std::string& getZipPath() const;
    const std::string& getEndpoint() const;

private:
    static std::string makeEndpoint(const std::string& url);
    void report(DuplexHunterStatus status, double progress);

private:
    std::string exportDir;
    std::string zipPath;
    std::string endpoint;
    std::function<void(DuplexHunterStatus, double)> progressCallback;
    DuplexHunterStatus lastStatus = DuplexHunterStatus::Done;
    int lastPercent = -1;
};


#endif //_UPLOAD_HPP_
