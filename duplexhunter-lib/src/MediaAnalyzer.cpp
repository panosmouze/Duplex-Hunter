#include "MediaAnalyzer.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <vector>

namespace {

struct FormatCloser { void operator()(AVFormatContext* p) const { avformat_close_input(&p); } };
struct CodecFree    { void operator()(AVCodecContext* p) const { avcodec_free_context(&p); } };
struct FrameFree    { void operator()(AVFrame* p) const { av_frame_free(&p); } };
struct PacketFree   { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct SwsFree      { void operator()(SwsContext* p) const { sws_free_context(&p); } };
struct SwrFree      { void operator()(SwrContext* p) const { swr_free(&p); } };

using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
using CodecPtr  = std::unique_ptr<AVCodecContext, CodecFree>;
using FramePtr  = std::unique_ptr<AVFrame, FrameFree>;
using PacketPtr = std::unique_ptr<AVPacket, PacketFree>;
using SwsPtr    = std::unique_ptr<SwsContext, SwsFree>;
using SwrPtr    = std::unique_ptr<SwrContext, SwrFree>;

// Frames are reduced to a 9x8 luma thumbnail, used for the difference hash (dHash)
// and black frame detection.
constexpr int THUMB_W = 9;
constexpr int THUMB_H = 8;
using Thumbnail = std::array<uint8_t, THUMB_W * THUMB_H>;

constexpr double BLACK_FRAME_LUMA = 20.0;
// Content may fall short of the container figures by this much before it counts as incomplete.
constexpr double MISSING_TOLERANCE = 0.02;
constexpr double MISSING_DURATION_MIN = 0.5;
// A jump in a stream's timestamps longer than this counts as a gap.
constexpr double GAP_THRESHOLD = 1.0;
// Per-second bitrate variation (coefficient of variation) below which a stream counts as constant bitrate.
constexpr double CONSTANT_BITRATE_VARIATION = 0.05;
// Frame intervals within this relative difference of the median count as the same frame rate.
constexpr double FRAME_INTERVAL_TOLERANCE = 0.01;
constexpr float CLIP_LEVEL = 0.9999f;
constexpr float SILENCE_LEVEL = 0.001f; // -60 dBFS
// Relative positions in a video at which perceptual hashes are taken.
constexpr std::array<double, 5> HASH_POSITIONS = { 0.1, 0.3, 0.5, 0.7, 0.9 };
constexpr double HASH_TIME_EPSILON = 1e-3;

std::string errorString(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

double round3(double v) {
    return std::round(v * 1000.0) / 1000.0;
}

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    std::size_t start = s.find_first_not_of(ws);
    if (start == std::string::npos) return "";
    return s.substr(start, s.find_last_not_of(ws) - start + 1);
}

nlohmann::json dictToJson(const AVDictionary* dict) {
    nlohmann::json j = nlohmann::json::object();
    const AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_iterate(dict, entry))) {
        j[entry->key] = trim(entry->value);
    }
    return j;
}

nlohmann::json nameOrNull(const char* name) {
    return name ? nlohmann::json(name) : nlohmann::json(nullptr);
}

nlohmann::json seconds(int64_t value, AVRational timeBase) {
    if (value == AV_NOPTS_VALUE || value <= 0) return nullptr;
    return round3(value * av_q2d(timeBase));
}

