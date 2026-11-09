#pragma once

#include "stable_tracker.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class YoloDetector {
public:
    YoloDetector(const std::filesystem::path& modelPath,
                 std::string device,
                 float confidenceThreshold = 0.10F,
                 float nmsThreshold = 0.45F,
                 int inputSize = 640);
    ~YoloDetector();

    YoloDetector(const YoloDetector&) = delete;
    YoloDetector& operator=(const YoloDetector&) = delete;
    YoloDetector(YoloDetector&&) noexcept;
    YoloDetector& operator=(YoloDetector&&) noexcept;

    std::vector<Detection> detect(const cv::Mat& frame);

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
