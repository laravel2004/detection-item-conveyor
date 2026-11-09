#pragma once

#include "stable_tracker.hpp"
#include <opencv2/core.hpp>
#include <vector>

class VisualRenderer {
public:
    VisualRenderer(float firstLineRatio = 0.72F,
                   float secondLineRatio = 0.88F,
                   float lineStartRatio = 0.41F,
                   float lineEndRatio = 0.53F);

    void render(cv::Mat& annotated,
                const std::vector<Detection>& detections,
                const TrackingResult& tracking,
                int totalSack,
                double displayedFps,
                double sourceFps);

    [[nodiscard]] float firstLine(int frameHeight) const;
    [[nodiscard]] float secondLine(int frameHeight) const;
    [[nodiscard]] float xStart(int frameWidth) const;
    [[nodiscard]] float xEnd(int frameWidth) const;

private:
    void drawLabel(cv::Mat& image,
                   int x,
                   int y,
                   const std::string& label,
                   const cv::Scalar& color);

    float firstLineRatio_;
    float secondLineRatio_;
    float lineStartRatio_;
    float lineEndRatio_;
    float minDisplayConfidence_ = 0.10F;
    float solidDisplayConfidence_ = 0.30F;
};