std::string dhash(const Thumbnail& px) {
    uint64_t hash = 0;
    for (int y = 0; y < THUMB_H; ++y) {
        for (int x = 0; x < THUMB_W - 1; ++x) {
            hash = (hash << 1) | (px[y * THUMB_W + x] > px[y * THUMB_W + x + 1] ? 1 : 0);
        }
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return buf;
}

double meanLuma(const Thumbnail& px) {
    double sum = 0;
    for (uint8_t v : px) sum += v;
    return sum / px.size();
}

nlohmann::json streamInfo(AVFormatContext* fmt, AVStream* st) {
    const AVCodecParameters* par = st->codecpar;
    const AVCodecDescriptor* desc = avcodec_descriptor_get(par->codec_id);

    nlohmann::json j;
    j["index"] = st->index;
    j["type"] = nameOrNull(av_get_media_type_string(par->codec_type));
    j["codec"] = desc ? nlohmann::json(desc->name) : nlohmann::json(nullptr);
    j["codecLongName"] = desc ? nameOrNull(desc->long_name) : nlohmann::json(nullptr);
    j["profile"] = nameOrNull(avcodec_profile_name(par->codec_id, par->profile));
    j["bitrate"] = par->bit_rate > 0 ? nlohmann::json(par->bit_rate) : nlohmann::json(nullptr);
    j["duration"] = seconds(st->duration, st->time_base);

    const AVDictionaryEntry* language = av_dict_get(st->metadata, "language", nullptr, 0);
    const AVDictionaryEntry* title = av_dict_get(st->metadata, "title", nullptr, 0);
    j["language"] = language ? nlohmann::json(language->value) : nlohmann::json(nullptr);
    j["title"] = title ? nlohmann::json(title->value) : nlohmann::json(nullptr);
    j["default"] = (st->disposition & AV_DISPOSITION_DEFAULT) != 0;
    j["attachedPicture"] = (st->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
    j["forced"] = (st->disposition & AV_DISPOSITION_FORCED) != 0;
    j["hearingImpaired"] = (st->disposition & AV_DISPOSITION_HEARING_IMPAIRED) != 0;

    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        j["width"] = par->width;
        j["height"] = par->height;

        AVRational rate = av_guess_frame_rate(fmt, st, nullptr);
        j["frameRate"] = rate.num > 0 && rate.den > 0 ? nlohmann::json(round3(av_q2d(rate))) : nlohmann::json(nullptr);
        j["frames"] = st->nb_frames > 0 ? nlohmann::json(st->nb_frames) : nlohmann::json(nullptr);

        const AVPixFmtDescriptor* pix = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(par->format));
        j["pixelFormat"] = pix ? nlohmann::json(pix->name) : nlohmann::json(nullptr);
        j["bitDepth"] = pix ? nlohmann::json(pix->comp[0].depth) : nlohmann::json(nullptr);
        j["hasAlpha"] = pix ? nlohmann::json((pix->flags & AV_PIX_FMT_FLAG_ALPHA) != 0) : nlohmann::json(nullptr);

        j["colorRange"] = par->color_range != AVCOL_RANGE_UNSPECIFIED ? nameOrNull(av_color_range_name(par->color_range)) : nlohmann::json(nullptr);
        j["colorPrimaries"] = par->color_primaries != AVCOL_PRI_UNSPECIFIED ? nameOrNull(av_color_primaries_name(par->color_primaries)) : nlohmann::json(nullptr);
        j["colorTransfer"] = par->color_trc != AVCOL_TRC_UNSPECIFIED ? nameOrNull(av_color_transfer_name(par->color_trc)) : nlohmann::json(nullptr);
        j["colorSpace"] = par->color_space != AVCOL_SPC_UNSPECIFIED ? nameOrNull(av_color_space_name(par->color_space)) : nlohmann::json(nullptr);
        j["hdr"] = par->color_trc == AVCOL_TRC_SMPTE2084 || par->color_trc == AVCOL_TRC_ARIB_STD_B67;

        // Same convention as ffprobe: counterclockwise degrees from the display matrix.
        const AVPacketSideData* sd = av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
        double rotation = sd ? av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data)) : 0.0;
        j["rotation"] = std::isnan(rotation) ? 0.0 : round3(rotation);
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
        j["sampleRate"] = par->sample_rate;
        j["channels"] = par->ch_layout.nb_channels;

        char layout[128];
        j["channelLayout"] = av_channel_layout_describe(&par->ch_layout, layout, sizeof(layout)) > 0
            ? nlohmann::json(layout) : nlohmann::json(nullptr);
        j["sampleFormat"] = nameOrNull(av_get_sample_fmt_name(static_cast<AVSampleFormat>(par->format)));

        int bits = par->bits_per_raw_sample > 0 ? par->bits_per_raw_sample : par->bits_per_coded_sample;
        j["bitsPerSample"] = bits > 0 ? nlohmann::json(bits) : nlohmann::json(nullptr);

        // Some codecs support both modes, in which case it cannot be told from the parameters.
        bool lossless = desc && (desc->props & AV_CODEC_PROP_LOSSLESS);
        bool lossy = desc && (desc->props & AV_CODEC_PROP_LOSSY);
        j["lossless"] = lossless == lossy ? nlohmann::json(nullptr) : nlohmann::json(lossless);
    }

    return j;
}

CodecPtr openDecoder(AVStream* st) {
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return nullptr;

    CodecPtr ctx(avcodec_alloc_context3(codec));
    if (!ctx || avcodec_parameters_to_context(ctx.get(), st->codecpar) < 0) return nullptr;

    ctx->pkt_timebase = st->time_base;
    ctx->thread_count = 0;
    ctx->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM;
    if (avcodec_open2(ctx.get(), codec, nullptr) < 0) return nullptr;
    return ctx;
}

class StreamDecoder {
public:
    explicit StreamDecoder(AVStream* stream) : stream(stream), ctx(openDecoder(stream)) {}
    virtual ~StreamDecoder() = default;

    bool isOpen() const { return ctx != nullptr; }

    // Sends a packet (nullptr to flush) and processes all frames that become available.
    void feed(const AVPacket* pkt, AVFrame* frame) {
        int ret = avcodec_send_packet(ctx.get(), pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) ++errors;

        while ((ret = avcodec_receive_frame(ctx.get(), frame)) >= 0) {
            ++frames;
            if (frame->decode_error_flags || (frame->flags & AV_FRAME_FLAG_CORRUPT)) ++corruptFrames;
            process(frame);
            av_frame_unref(frame);
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) ++errors;
    }

protected:
    virtual void process(const AVFrame* frame) = 0;

public:
    AVStream* stream;
    uint64_t frames = 0;
    uint64_t corruptFrames = 0;
    uint64_t errors = 0;

private:
    CodecPtr ctx;
};

