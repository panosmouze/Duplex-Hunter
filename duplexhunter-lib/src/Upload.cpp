#include "Upload.hpp"

#include <curl/curl.h>
#include <zip.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

#define RESULTS_ENDPOINT "/api/results"
#define CONNECT_TIMEOUT_S 10L

Upload::Upload(const std::string& exportDir, const std::string& url)
    : endpoint(makeEndpoint(url))
{
    fs::path dir = fs::path(exportDir).lexically_normal();
    if (!dir.has_filename()) dir = dir.parent_path();

    this->exportDir = dir.string();
    this->zipPath = dir.string() + ".zip";
}

Upload::~Upload() {

}

void Upload::setProgressCallback(std::function<void(DuplexHunterStatus, double)> cb) {
    progressCallback = std::move(cb);
}

const std::string& Upload::getZipPath() const {
    return zipPath;
}

const std::string& Upload::getEndpoint() const {
    return endpoint;
}

// Accepts both the base URL of the web UI and the full upload endpoint.
std::string Upload::makeEndpoint(const std::string& url) {
    std::string base = url;
    while (!base.empty() && base.back() == '/') base.pop_back();

    if (base.empty()) {
        throw std::runtime_error("Upload URL is empty");
    }

    const std::string suffix = RESULTS_ENDPOINT;
    if (base.size() >= suffix.size() && base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return base;
    }
    return base + suffix;
}

// libcurl reports progress continuously, even while waiting for the response.
void Upload::report(DuplexHunterStatus status, double progress) {
    int percent = static_cast<int>(progress * 100);
    if (!progressCallback || (status == lastStatus && percent == lastPercent)) return;

    lastStatus = status;
    lastPercent = percent;
    progressCallback(status, progress);
}

void Upload::createZip() {
    // The files are stored under the timestamp folder, as the CLI exports them.
    const std::string folder = fs::path(exportDir).filename().string();

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(exportDir)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    if (files.empty()) {
        throw std::runtime_error("Nothing to upload in " + exportDir);
    }

    int error = 0;
    zip_t* archive = zip_open(zipPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!archive) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, error);
        std::string msg = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        throw std::runtime_error("Failed to create " + zipPath + ": " + msg);
    }

    for (const fs::path& file : files) {
        std::string name = folder + "/" + file.filename().string();
        zip_source_t* source = zip_source_file(archive, file.c_str(), 0, ZIP_LENGTH_TO_END);
        if (!source || zip_file_add(archive, name.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0) {
            std::string msg = zip_strerror(archive);
            if (source) zip_source_free(source);
            zip_discard(archive);
            throw std::runtime_error("Failed to add " + file.string() + " to " + zipPath + ": " + msg);
        }
    }

    // Files are only read and compressed when the archive is closed.
    zip_register_progress_callback_with_state(archive, 0.01,
        [](zip_t*, double progress, void* self) {
            static_cast<Upload*>(self)->report(DuplexHunterStatus::Zipping, progress);
        }, nullptr, this);

    if (zip_close(archive) < 0) {
        std::string msg = zip_strerror(archive);
        zip_discard(archive);
        throw std::runtime_error("Failed to write " + zipPath + ": " + msg);
    }
}

void Upload::send() {
    static std::once_flag curlInit;
    std::call_once(curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) {
        throw std::runtime_error("Failed to initialize libcurl");
    }

    std::unique_ptr<curl_mime, decltype(&curl_mime_free)> form(curl_mime_init(curl.get()), curl_mime_free);
    curl_mimepart* part = curl_mime_addpart(form.get());
    curl_mime_name(part, "file");
    curl_mime_filedata(part, zipPath.c_str());
    curl_mime_type(part, "application/zip");

    std::string response;
    char errorBuffer[CURL_ERROR_SIZE] = {};

    curl_easy_setopt(curl.get(), CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_MIMEPOST, form.get());
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION,
        +[](char* data, size_t size, size_t count, void* out) -> size_t {
            static_cast<std::string*>(out)->append(data, size * count);
            return size * count;
        });
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION,
        +[](void* self, curl_off_t, curl_off_t, curl_off_t ultotal, curl_off_t ulnow) -> int {
            if (ultotal > 0) {
                static_cast<Upload*>(self)->report(DuplexHunterStatus::Uploading,
                                                   static_cast<double>(ulnow) / ultotal);
            }
            return 0;
        });

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        throw std::runtime_error("Upload to " + endpoint + " failed: "
                                 + (errorBuffer[0] ? errorBuffer : curl_easy_strerror(res))
                                 + "\nThe results are saved in " + zipPath);
    }

    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status >= 400) {
        // The web UI reports the reason in "detail".
        std::string detail = response;
        nlohmann::json j = nlohmann::json::parse(response, nullptr, false);
        if (j.is_object() && j.contains("detail")) {
            detail = j["detail"].is_string() ? j["detail"].get<std::string>() : j["detail"].dump();
        }
        throw std::runtime_error("Upload to " + endpoint + " was rejected (HTTP "
                                 + std::to_string(status) + "): " + detail
                                 + "\nThe results are saved in " + zipPath);
    }
    report(DuplexHunterStatus::Uploading, 1.0);
}
