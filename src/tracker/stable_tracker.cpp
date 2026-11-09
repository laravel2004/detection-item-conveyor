#include "stable_tracker.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace {

cv::Point2f centerOf(const cv::Rect2f& box) {
    return {box.x + box.width * 0.5F, box.y + box.height * 0.5F};
}

bool inRange(float value, float low, float high) {
    return value >= low && value <= high;
}

float calculateIoU(const cv::Rect2f& a, const cv::Rect2f& b) {
    const float x1 = std::max(a.x, b.x);
    const float y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.width, b.x + b.width);
    const float y2 = std::min(a.y + a.height, b.y + b.height);
    const float intersection = std::max(0.0F, x2 - x1) * std::max(0.0F, y2 - y1);
    const float areaA = a.width * a.height;
    const float areaB = b.width * b.height;
    const float unionArea = areaA + areaB - intersection;
    return unionArea > 0.0F ? (intersection / unionArea) : 0.0F;
}

}  // namespace

StableTracker::StableTracker(double retentionSeconds,
                             double matchGapSeconds,
                             int minimumFrames,
                             int confirmationFrames,
                             float newTrackConfidence)
    : retentionSeconds_(retentionSeconds),
      matchGapSeconds_(matchGapSeconds),
      minimumFrames_(minimumFrames),
      confirmationFrames_(confirmationFrames),
      newTrackConfidence_(newTrackConfidence) {}

std::optional<float> StableTracker::matchCost(
    const Track& track, const Detection& detection, double nowSeconds) const {
    const double elapsed = nowSeconds - track.lastSeen;
    if (elapsed <= 0.0 || elapsed > matchGapSeconds_) {
        return std::nullopt;
    }

    const auto& oldBox = track.observation.box;
    const auto& newBox = detection.box;
    if (oldBox.width <= 0.0F || oldBox.height <= 0.0F ||
        newBox.width <= 0.0F || newBox.height <= 0.0F) {
        return std::nullopt;
    }

    float appearanceDistance = 0.0F;
    if (!track.observation.appearance.empty() && !detection.appearance.empty()) {
        appearanceDistance = static_cast<float>(
            cv::norm(track.observation.appearance, detection.appearance, cv::NORM_L1) * 0.5);
        if (appearanceDistance > 0.40F) {
            return std::nullopt;
        }
    }

    const float iou = calculateIoU(oldBox, newBox);
    if (iou >= 0.30F) {
        return (1.0F - iou) + appearanceDistance * 0.5F;
    }

    const float widthSimilarity =
        std::min(oldBox.width, newBox.width) / std::max(oldBox.width, newBox.width);
    const float heightSimilarity =
        std::min(oldBox.height, newBox.height) / std::max(oldBox.height, newBox.height);
    const float minimumSizeSimilarity = elapsed > 0.5 ? 0.35F : 0.45F;
    if (widthSimilarity < minimumSizeSimilarity ||
        heightSimilarity < minimumSizeSimilarity) {
        return std::nullopt;
    }

    const auto oldCenter = centerOf(oldBox);
    const auto newCenter = centerOf(newBox);
    const float horizon = static_cast<float>(std::min(elapsed, 1.5));
    const auto predicted = oldCenter + track.velocity * horizon;
    const auto normalizedDistance = [&](const cv::Point2f& reference) {
        const float dx = (newCenter.x - reference.x) / oldBox.width;
        const float dy = (newCenter.y - reference.y) / oldBox.height;
        return std::sqrt(dx * dx + dy * dy);
    };
    const float distance = std::min(normalizedDistance(predicted),
                                    normalizedDistance(oldCenter));
    const float distanceLimit = 0.65F + static_cast<float>(std::min(elapsed, 1.5)) * 0.15F;
    if (distance > distanceLimit) {
        return std::nullopt;
    }

    return distance + appearanceDistance - (iou * 0.5F);
}