// Reduces frames to a luma thumbnail for perceptual hashing and black frame detection.
class FrameHasher {
public:
    FrameHasher() : sws(sws_alloc_context()), thumbFrame(av_frame_alloc()) {
        if (sws) sws->flags = SWS_AREA;
    }

    bool thumbnail(const AVFrame* src, Thumbnail& out) {
        if (!sws || !thumbFrame) return false;
        av_frame_unref(thumbFrame.get());
        thumbFrame->width = THUMB_W;
        thumbFrame->height = THUMB_H;
        thumbFrame->format = AV_PIX_FMT_GRAY8;
        if (sws_scale_frame(sws.get(), thumbFrame.get(), src) < 0) return false;

        for (int y = 0; y < THUMB_H; ++y) {
            for (int x = 0; x < THUMB_W; ++x) {
                out[y * THUMB_W + x] = thumbFrame->data[0][y * thumbFrame->linesize[0] + x];
            }
        }
        return true;
    }

private:
    SwsPtr sws;
    FramePtr thumbFrame;
};

// Seconds since the start of the stream.
double frameTime(const AVStream* stream, const AVFrame* frame) {
    int64_t start = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    return frame->best_effort_timestamp != AV_NOPTS_VALUE
        ? (frame->best_effort_timestamp - start) * av_q2d(stream->time_base) : 0.0;
}

double streamDuration(const AVStream* stream, double fallback) {
    return stream->duration != AV_NOPTS_VALUE ? stream->duration * av_q2d(stream->time_base) : fallback;
}

// Targets for the perceptual hashes; with an unknown duration only the first frame is hashed.
std::vector<double> hashTargets(double duration) {
    if (duration <= 0) return { 0.0 };
    std::vector<double> targets;
    for (double position : HASH_POSITIONS) targets.push_back(position * duration);
    return targets;
}

class VideoDecoder : public StreamDecoder {
public:
    VideoDecoder(AVStream* stream, double duration)
        : StreamDecoder(stream), targets(hashTargets(duration)) {}

    nlohmann::json firstFrameMetadata = nlohmann::json::object();
    std::string firstFrameHash;
    uint64_t blackFrames = 0;
    nlohmann::json frameHashes = nlohmann::json::array();
    double decodedUntil = 0.0;

protected:
    void process(const AVFrame* frame) override {
        Thumbnail thumb;
        if (!hasher.thumbnail(frame, thumb)) return;
        std::string hash = dhash(thumb);

        if (frames == 1) {
            firstFrameMetadata = dictToJson(frame->metadata);
            firstFrameHash = hash;
        }
        if (meanLuma(thumb) < BLACK_FRAME_LUMA) ++blackFrames;

        double time = frameTime(stream, frame);
        decodedUntil = std::max(decodedUntil, time + frame->duration * av_q2d(stream->time_base));

        while (nextTarget < targets.size() && time >= targets[nextTarget] - HASH_TIME_EPSILON) {
            frameHashes.push_back({ {"time", round3(time)}, {"dhash", hash} });
            ++nextTarget;
        }
    }

private:
    FrameHasher hasher;
    std::vector<double> targets;
    std::size_t nextTarget = 0;
};

