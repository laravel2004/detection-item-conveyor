#pragma once

#include <opencv2/core.hpp>

#include <optional>
#include <unordered_map>
#include <vector>

struct Detection {
    cv::Rect2f box;
    float confidence = 0.0F;
    cv::Mat appearance;
};

struct TrackView {
    int trackId = 0;
    int sackId = 0;
    Detection detection;
    bool pending = false;
    bool counted = false;
};

struct TrackingResult {
    std::vector<std::optional<TrackView>> assignments;
    std::vector<int> countEvents;
};

class StableTracker {
public:
    StableTracker(double retentionSeconds = 30.0,
                  double matchGapSeconds = 15.0,
                  int minimumFrames = 2,
                  int confirmationFrames = 1,
                  float newTrackConfidence = 0.45F);

    TrackingResult update(const std::vector<Detection>& detections,
                          double nowSeconds,
                          float firstLineY,
                          float secondLineY,
                          float xStart,
                          float xEnd,
                          float margin = 3.0F);

    [[nodiscard]] int total() const noexcept { return total_; }

private:
    struct Track {
        int trackId = 0;
        int sackId = 0;
        Detection observation;
        double lastSeen = 0.0;
        cv::Point2f velocity{0.0F, 0.0F};
        int frames = 0;
        bool pending = false;
        bool counted = false;
        bool seenAboveCount = false;
        int belowFrames = 0;
        float startY = 0.0F;
        float maxY = 0.0F;
    };

    [[nodiscard]] std::optional<float> matchCost(
        const Track& track, const Detection& detection, double nowSeconds) const;

    double retentionSeconds_;
    double matchGapSeconds_;
    int minimumFrames_;
    int confirmationFrames_;
    float newTrackConfidence_;
    std::unordered_map<int, Track> tracks_;
    int nextTrackId_ = 1;
    int nextSackId_ = 1;
    int total_ = 0;
};

cv::Mat appearanceDescriptor(const cv::Mat& frame, const cv::Rect2f& box);