TrackingResult StableTracker::update(const std::vector<Detection>& detections,
                                     double nowSeconds,
                                     float firstLineY,
                                     float secondLineY,
                                     float xStart,
                                     float xEnd,
                                     float margin) {
    for (auto iterator = tracks_.begin(); iterator != tracks_.end();) {
        if (nowSeconds - iterator->second.lastSeen > retentionSeconds_) {
            iterator = tracks_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    struct Edge {
        float cost;
        std::size_t detectionIndex;
        int trackId;
    };
    std::vector<Edge> edges;
    for (std::size_t detectionIndex = 0; detectionIndex < detections.size();
         ++detectionIndex) {
        for (const auto& [trackId, track] : tracks_) {
            if (const auto cost = matchCost(track, detections[detectionIndex], nowSeconds)) {
                edges.push_back({*cost, detectionIndex, trackId});
            }
        }
    }
    std::sort(edges.begin(), edges.end(), [](const Edge& left, const Edge& right) {
        return left.cost < right.cost;
    });

    std::unordered_map<std::size_t, int> assigned;
    std::unordered_set<int> usedTracks;
    for (const auto& edge : edges) {
        if (assigned.count(edge.detectionIndex) != 0 ||
            usedTracks.count(edge.trackId) != 0) {
            continue;
        }
        assigned[edge.detectionIndex] = edge.trackId;
        usedTracks.insert(edge.trackId);
    }

    // Spatial Fallback: warisi track checkpoint jika ada deteksi unassigned di posisi karung checkpoint
    for (std::size_t index = 0; index < detections.size(); ++index) {
        if (assigned.count(index) != 0) {
            continue;
        }
        const auto& detection = detections[index];
        const auto newCenter = centerOf(detection.box);
        int inheritedTrackId = 0;
        float bestDist = std::numeric_limits<float>::infinity();

        for (const auto& [trackId, track] : tracks_) {
            if (usedTracks.count(trackId) != 0) {
                continue;
            }
            if (!track.pending && !track.counted) {
                continue; // Hanya warisi dari track yang sudah checkpoint atau counted
            }
            const auto oldCenter = centerOf(track.observation.box);
            const float dx = std::abs(newCenter.x - oldCenter.x);
            const float dy = newCenter.y - oldCenter.y;
            // Dalam jangkauan spasial yang wajar dan bergerak searah aliran konveyor
            if (dx <= std::max(detection.box.width, track.observation.box.width) * 1.5F &&
                dy >= -30.0F && dy <= track.observation.box.height * 2.0F) {
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < bestDist) {
                    bestDist = dist;
                    inheritedTrackId = trackId;
                }
            }
        }
        if (inheritedTrackId > 0) {
            assigned[index] = inheritedTrackId;
            usedTracks.insert(inheritedTrackId);
        }
    }

    TrackingResult result;
    result.assignments.resize(detections.size());
    for (std::size_t index = 0; index < detections.size(); ++index) {
        const auto& detection = detections[index];
        int trackId = 0;
        if (const auto match = assigned.find(index); match != assigned.end()) {
            trackId = match->second;
        } else if (detection.confidence >= newTrackConfidence_) {
            trackId = nextTrackId_++;
            Track newTrack{trackId, 0, detection, nowSeconds};
            const auto center = centerOf(detection.box);
            newTrack.startY = center.y;
            newTrack.maxY = center.y;
            tracks_.emplace(trackId, std::move(newTrack));
        } else {
            continue;
        }

        auto& track = tracks_.at(trackId);
        const double elapsed = nowSeconds - track.lastSeen;
        const auto previousCenter = centerOf(track.observation.box);
        const auto currentCenter = centerOf(detection.box);
        if (track.startY <= 0.0F) {
            track.startY = currentCenter.y;
        }
        track.maxY = std::max(track.maxY, currentCenter.y);
        bool crossedFirst = false;
        if (elapsed > 0.0 && previousCenter.y < firstLineY &&
            currentCenter.y >= firstLineY && currentCenter.y != previousCenter.y) {
            const float fraction =
                (firstLineY - previousCenter.y) / (currentCenter.y - previousCenter.y);
            const float crossingX =
                previousCenter.x + (currentCenter.x - previousCenter.x) * fraction;
            crossedFirst = inRange(crossingX, xStart, xEnd);
        }

        if (elapsed > 0.0) {
            const cv::Point2f measuredVelocity =
                (currentCenter - previousCenter) / static_cast<float>(elapsed);
            if (track.frames > 0) {
                track.velocity = track.velocity * 0.65F + measuredVelocity * 0.35F;
            } else {
                track.velocity = measuredVelocity;
            }
        }
        track.observation = detection;
        track.lastSeen = nowSeconds;
        ++track.frames;

        // Dynamic lane margin: toleransi karung miring atau di tepi konveyor
        const float laneMarginX = std::max(margin, detection.box.width * 0.35F);
        const bool inLane = (currentCenter.x >= xStart - laneMarginX) &&
                            (currentCenter.x <= xEnd + laneMarginX);

        if (currentCenter.y <= secondLineY - margin) {
            track.seenAboveCount = true;
        }
        // State Immutability: sekali pending, tetap pending!
        // Karung masuk checkpoint jika melintasi Garis 1 atau pusatnya berada di antara Garis 1 dan Garis 2
        if (crossedFirst ||
            (inLane && currentCenter.y >= firstLineY && currentCenter.y < secondLineY) ||
            (currentCenter.y >= firstLineY && currentCenter.y < secondLineY)) {
            track.pending = true;
        }
        if ((track.pending || track.counted) && track.sackId == 0) {
            track.sackId = nextSackId_++;
        }

        // Garis 2 HANYA dilewati jika PUSAT karung (center) mencapai atau melintasi Garis 2
        bool crossedSecond = (currentCenter.y >= secondLineY + margin);
        if (elapsed > 0.0 && previousCenter.y < secondLineY && currentCenter.y >= secondLineY) {
            crossedSecond = true;
        }

        if (track.pending && inLane && crossedSecond) {
            ++track.belowFrames;
        } else {
            track.belowFrames = 0;
        }

        const bool crossedCheckpoint = track.pending && track.seenAboveCount;
        const bool movingDownwards = track.velocity.y >= -15.0F;
        const bool confirmedBelow = track.belowFrames >= confirmationFrames_;

        if (!track.counted && crossedCheckpoint && movingDownwards &&
            track.frames >= minimumFrames_ && confirmedBelow) {
            track.counted = true;
            ++total_;
            result.countEvents.push_back(track.sackId);
        }
        result.assignments[index] =
            TrackView{track.trackId, track.sackId, detection, track.pending, track.counted};
    }
    return result;
}

cv::Mat appearanceDescriptor(const cv::Mat& frame, const cv::Rect2f& box) {
    const cv::Rect frameBounds{0, 0, frame.cols, frame.rows};
    cv::Rect region{
        static_cast<int>(std::floor(box.x)),
        static_cast<int>(std::floor(box.y)),
        static_cast<int>(std::ceil(box.width)),
        static_cast<int>(std::ceil(box.height)),
    };
    region &= frameBounds;
    if (region.empty()) {
        return {};
    }
    cv::Mat hsv;
    cv::cvtColor(frame(region), hsv, cv::COLOR_BGR2HSV);
    const int channels[] = {0, 1, 2};
    const int histogramSize[] = {8, 4, 4};
    const float hueRange[] = {0.0F, 180.0F};
    const float saturationRange[] = {0.0F, 256.0F};
    const float valueRange[] = {0.0F, 256.0F};
    const float* ranges[] = {hueRange, saturationRange, valueRange};
    cv::Mat histogram;
    cv::calcHist(&hsv, 1, channels, cv::Mat(), histogram, 3, histogramSize, ranges);
    histogram = histogram.reshape(1, 1);
    const double sum = cv::sum(histogram)[0];
    if (sum > 0.0) {
        histogram /= sum;
    }
    return histogram;
}