// Statistics gathered from the packets of one stream, without decoding them.
class PacketStats {
public:
    explicit PacketStats(AVStream* stream)
        : stream(stream),
          timeBase(av_q2d(stream->time_base)),
          start(stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0),
          video(stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO),
          timed(video || stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {}

    void add(const AVPacket* pkt) {
        ++packets;
        bytes += pkt->size;
        if (pkt->flags & AV_PKT_FLAG_CORRUPT) ++corruptPackets;
        if (pkt->flags & AV_PKT_FLAG_KEY) ++keyframes;

        double length = pkt->duration > 0 ? pkt->duration * timeBase : 0.0;
        int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
        if (ts != AV_NOPTS_VALUE) {
            double t = seconds(ts);
            firstTime = std::min(firstTime, t);
            endTime = std::max(endTime, t + length);
            bytesPerSecond[static_cast<int64_t>(std::floor(t))] += pkt->size;
            if (video) {
                frameTimes.push_back(t);
                if (pkt->flags & AV_PKT_FLAG_KEY) keyframeTimes.push_back(t);
            }
        }

        // Decoding timestamps must increase; presentation timestamps are reordered by B-frames.
        if (timed && pkt->dts != AV_NOPTS_VALUE) {
            double t = seconds(pkt->dts);
            if (prevDts != AV_NOPTS_VALUE) {
                if (pkt->dts < prevDts) {
                    ++timestampErrors;
                } else if (t - prevDtsEnd > GAP_THRESHOLD) {
                    ++gaps;
                    gapDuration += t - prevDtsEnd;
                }
            }
            prevDts = pkt->dts;
            prevDtsEnd = t + length;
        }
    }

    bool hasTimes() const { return endTime > firstTime; }
    double end() const { return hasTimes() ? endTime : 0.0; }
    double first() const { return firstTime; }

    nlohmann::json toJson() const {
        nlohmann::json j;
        double duration = hasTimes() ? endTime - firstTime : 0.0;

        j["index"] = stream->index;
        j["type"] = nameOrNull(av_get_media_type_string(stream->codecpar->codec_type));
        j["packets"] = packets;
        j["bytes"] = bytes;
        j["start"] = hasTimes() ? nlohmann::json(round3(firstTime)) : nlohmann::json(nullptr);
        j["duration"] = duration > 0 ? nlohmann::json(round3(duration)) : nlohmann::json(nullptr);
        j["bitrate"] = duration > 0 ? nlohmann::json(std::llround(bytes * 8 / duration)) : nlohmann::json(nullptr);
        addBitrateStats(j);
        j["corruptPackets"] = corruptPackets;

        if (video) {
            j["keyframes"] = keyframes;
            addKeyframeStats(j);
            addFrameRateStats(j);
        }
        if (timed) {
            j["timestampErrors"] = timestampErrors;
            j["gaps"] = gaps;
            j["gapDuration"] = round3(gapDuration);
        }
        return j;
    }

private:
    double seconds(int64_t ts) const { return (ts - start) * timeBase; }

    // Per-second bitrate, ignoring the first and last seconds which are usually partial.
    void addBitrateStats(nlohmann::json& j) const {
        std::vector<double> perSecond;
        if (bytesPerSecond.size() > 2) {
            auto last = std::prev(bytesPerSecond.end());
            for (auto it = std::next(bytesPerSecond.begin()); it != last; ++it) {
                perSecond.push_back(it->second * 8.0);
            }
        }
        if (perSecond.size() < 3) {
            j["peakBitrate"] = nullptr;
            j["bitrateVariation"] = nullptr;
            j["bitrateMode"] = nullptr;
            return;
        }

        double mean = 0.0, variance = 0.0;
        for (double b : perSecond) mean += b;
        mean /= perSecond.size();
        for (double b : perSecond) variance += (b - mean) * (b - mean);
        double variation = mean > 0 ? std::sqrt(variance / perSecond.size()) / mean : 0.0;

        j["peakBitrate"] = std::llround(*std::max_element(perSecond.begin(), perSecond.end()));
        j["bitrateVariation"] = round3(variation);
        j["bitrateMode"] = variation < CONSTANT_BITRATE_VARIATION ? "constant" : "variable";
    }

    void addKeyframeStats(nlohmann::json& j) const {
        std::vector<double> times = keyframeTimes;
        std::sort(times.begin(), times.end());
        if (times.size() < 2) {
            j["keyframeInterval"] = nullptr;
            j["maxKeyframeInterval"] = nullptr;
            return;
        }
        double maxInterval = 0.0;
        for (std::size_t i = 1; i < times.size(); ++i) {
            maxInterval = std::max(maxInterval, times[i] - times[i - 1]);
        }
        j["keyframeInterval"] = round3((times.back() - times.front()) / (times.size() - 1));
        j["maxKeyframeInterval"] = round3(maxInterval);
    }

    void addFrameRateStats(nlohmann::json& j) const {
        std::vector<double> times = frameTimes;
        std::sort(times.begin(), times.end());

        std::vector<double> intervals;
        for (std::size_t i = 1; i < times.size(); ++i) {
            if (times[i] > times[i - 1]) intervals.push_back(times[i] - times[i - 1]);
        }
        if (intervals.size() < 3) {
            j["averageFrameRate"] = nullptr;
            j["frameRateMode"] = nullptr;
            return;
        }

        std::vector<double> sorted = intervals;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        double median = sorted[sorted.size() / 2];
        // Timestamps are rounded to the time base, e.g. 29.97 fps in milliseconds alternates 33 and 34.
        double tolerance = std::max(2 * timeBase, median * FRAME_INTERVAL_TOLERANCE);
        std::size_t irregular = std::count_if(intervals.begin(), intervals.end(),
            [&](double d) { return std::fabs(d - median) > tolerance; });

        j["averageFrameRate"] = round3((times.size() - 1) / (times.back() - times.front()));
        j["frameRateMode"] = irregular > std::max<std::size_t>(1, intervals.size() / 100) ? "variable" : "constant";
    }

public:
    AVStream* stream;
    uint64_t packets = 0;
    uint64_t bytes = 0;
    uint64_t keyframes = 0;
    uint64_t corruptPackets = 0;
    uint64_t timestampErrors = 0;
    uint64_t gaps = 0;
    double gapDuration = 0.0;

private:
    double timeBase;
    int64_t start;
    bool video;
    bool timed;

    double firstTime = std::numeric_limits<double>::infinity();
    double endTime = -std::numeric_limits<double>::infinity();
    int64_t prevDts = AV_NOPTS_VALUE;
    double prevDtsEnd = 0.0;
    std::map<int64_t, uint64_t> bytesPerSecond;
    std::vector<double> frameTimes;
    std::vector<double> keyframeTimes;
};

class AudioDecoder : public StreamDecoder {
public:
    using StreamDecoder::StreamDecoder;

