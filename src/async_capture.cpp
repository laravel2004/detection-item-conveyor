#include "async_capture.hpp"

#include <opencv2/videoio.hpp>

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace {

cv::VideoCapture openRtsp(const std::string& source) {
#ifdef _WIN32
    _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp");
#else
    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp", 0);
#endif
    const std::vector<int> parameters{
        cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
        cv::CAP_PROP_READ_TIMEOUT_MSEC, 5000,
    };
    cv::VideoCapture capture(source, cv::CAP_FFMPEG, parameters);
    capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
    return capture;
}

}  // namespace

AsyncCapture::AsyncCapture(std::string source)
    : source_(std::move(source)), worker_(&AsyncCapture::run, this) {}

AsyncCapture::~AsyncCapture() {
    stop_.store(true);
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool AsyncCapture::waitForFrame(cv::Mat& frame,
                                std::uint64_t& lastSequence,
                                std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    const bool ready = condition_.wait_for(lock, timeout, [&] {
        return stop_.load() || sequence_ > lastSequence;
    });
    if (!ready || stop_.load() || sequence_ <= lastSequence || !latestFrame_) {
        return false;
    }
    frame = *latestFrame_;
    lastSequence = sequence_;
    return true;
}

void AsyncCapture::run() {
    while (!stop_.load()) {
        cv::VideoCapture capture = openRtsp(source_);
        if (!capture.isOpened()) {
            std::cerr << "RTSP belum tersambung; mencoba lagi dalam 2 detik...\n";
            std::unique_lock lock(mutex_);
            condition_.wait_for(lock, std::chrono::seconds(2), [&] {
                return stop_.load();
            });
            continue;
        }

        double fps = capture.get(cv::CAP_PROP_FPS);
        if (fps <= 0.0 || fps > 120.0) {
            fps = 25.0;
        }
        sourceFps_.store(fps);
        const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
        const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
        const int bitrate = static_cast<int>(capture.get(cv::CAP_PROP_BITRATE));
        const int fourcc = static_cast<int>(capture.get(cv::CAP_PROP_FOURCC));
        std::string codec;
        for (int byte = 0; byte < 4; ++byte) {
            const char character = static_cast<char>((fourcc >> (byte * 8)) & 0xFF);
            if (character >= 32 && character <= 126) {
                codec.push_back(character);
            }
        }
        if (codec.empty()) {
            codec = "unknown";
        }
        std::cout << "RTSP tersambung. Stream: " << width << 'x' << height
                  << " | FPS: " << fps << " | codec: " << codec;
        if (bitrate > 0) {
            std::cout << " | bitrate: " << bitrate << " kbps";
        }
        std::cout << '\n';

        while (!stop_.load()) {
            cv::Mat frame;
            if (!capture.read(frame) || frame.empty()) {
                std::cerr << "Frame RTSP terputus; menghubungkan ulang...\n";
                break;
            }
            {
                std::lock_guard lock(mutex_);
                latestFrame_ = std::make_shared<cv::Mat>(std::move(frame));
                ++sequence_;
            }
            condition_.notify_one();
        }
        capture.release();
    }
}
