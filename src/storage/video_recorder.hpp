#pragma once

#include <filesystem>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

class VideoRecorder {
public:
    VideoRecorder(std::filesystem::path outputPath, bool enabled);
    ~VideoRecorder();

    void write(const cv::Mat& frame, double sourceFps);
    void release();

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }
    [[nodiscard]] bool isOpened() const { return writer_.isOpened(); }
    [[nodiscard]] const std::filesystem::path& outputPath() const noexcept { return outputPath_; }

private:
    std::filesystem::path outputPath_;
    bool enabled_;
    cv::VideoWriter writer_;
};