    double decodedDuration() const {
        return sampleRate > 0 ? static_cast<double>(positions) / sampleRate : 0.0;
    }

    nlohmann::json levels() const {
        nlohmann::json j;
        if (positions == 0 || sampleRate <= 0) return nullptr;

        bool silent = firstLoud < 0;
        j["peakDb"] = peak > 0 ? nlohmann::json(round3(20.0 * std::log10(peak))) : nlohmann::json(nullptr);
        j["rmsDb"] = sumSquares > 0 ? nlohmann::json(round3(10.0 * std::log10(static_cast<double>(sumSquares / samples)))) : nlohmann::json(nullptr);
        j["clippedSamples"] = clipped;
        j["silent"] = silent;
        j["leadingSilence"] = round3(static_cast<double>(silent ? positions : firstLoud) / sampleRate);
        j["trailingSilence"] = round3(static_cast<double>(silent ? positions : positions - 1 - lastLoud) / sampleRate);
        return j;
    }

protected:
    void process(const AVFrame* frame) override {
        int channels = frame->ch_layout.nb_channels;
        if (channels <= 0) return;
        if (!swr || frame->format != inFormat || frame->sample_rate != sampleRate || channels != inChannels) {
            if (!setupResampler(frame)) return;
        }

        int maxOut = swr_get_out_samples(swr.get(), frame->nb_samples);
        if (maxOut <= 0) return;
        buffer.resize(static_cast<std::size_t>(maxOut) * channels);
        uint8_t* out = reinterpret_cast<uint8_t*>(buffer.data());
        int n = swr_convert(swr.get(), &out, maxOut, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);

        for (int i = 0; i < n; ++i) {
            bool loud = false;
            for (int c = 0; c < channels; ++c) {
                float v = buffer[static_cast<std::size_t>(i) * channels + c];
                float a = std::fabs(v);
                peak = std::max(peak, static_cast<double>(a));
                sumSquares += static_cast<long double>(v) * v;
                if (a >= CLIP_LEVEL) ++clipped;
                if (a > SILENCE_LEVEL) loud = true;
            }
            if (loud) {
                int64_t pos = positions + i;
                if (firstLoud < 0) firstLoud = pos;
                lastLoud = pos;
            }
        }
        positions += n > 0 ? n : 0;
        samples += static_cast<uint64_t>(n > 0 ? n : 0) * channels;
    }

private:
    bool setupResampler(const AVFrame* frame) {
        SwrContext* raw = nullptr;
        if (swr_alloc_set_opts2(&raw, &frame->ch_layout, AV_SAMPLE_FMT_FLT, frame->sample_rate,
                                &frame->ch_layout, static_cast<AVSampleFormat>(frame->format), frame->sample_rate,
                                0, nullptr) < 0 || swr_init(raw) < 0) {
            swr_free(&raw);
            swr.reset();
            return false;
        }
        swr.reset(raw);
        inFormat = frame->format;
        inChannels = frame->ch_layout.nb_channels;
        sampleRate = frame->sample_rate;
        return true;
    }

private:
    SwrPtr swr;
    int inFormat = -1;
    int inChannels = 0;
    int sampleRate = 0;
    std::vector<float> buffer;

    double peak = 0.0;
    long double sumSquares = 0.0;
    uint64_t samples = 0;    // individual channel samples
    int64_t positions = 0;   // sample positions across all channels
    uint64_t clipped = 0;
    int64_t firstLoud = -1;
    int64_t lastLoud = -1;
};

// Seeks to each hash position and decodes only from the preceding keyframe up to it,
// which gives the same hashes as a full decode at a fraction of the cost.
nlohmann::json seekFrameHashes(AVFormatContext* fmt, AVStream* stream, double duration) {
    nlohmann::json hashes = nlohmann::json::array();

    CodecPtr ctx = openDecoder(stream);
    PacketPtr pkt(av_packet_alloc());
    FramePtr frame(av_frame_alloc());
    if (!ctx || !pkt || !frame) return hashes;

    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        fmt->streams[i]->discard = fmt->streams[i] == stream ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }

    FrameHasher hasher;
    int64_t start = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;

