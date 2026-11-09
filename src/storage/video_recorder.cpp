#include "video_recorder.hpp"

#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

VideoRecorder::VideoRecorder(fs::path outputPath, bool enabled)
    : outputPath_(std::move(outputPath)), enabled_(enabled) {
    if (enabled_) {
        std::error_code ec;
        fs::create_directories(outputPath_.parent_path(), ec);
    }
}

VideoRecorder::~VideoRecorder() {
    release();
}

void VideoRecorder::write(const cv::Mat& frame, double sourceFps) {
    if (!enabled_ || frame.empty()) {
        return;
    }

    if (!writer_.isOpened()) {
        double fps = sourceFps;
        if (fps <= 0.0 || fps > 120.0) {
            fps = 25.0;
        }
        writer_.open(outputPath_.string(), cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                    fps, frame.size());
        if (!writer_.isOpened()) {
            throw std::runtime_error("Gagal membuat video rekaman: " + outputPath_.string());
        }
    }

    writer_.write(frame);
}

void VideoRecorder::release() {
    if (writer_.isOpened()) {
        writer_.release();
    }
}
