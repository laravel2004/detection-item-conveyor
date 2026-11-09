#include "sack_counter_app.hpp"

#include "async_capture.hpp"
#include "count_logger.hpp"
#include "path_resolver.hpp"
#include "stable_tracker.hpp"
#include "ui_window.hpp"
#include "video_recorder.hpp"
#include "visual_renderer.hpp"
#include "yolo_detector.hpp"

#include <opencv2/videoio.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

bool isNetworkSource(const std::string& source) {
    return source.rfind("rtsp://", 0) == 0 || source.rfind("rtsps://", 0) == 0;
}

cv::VideoCapture openLocalCapture(const std::string& source) {
#ifdef _WIN32
    _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp");
#else
    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp", 0);
#endif
    cv::VideoCapture capture(source, cv::CAP_FFMPEG);
    capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
    return capture;
}

std::string formatTimestamp(const std::chrono::system_clock::time_point& point, const char* format) {
    const auto time = std::chrono::system_clock::to_time_t(point);
    std::tm calendar{};
#ifdef _WIN32
    localtime_s(&calendar, &time);
#else
    localtime_r(&time, &calendar);
#endif
    std::ostringstream stream;
    stream << std::put_time(&calendar, format);
    return stream.str();
}

}  // namespace

SackCounterApp::SackCounterApp(AppConfig config) : config_(std::move(config)) {}

SackCounterApp::~SackCounterApp() = default;

int SackCounterApp::run() {
    const fs::path projectRoot = findProjectRoot();
    config_.model = resolveModelPath(config_.model, projectRoot);

    if (!fs::is_regular_file(config_.model)) {
        throw std::runtime_error("Model tidak ditemukan: " + config_.model.string());
    }

    // 1. Inisialisasi Komponen Pipeline
    YoloDetector detector(config_.model, config_.device);
    StableTracker tracker;
    VisualRenderer renderer;
    CountLogger logger(projectRoot, config_.source);

    const auto sessionTime = std::chrono::system_clock::now();
    const fs::path workspaceRoot =
        projectRoot.has_parent_path() ? projectRoot.parent_path() : projectRoot;
    const fs::path outputVideoPath = workspaceRoot / "runs" / "detect" /
        ("cpp_rtsp_sack_counted_" + formatTimestamp(sessionTime, "%Y%m%d_%H%M%S") + ".mp4");

    VideoRecorder recorder(outputVideoPath, config_.save);
    UIWindow window("C++ RTSP Sack Detection & Counting", config_.display, config_.displayWidth);

    // 2. Inisialisasi Sumber Video / RTSP
    const bool networkSource = isNetworkSource(config_.source);
    std::unique_ptr<AsyncCapture> asyncCapture;
    cv::VideoCapture localCapture;
    std::uint64_t captureSequence = 0;

    if (networkSource) {
        asyncCapture = std::make_unique<AsyncCapture>(config_.source);
    }

    std::cout << "Model: " << config_.model.string() << "\nMenghubungkan ke sumber...\n";
    if (config_.display) {
        std::cout << "Kontrol Keyboard saat preview:\n"
                  << "  [Q/ESC] Keluar | [H] Hide/Unhide | [M] Minimize | [F] Fullscreen\n";
    }

    // 3. Loop Eksekusi Pipeline Frame
    int frameIndex = 0;
    int fpsWindowFrames = 0;
    double displayedFps = 0.0;
    auto fpsWindowStart = Clock::now();

    while (config_.maxFrames <= 0 || frameIndex < config_.maxFrames) {
        if (!networkSource && !localCapture.isOpened()) {
            localCapture = openLocalCapture(config_.source);
            if (!localCapture.isOpened()) {
                throw std::runtime_error("Gagal membuka video: " + config_.source);
            }
            std::cout << "Sumber tersambung. Tekan Q untuk berhenti.\n";
        }

        cv::Mat frame;
        if (networkSource) {
            if (!asyncCapture->waitForFrame(frame, captureSequence, std::chrono::milliseconds(6000))) {
                continue;
            }
        } else if (!localCapture.read(frame) || frame.empty()) {
            localCapture.release();
            if (!networkSource) {
                break;
            }
        }
        ++frameIndex;

        // Logging Period
        const auto wallTime = std::chrono::system_clock::now();
        logger.updatePeriod(wallTime);

        // Tahap Pipeline 1: YOLO Detection
        const auto detections = detector.detect(frame);

        // Update FPS Metrik
        ++fpsWindowFrames;
        const auto fpsNow = Clock::now();
        const double fpsWindowSeconds =
            std::chrono::duration<double>(fpsNow - fpsWindowStart).count();
        if (fpsWindowSeconds >= 1.0) {
            displayedFps = fpsWindowFrames / fpsWindowSeconds;
            fpsWindowFrames = 0;
            fpsWindowStart = fpsNow;
        }

        const double sourceFps = networkSource
                                     ? asyncCapture->sourceFps()
                                     : localCapture.get(cv::CAP_PROP_FPS);

        // Tahap Pipeline 2: Stable Tracker
        const float line1 = renderer.firstLine(frame.rows);
        const float line2 = renderer.secondLine(frame.rows);
        const float x1 = renderer.xStart(frame.cols);
        const float x2 = renderer.xEnd(frame.cols);
        const double nowSeconds =
            std::chrono::duration<double>(Clock::now().time_since_epoch()).count();

        const auto tracking = tracker.update(
            detections, nowSeconds, line1, line2, x1, x2,
            std::max(3.0F, frame.rows * 0.003F));

        // Catat Event Hitung
        int eventTotal = tracker.total() - static_cast<int>(tracking.countEvents.size());
        for (const int sackId : tracking.countEvents) {
            ++eventTotal;
            logger.logCount(eventTotal, sackId, frameIndex, wallTime);
            std::cout << "Sack #" << eventTotal << " terhitung (sack_id " << sackId << ")\n";
        }

        // Tahap Pipeline 3: Visual Renderer
        cv::Mat annotated = frame.clone();
        renderer.render(annotated, detections, tracking, tracker.total(), displayedFps, sourceFps);

        // Tahap Pipeline 4: Video Recorder
        recorder.write(annotated, sourceFps);

        // Tahap Pipeline 5: UI Window & Event Handling
        if (!window.processFrame(annotated)) {
            break; // User requested quit (Q, ESC, atau klik X)
        }
    }

    // Cleanup & Summary
    asyncCapture.reset();
    localCapture.release();
    recorder.release();

    std::cout << "Total sack terhitung: " << tracker.total() << '\n';
    if (config_.save) {
        std::cout << "Rekaman: " << recorder.outputPath().string() << '\n';
    }

    return 0;
}