    for (double target : hashTargets(duration)) {
        int64_t ts = start + static_cast<int64_t>(target / av_q2d(stream->time_base));
        if (av_seek_frame(fmt, stream->index, ts, AVSEEK_FLAG_BACKWARD) < 0) continue;
        avcodec_flush_buffers(ctx.get());

        bool found = false;
        bool eof = false;
        while (!found && !eof) {
            int ret = av_read_frame(fmt, pkt.get());
            if (ret < 0) {
                eof = true;
                avcodec_send_packet(ctx.get(), nullptr);
            } else if (pkt->stream_index != stream->index) {
                av_packet_unref(pkt.get());
                continue;
            } else {
                avcodec_send_packet(ctx.get(), pkt.get());
                av_packet_unref(pkt.get());
            }

            while (!found && avcodec_receive_frame(ctx.get(), frame.get()) >= 0) {
                double time = frameTime(stream, frame.get());
                Thumbnail thumb;
                if (time >= target - HASH_TIME_EPSILON && hasher.thumbnail(frame.get(), thumb)) {
                    hashes.push_back({ {"time", round3(time)}, {"dhash", dhash(thumb)} });
                    found = true;
                }
                av_frame_unref(frame.get());
            }
        }
    }
    return hashes;
}

} // namespace

MediaAnalyzer::MediaAnalyzer() {
    // Decoder warnings about damaged files are reported in the results instead.
    av_log_set_level(AV_LOG_QUIET);
}

std::string MediaAnalyzer::name() const {
    return "media";
}

bool MediaAnalyzer::canHandle(const FileType& type) const {
    const std::string& mime = type.mime;
    if (mime == "image/svg+xml") return false;
    return mime.rfind("video/", 0) == 0
        || mime.rfind("audio/", 0) == 0
        || mime.rfind("image/", 0) == 0
        || mime == "application/ogg";
}

nlohmann::json MediaAnalyzer::subtitleStats(const fs::path& path) {
    AVFormatContext* rawFmt = nullptr;
    int ret = avformat_open_input(&rawFmt, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        return { {"error", errorString(ret)} };
    }
    FormatPtr fmt(rawFmt);

    ret = avformat_find_stream_info(fmt.get(), nullptr);
    if (ret < 0) {
        return { {"error", errorString(ret)} };
    }

    int index = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_SUBTITLE, -1, -1, nullptr, 0);
    if (index < 0) {
        return { {"error", "No subtitle stream found"} };
    }
    const AVStream* stream = fmt->streams[index];

    PacketPtr pkt(av_packet_alloc());
    if (!pkt) throw std::bad_alloc();

    uint64_t cues = 0;
    double first = std::numeric_limits<double>::infinity();
    double last = 0.0;
    while (av_read_frame(fmt.get(), pkt.get()) >= 0) {
        if (pkt->stream_index == index && pkt->pts != AV_NOPTS_VALUE) {
            double start = pkt->pts * av_q2d(stream->time_base);
            double end = start + std::max<int64_t>(pkt->duration, 0) * av_q2d(stream->time_base);
            ++cues;
            first = std::min(first, start);
            last = std::max(last, end);
        }
        av_packet_unref(pkt.get());
    }

    nlohmann::json j;
    j["codec"] = nameOrNull(avcodec_get_name(stream->codecpar->codec_id));
    j["cues"] = cues;
    j["firstCue"] = cues ? nlohmann::json(round3(first)) : nlohmann::json(nullptr);
    j["lastCue"] = cues ? nlohmann::json(round3(last)) : nlohmann::json(nullptr);
    return j;
}

