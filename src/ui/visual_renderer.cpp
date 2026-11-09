#include "visual_renderer.hpp"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {
const cv::Scalar kNewColor{0, 0, 255};          // Merah
const cv::Scalar kPendingColor{0, 255, 255};    // Kuning
const cv::Scalar kCountedColor{0, 255, 0};      // Hijau
const cv::Scalar kLowConfColor{0, 165, 255};    // Oranye / Amber
}  // namespace

VisualRenderer::VisualRenderer(float firstLineRatio,
                               float secondLineRatio,
                               float lineStartRatio,
                               float lineEndRatio)
    : firstLineRatio_(firstLineRatio),
      secondLineRatio_(secondLineRatio),
      lineStartRatio_(lineStartRatio),
      lineEndRatio_(lineEndRatio) {}

float VisualRenderer::firstLine(int frameHeight) const {
    return frameHeight * firstLineRatio_;
}

float VisualRenderer::secondLine(int frameHeight) const {
    return frameHeight * secondLineRatio_;
}

float VisualRenderer::xStart(int frameWidth) const {
    return frameWidth * lineStartRatio_;
}

float VisualRenderer::xEnd(int frameWidth) const {
    return frameWidth * lineEndRatio_;
}

void VisualRenderer::drawLabel(cv::Mat& image,
                               int x,
                               int y,
                               const std::string& label,
                               const cv::Scalar& color) {
    int baseline = 0;
    const auto size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.55, 2, &baseline);
    const int top = std::max(0, y - size.height - baseline - 8);
    cv::rectangle(image, {x, top}, {x + size.width + 8, y}, color, cv::FILLED);
    cv::putText(image, label, {x + 4, y - baseline - 4}, cv::FONT_HERSHEY_SIMPLEX,
                0.55, {0, 0, 0}, 2, cv::LINE_AA);
}

void VisualRenderer::render(cv::Mat& annotated,
                            const std::vector<Detection>& detections,
                            const TrackingResult& tracking,
                            int totalSack,
                            double displayedFps,
                            double sourceFps) {
    const float line1 = firstLine(annotated.rows);
    const float line2 = secondLine(annotated.rows);
    const float x1 = xStart(annotated.cols);
    const float x2 = xEnd(annotated.cols);

    // 1. Gambar Bounding Box Deteksi
    for (std::size_t index = 0; index < detections.size(); ++index) {
        const auto& detection = detections[index];
        if (detection.confidence < minDisplayConfidence_) {
            continue;
        }
        const bool isLowConfidence = detection.confidence < solidDisplayConfidence_;

        cv::Scalar color = kNewColor;
        std::string status = "NEW";
        std::string id = "-";
        if (tracking.assignments[index]) {
            const auto& track = *tracking.assignments[index];
            if (track.counted) {
                color = kCountedColor;
                status = "COUNTED";
            } else if (track.pending) {
                color = kPendingColor;
                status = "CHECKPOINT";
            }
            if (track.sackId > 0) {
                id = std::to_string(track.sackId);
            }
        }

        const cv::Rect box = detection.box;
        const cv::Scalar boxColor = isLowConfidence ? kLowConfColor : color;
        const int boxThickness = isLowConfidence ? 1 : 3;
        cv::rectangle(annotated, box, boxColor, boxThickness);

        std::ostringstream label;
        if (id != "-") {
            label << "sack_id: " << id << " | ";
        }
        label << "conf: " << std::fixed << std::setprecision(2) << detection.confidence;
        if (isLowConfidence) {
            label << " [LOW]";
        }
        label << " | " << status;

        drawLabel(annotated, box.x, box.y, label.str(), boxColor);
        cv::circle(annotated,
                   {box.x + box.width / 2, box.y + box.height / 2},
                   4, {255, 0, 255}, cv::FILLED);
    }

    // 2. Gambar Garis Checkpoint dan Garis Hitung
    cv::line(annotated, {static_cast<int>(x1), static_cast<int>(line1)},
             {static_cast<int>(x2), static_cast<int>(line1)},
             kPendingColor, 4);
    cv::line(annotated, {static_cast<int>(x1), static_cast<int>(line2)},
             {static_cast<int>(x2), static_cast<int>(line2)},
             kCountedColor, 4);

    cv::putText(annotated, "LINE 1: CHECKPOINT",
                {static_cast<int>(x1), std::max(30, static_cast<int>(line1) - 12)},
                cv::FONT_HERSHEY_SIMPLEX, 0.7, kPendingColor, 2, cv::LINE_AA);
    cv::putText(annotated, "LINE 2: COUNT",
                {static_cast<int>(x1), std::max(30, static_cast<int>(line2) - 12)},
                cv::FONT_HERSHEY_SIMPLEX, 0.7, kCountedColor, 2, cv::LINE_AA);

    // 3. Teks Total Sak & FPS
    cv::putText(annotated, "TOTAL SACK: " + std::to_string(totalSack),
                {40, annotated.rows - 40}, cv::FONT_HERSHEY_SIMPLEX, 1.5,
                kCountedColor, 4, cv::LINE_AA);

    std::ostringstream fpsLabel;
    fpsLabel << "FPS: " << std::fixed << std::setprecision(1) << displayedFps
             << " | SOURCE: " << sourceFps;
    cv::putText(annotated, fpsLabel.str(), {40, annotated.rows - 95},
                cv::FONT_HERSHEY_SIMPLEX, 1.0, {255, 255, 255}, 3, cv::LINE_AA);
}