nlohmann::json MediaAnalyzer::analyze(const fs::path& path, const FileType& type, AnalysisLevel level) {
    AVFormatContext* rawFmt = nullptr;
    int ret = avformat_open_input(&rawFmt, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        return { {"error", errorString(ret)} };
    }
    FormatPtr fmt(rawFmt);

    ret = avformat_find_stream_info(fmt.get(), nullptr);
    if (ret < 0) {
        return { {"error", errorString(ret)} };
    }

    AVStream* videoStream = nullptr;
    AVStream* audioStream = nullptr;
    int videoCount = 0, audioCount = 0, subtitleCount = 0;
    bool coverArt = false;
    nlohmann::json streams = nlohmann::json::array();

    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        AVStream* st = fmt->streams[i];
        streams.push_back(streamInfo(fmt.get(), st));
        switch (st->codecpar->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                    coverArt = true;
                } else {
                    ++videoCount;
                    if (!videoStream) videoStream = st;
                }
                break;
            case AVMEDIA_TYPE_AUDIO:
                ++audioCount;
                if (!audioStream) audioStream = st;
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                ++subtitleCount;
                break;
            default:
                break;
        }
    }

    const bool packets = level >= AnalysisLevel::Packets;
    const bool deep = level >= AnalysisLevel::Deep;
    const bool image = type.mime.rfind("image/", 0) == 0;
    std::string kind = image ? "image" : videoStream ? "video" : audioStream ? "audio" : "unknown";
    double duration = fmt->duration != AV_NOPTS_VALUE ? fmt->duration / static_cast<double>(AV_TIME_BASE) : 0.0;
    double videoDuration = videoStream ? streamDuration(videoStream, duration) : 0.0;

    nlohmann::json j;
    j["kind"] = kind;
    j["container"] = nameOrNull(fmt->iformat->name);
    j["containerLongName"] = nameOrNull(fmt->iformat->long_name);
    j["duration"] = !image && duration > 0 ? nlohmann::json(round3(duration)) : nlohmann::json(nullptr);
    j["bitrate"] = !image && fmt->bit_rate > 0 ? nlohmann::json(fmt->bit_rate) : nlohmann::json(nullptr);
    j["tags"] = dictToJson(fmt->metadata);
    j["chapters"] = fmt->nb_chapters;
    j["videoStreams"] = videoCount;
    j["audioStreams"] = audioCount;
    j["subtitleStreams"] = subtitleCount;
    j["hasCoverArt"] = coverArt;
    j["streams"] = streams;

    // Images are always decoded once for their embedded metadata and perceptual hash.
    std::unique_ptr<VideoDecoder> video;
    std::unique_ptr<AudioDecoder> audio;
    nlohmann::json missingDecoders = nlohmann::json::array();

    if (videoStream && (image || deep)) {
        video = std::make_unique<VideoDecoder>(videoStream, videoDuration);
        if (!video->isOpen()) {
            missingDecoders.push_back(streams[videoStream->index]["codec"]);
            video.reset();
        }
    }
    if (audioStream && deep && !image) {
        audio = std::make_unique<AudioDecoder>(audioStream);
        if (!audio->isOpen()) {
            missingDecoders.push_back(streams[audioStream->index]["codec"]);
            audio.reset();
        }
    }

    // A single pass over the file collects packet stats and feeds the decoders.
    std::vector<PacketStats> packetStats;
    std::string demuxError;
    if (packets || video || audio) {
        for (unsigned i = 0; i < fmt->nb_streams; ++i) {
            AVStream* st = fmt->streams[i];
            bool used = packets || (video && st == video->stream) || (audio && st == audio->stream);
            st->discard = used ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
        }

        PacketPtr pkt(av_packet_alloc());
        FramePtr frame(av_frame_alloc());
        if (!pkt || !frame) throw std::bad_alloc();

        while ((ret = av_read_frame(fmt.get(), pkt.get())) >= 0) {
            if (packets) {
                // Some containers announce streams only while being read.
                while (packetStats.size() <= static_cast<std::size_t>(pkt->stream_index)) {
                    packetStats.emplace_back(fmt->streams[packetStats.size()]);
                }
                packetStats[pkt->stream_index].add(pkt.get());
            }
            if (video && pkt->stream_index == video->stream->index && (deep || video->frames == 0)) {
                video->feed(pkt.get(), frame.get());
            } else if (audio && pkt->stream_index == audio->stream->index) {
                audio->feed(pkt.get(), frame.get());
            }
            av_packet_unref(pkt.get());
            if (!packets && video && video->frames > 0) break;
        }
        if (packets && ret < 0 && ret != AVERROR_EOF) demuxError = errorString(ret);

        if (video && (deep || video->frames == 0)) video->feed(nullptr, frame.get());
        if (audio) audio->feed(nullptr, frame.get());
    }

    if (image && videoStream) {
        const AVCodecParameters* par = videoStream->codecpar;
        nlohmann::json img;
        img["width"] = par->width;
        img["height"] = par->height;
        img["megapixels"] = std::round(par->width * static_cast<double>(par->height) / 1e4) / 100.0;

        // Frame metadata holds EXIF tags for JPEG, TIFF and WebP and text chunks for PNG.
        nlohmann::json metadata = video && video->frames > 0 ? video->firstFrameMetadata : nlohmann::json::object();
        bool gps = false;
        for (auto& [key, value] : metadata.items()) {
            if (key.rfind("GPS", 0) == 0) gps = true;
        }
        img["metadata"] = metadata;
        img["gpsPresent"] = gps;
        img["dhash"] = video && !video->firstFrameHash.empty() ? nlohmann::json(video->firstFrameHash) : nlohmann::json(nullptr);

        // Animated images store one packet per frame.
        if (packets && static_cast<std::size_t>(videoStream->index) < packetStats.size()) {
            uint64_t frameCount = packetStats[videoStream->index].packets;
            img["frameCount"] = frameCount;
            img["animated"] = frameCount > 1;
        }
        j["image"] = img;
    }

    if (!packets) return j;

    nlohmann::json issues = nlohmann::json::array();

    if (videoStream && !image) {
        j["frameHashes"] = deep && video
            ? video->frameHashes
            : seekFrameHashes(fmt.get(), videoStream, videoDuration);
    }

    // Packet level: damaged files are often silently resynced by the demuxer, so the
    // packets are also compared against the frame count and duration of the container.
    {
        nlohmann::json streamStats = nlohmann::json::array();
        uint64_t corruptPackets = 0, timestampErrors = 0, gaps = 0;
        double gapDuration = 0.0, contentEnd = 0.0;
        for (const PacketStats& ps : packetStats) {
            streamStats.push_back(ps.toJson());
            corruptPackets += ps.corruptPackets;
            timestampErrors += ps.timestampErrors;
            gaps += ps.gaps;
            gapDuration += ps.gapDuration;
            AVMediaType t = ps.stream->codecpar->codec_type;
            if (t == AVMEDIA_TYPE_VIDEO || t == AVMEDIA_TYPE_AUDIO) contentEnd = std::max(contentEnd, ps.end());
        }

        const PacketStats* videoPackets = videoStream && static_cast<std::size_t>(videoStream->index) < packetStats.size()
            ? &packetStats[videoStream->index] : nullptr;
        const PacketStats* audioPackets = audioStream && static_cast<std::size_t>(audioStream->index) < packetStats.size()
            ? &packetStats[audioStream->index] : nullptr;

        int64_t expectedFrames = 0;
        bool expectedEstimated = false;
        if (videoStream && !image) {
            AVRational rate = videoStream->avg_frame_rate;
            if (videoStream->nb_frames > 0) {
                expectedFrames = videoStream->nb_frames;
            } else if (rate.num > 0 && rate.den > 0 && videoDuration > 0) {
                // Containers like Matroska store no frame count.
                expectedFrames = std::llround(videoDuration * av_q2d(rate));
                expectedEstimated = true;
            }
        }
        uint64_t videoFrames = videoPackets ? videoPackets->packets : 0;

        nlohmann::json pk;
        pk["demuxError"] = demuxError.empty() ? nlohmann::json(nullptr) : nlohmann::json(demuxError);
        pk["videoFrames"] = videoStream && !image ? nlohmann::json(videoFrames) : nlohmann::json(nullptr);
        pk["expectedVideoFrames"] = expectedFrames > 0 ? nlohmann::json(expectedFrames) : nlohmann::json(nullptr);
        pk["expectedVideoFramesEstimated"] = expectedEstimated;
        pk["contentEnd"] = !image && contentEnd > 0 ? nlohmann::json(round3(contentEnd)) : nlohmann::json(nullptr);
        pk["audioVideoOffset"] = videoPackets && audioPackets && videoPackets->hasTimes() && audioPackets->hasTimes()
            ? nlohmann::json(round3(audioPackets->first() - videoPackets->first())) : nlohmann::json(nullptr);
        pk["streams"] = streamStats;
        j["packets"] = pk;

        auto count = [](uint64_t n, const char* what) { return std::to_string(n) + " " + what; };
        char buf[128];
        if (!demuxError.empty()) issues.push_back("Demux error: " + demuxError);
        if (corruptPackets) issues.push_back(count(corruptPackets, "corrupt packets"));
        if (expectedFrames > 0 && videoFrames < expectedFrames * (1.0 - MISSING_TOLERANCE) - 1) {
            issues.push_back("Video has " + std::to_string(videoFrames) + " of " + std::to_string(expectedFrames) + " frames");
        }
        if (!image && duration > 0 && contentEnd < duration - std::max(MISSING_DURATION_MIN, duration * MISSING_TOLERANCE)) {
            std::snprintf(buf, sizeof(buf), "Content ends at %.1f s of %.1f s", contentEnd, duration);
            issues.push_back(buf);
        }
        if (gaps) {
            std::snprintf(buf, sizeof(buf), "%llu timestamp gaps (%.1f s)", static_cast<unsigned long long>(gaps), gapDuration);
            issues.push_back(buf);
        }
        if (timestampErrors) issues.push_back(count(timestampErrors, "non-monotonic timestamps"));
    }

    // Decode level.
    if (deep) {
        uint64_t errors = (video ? video->errors : 0) + (audio ? audio->errors : 0);
        uint64_t corrupt = (video ? video->corruptFrames : 0) + (audio ? audio->corruptFrames : 0);
        double decodedDuration = std::max(video ? video->decodedUntil : 0.0, audio ? audio->decodedDuration() : 0.0);

        nlohmann::json dec;
        dec["decodeErrors"] = errors;
        dec["corruptFrames"] = corrupt;
        dec["videoFrames"] = video ? nlohmann::json(video->frames) : nlohmann::json(nullptr);
        dec["audioFrames"] = audio ? nlohmann::json(audio->frames) : nlohmann::json(nullptr);
        dec["decodedDuration"] = image ? nlohmann::json(nullptr) : nlohmann::json(round3(decodedDuration));
        dec["missingDecoders"] = missingDecoders;
        if (video && !image) {
            dec["video"] = {
                {"blackFrames", video->blackFrames},
                {"blackFrameRatio", video->frames > 0 ? round3(static_cast<double>(video->blackFrames) / video->frames) : 0.0}
            };
        }
        if (audio) dec["audio"] = audio->levels();
        j["decode"] = dec;

        if (errors) issues.push_back(std::to_string(errors) + " decode errors");
        if (corrupt) issues.push_back(std::to_string(corrupt) + " corrupt frames");
        for (const auto& codec : missingDecoders) {
            issues.push_back("No decoder for " + (codec.is_string() ? codec.get<std::string>() : std::string("unknown codec")));
        }
    }

    j["integrity"] = {
        {"level", analysisLevelName(level)},
        {"complete", issues.empty()},
        {"issues", issues}
    };

    return j;
}
