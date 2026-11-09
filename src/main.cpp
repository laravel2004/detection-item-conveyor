#include "async_capture.hpp"
#include "report_paths.hpp"
#include "stable_tracker.hpp"
#include "yolo_detector.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <conio.h>
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr float kFirstLineRatio = 0.72F;
constexpr float kSecondLineRatio = 0.88F;
constexpr float kLineStartRatio = 0.41F;
constexpr float kLineEndRatio = 0.53F;
constexpr float kMinDisplayConfidence = 0.10F;
constexpr float kSolidDisplayConfidence = 0.30F;
const cv::Scalar kNewColor{0, 0, 255};
const cv::Scalar kPendingColor{0, 255, 255};
const cv::Scalar kCountedColor{0, 255, 0};
const cv::Scalar kLowConfColor{0, 165, 255};

struct Options {
    std::string source;
    fs::path model;
    std::string device = "gpu";
    bool save = true;
    bool display = true;
    int displayWidth = 1600;
    int maxFrames = 0;
};

fs::path getExecutablePath() {
#ifdef _WIN32
    std::wstring buffer;
    buffer.resize(MAX_PATH);
    while (true) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    std::error_code ec;
    auto path = fs::canonical("/proc/self/exe", ec);
    if (!ec) {
        return path;
    }
    return {};
#endif
}

fs::path findProjectRoot() {
    std::error_code ec;
    // 1. Check current working directory
    const auto current = fs::current_path(ec);
    if (!ec) {
        if (fs::is_regular_file(current / "best.onnx", ec) ||
            fs::is_regular_file(current / "CMakeLists.txt", ec) ||
            fs::is_directory(current / "report", ec)) {
            return current;
        }
    }

    // 2. Check executable directory and its ancestors
    const auto exePath = getExecutablePath();
    if (!exePath.empty()) {
        auto candidate = exePath.parent_path();
        while (!candidate.empty() && candidate != candidate.parent_path()) {
            if (fs::is_regular_file(candidate / "best.onnx", ec) ||
                fs::is_regular_file(candidate / "CMakeLists.txt", ec) ||
                fs::is_directory(candidate / "report", ec)) {
                return candidate;
            }
            candidate = candidate.parent_path();
        }
    }

    return current;
}

fs::path resolveModelPath(const fs::path& requestedModel, const fs::path& projectRoot) {
    std::error_code ec;

    // If user explicitly specified --model
    if (!requestedModel.empty()) {
        if (fs::is_regular_file(requestedModel, ec)) {
            return requestedModel;
        }
        if (!projectRoot.empty() && fs::is_regular_file(projectRoot / requestedModel, ec)) {
            return projectRoot / requestedModel;
        }
        return requestedModel;
    }

    // Check SACK_MODEL_PATH environment variable
#ifdef _WIN32
    char* envModel = nullptr;
    std::size_t envLength = 0;
    if (_dupenv_s(&envModel, &envLength, "SACK_MODEL_PATH") == 0 && envModel != nullptr) {
        fs::path p(envModel);
        std::free(envModel);
        if (fs::is_regular_file(p, ec)) {
            return p;
        }
    }
#else
    if (const char* envModel = std::getenv("SACK_MODEL_PATH")) {
        fs::path p(envModel);
        if (fs::is_regular_file(p, ec)) {
            return p;
        }
    }
#endif

    // Check current working directory / best.onnx
    const auto current = fs::current_path(ec);
    if (!ec && fs::is_regular_file(current / "best.onnx", ec)) {
        return current / "best.onnx";
    }

    // Check project root / best.onnx
    if (!projectRoot.empty() && fs::is_regular_file(projectRoot / "best.onnx", ec)) {
        return projectRoot / "best.onnx";
    }

    // Check exe directory and ancestors for best.onnx
    const auto exePath = getExecutablePath();
    if (!exePath.empty()) {
        auto candidate = exePath.parent_path();
        while (!candidate.empty() && candidate != candidate.parent_path()) {
            if (fs::is_regular_file(candidate / "best.onnx", ec)) {
                return candidate / "best.onnx";
            }
            candidate = candidate.parent_path();
        }
    }

    if (!projectRoot.empty()) {
        return projectRoot / "best.onnx";
    }
    return "best.onnx";
}

constexpr const char* kWindowName = "C++ RTSP Sack Detection & Counting";

cv::Mat fitFrameToWindow(const cv::Mat& src, int targetWidth, int targetHeight) {
    if (src.empty() || targetWidth <= 0 || targetHeight <= 0) {
        return src;
    }
    const double scale = std::min(
        static_cast<double>(targetWidth) / src.cols,
        static_cast<double>(targetHeight) / src.rows);
    const int newWidth = std::max(1, static_cast<int>(std::round(src.cols * scale)));
    const int newHeight = std::max(1, static_cast<int>(std::round(src.rows * scale)));

    cv::Mat resized;
    cv::resize(src, resized, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_LINEAR);

    if (newWidth == targetWidth && newHeight == targetHeight) {
        return resized;
    }

    cv::Mat canvas = cv::Mat::zeros(cv::Size(targetWidth, targetHeight), src.type());
    const int xOffset = (targetWidth - newWidth) / 2;
    const int yOffset = (targetHeight - newHeight) / 2;
    resized.copyTo(canvas(cv::Rect(xOffset, yOffset, newWidth, newHeight)));
    return canvas;
}

void printUsage() {
    std::cout
        << "Pemakaian:\n"
        << "  sack_rtsp --source <rtsp-url|video> [--model best.onnx] "
           "[--device cpu|gpu] [--display-width N] [--no-save] [--no-display] "
           "[--max-frames N]\n\n"
        << "Kontrol Keyboard saat preview:\n"
        << "  Q / ESC : Keluar dari program\n"
        << "  H       : Sembunyikan (Hide) / Tampilkan (Show) jendela preview\n"
        << "  M       : Minimize jendela ke taskbar\n"
        << "  F       : Toggle Fullscreen\n";
}

Options parseArguments(int argc, char** argv) {
    Options options;
#ifdef _WIN32
    char* environmentSource = nullptr;
    std::size_t environmentSourceLength = 0;
    if (_dupenv_s(&environmentSource, &environmentSourceLength, "SACK_RTSP_URL") == 0 &&
        environmentSource != nullptr) {
        options.source = environmentSource;
        std::free(environmentSource);
    }
#else
    if (const char* environmentSource = std::getenv("SACK_RTSP_URL")) {
        options.source = environmentSource;
    }
#endif
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto valueAfter = [&](const std::string& option) -> std::string {
            if (index + 1 >= argc) {
                throw std::runtime_error("Nilai belum diisi untuk " + option);
            }
            return argv[++index];
        };
        if (argument == "--source") {
            options.source = valueAfter(argument);
        } else if (argument == "--model") {
            options.model = valueAfter(argument);
        } else if (argument == "--device") {
            options.device = valueAfter(argument);
        } else if (argument == "--max-frames") {
            options.maxFrames = std::stoi(valueAfter(argument));
        } else if (argument == "--display-width") {
            options.displayWidth = std::stoi(valueAfter(argument));
        } else if (argument == "--no-save") {
            options.save = false;
        } else if (argument == "--no-display") {
            options.display = false;
        } else if (argument == "--help" || argument == "-h") {
            printUsage();
            std::exit(0);
        } else {
            throw std::runtime_error("Argumen tidak dikenal: " + argument);
        }
    }
    if (options.source.empty()) {
        throw std::runtime_error("RTSP belum diisi. Gunakan --source atau SACK_RTSP_URL.");
    }
    if (options.device == "cuda" || options.device == "0") {
        options.device = "gpu";
    }
    if (options.device != "cpu" && options.device != "gpu") {
        throw std::runtime_error("--device harus cpu atau gpu.");
    }
    if (options.displayWidth < 0) {
        throw std::runtime_error("--display-width tidak boleh negatif.");
    }
    return options;
}

std::tm localTime(std::time_t time) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif
    return result;
}

std::string timestamp(const std::chrono::system_clock::time_point& point,
                      const char* format) {
    const auto time = std::chrono::system_clock::to_time_t(point);
    const auto calendar = localTime(time);
    std::ostringstream stream;
    stream << std::put_time(&calendar, format);
    return stream.str();
}

void appendCount(const fs::path& path, int total, int sackId, int frameIndex) {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now.time_since_epoch()) %
                              1000;
    std::ofstream report(path, std::ios::app);
    report << timestamp(now, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3)
           << std::setfill('0') << milliseconds.count() << " | sack=" << total
           << " | sack_id=" << sackId << " | frame=" << frameIndex << '\n';
}

cv::VideoCapture openCapture(const std::string& source) {
#ifdef _WIN32
    _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp");
#else
    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp", 0);
#endif
    cv::VideoCapture capture(source, cv::CAP_FFMPEG);
    capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
    return capture;
}

void drawLabel(cv::Mat& image,
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

bool isNetworkSource(const std::string& source) {
    return source.rfind("rtsp://", 0) == 0 || source.rfind("rtsps://", 0) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options options = parseArguments(argc, argv);
        const fs::path projectRoot = findProjectRoot();
        options.model = resolveModelPath(options.model, projectRoot);
        if (!fs::is_regular_file(options.model)) {
            throw std::runtime_error("Model tidak ditemukan: " + options.model.string());
        }

        YoloDetector detector(options.model, options.device);
        StableTracker tracker;
        cv::VideoCapture capture;
        std::unique_ptr<AsyncCapture> asyncCapture;
        cv::VideoWriter writer;
        int frameIndex = 0;
        int fpsWindowFrames = 0;
        double displayedFps = 0.0;
        auto fpsWindowStart = Clock::now();
        std::string reportPeriod;
        fs::path currentReport;
        const bool networkSource = isNetworkSource(options.source);
        std::uint64_t captureSequence = 0;
        if (networkSource) {
            asyncCapture = std::make_unique<AsyncCapture>(options.source);
        }
        const auto sessionTime = std::chrono::system_clock::now();
        const fs::path workspaceRoot =
            projectRoot.has_parent_path() ? projectRoot.parent_path() : projectRoot;
        const fs::path outputPath = workspaceRoot / "runs" / "detect" /
            ("cpp_rtsp_sack_counted_" + timestamp(sessionTime, "%Y%m%d_%H%M%S") + ".mp4");
        fs::create_directories(outputPath.parent_path());

        bool isWindowVisible = options.display;
        bool isFullscreen = false;
        bool windowInitialized = false;

        if (options.display) {
            cv::namedWindow(kWindowName, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
            const int initialWidth = options.displayWidth > 0 ? options.displayWidth : 1280;
            const int initialHeight =
                std::max(1, static_cast<int>(std::round(initialWidth * 9.0 / 16.0)));
            cv::resizeWindow(kWindowName, initialWidth, initialHeight);
            windowInitialized = true;
        }

        std::cout << "Model: " << options.model.string() << "\nMenghubungkan ke sumber...\n";
        if (options.display) {
            std::cout << "Kontrol Keyboard saat preview:\n"
                      << "  [Q/ESC] Keluar | [H] Hide/Unhide | [M] Minimize | [F] Fullscreen\n";
        }
        while (options.maxFrames <= 0 || frameIndex < options.maxFrames) {
            if (!networkSource && !capture.isOpened()) {
                capture = openCapture(options.source);
                if (!capture.isOpened()) {
                    throw std::runtime_error("Gagal membuka video: " + options.source);
                }
                std::cout << "Sumber tersambung. Tekan Q untuk berhenti.\n";
            }

            cv::Mat frame;
            if (networkSource) {
                if (!asyncCapture->waitForFrame(
                        frame, captureSequence, std::chrono::milliseconds(6000))) {
                    continue;
                }
            } else if (!capture.read(frame) || frame.empty()) {
                capture.release();
                if (!networkSource) {
                    break;
                }
            }
            ++frameIndex;

            const auto wallTime = std::chrono::system_clock::now();
            const std::string currentPeriod = timestamp(wallTime, "%Y%m%d%H");
            if (currentPeriod != reportPeriod) {
                currentReport = hourlyReportPath(
                    projectRoot, options.source, wallTime);
                std::ofstream(currentReport, std::ios::app);
                reportPeriod = currentPeriod;
                std::cout << "Report jam ini: " << currentReport << '\n';
            }

            const auto detections = detector.detect(frame);
            ++fpsWindowFrames;
            const auto fpsNow = Clock::now();
            const double fpsWindowSeconds =
                std::chrono::duration<double>(fpsNow - fpsWindowStart).count();
            if (fpsWindowSeconds >= 1.0) {
                displayedFps = fpsWindowFrames / fpsWindowSeconds;
                fpsWindowFrames = 0;
                fpsWindowStart = fpsNow;
            }
            const float firstLine = frame.rows * kFirstLineRatio;
            const float secondLine = frame.rows * kSecondLineRatio;
            const float xStart = frame.cols * kLineStartRatio;
            const float xEnd = frame.cols * kLineEndRatio;
            const double nowSeconds = std::chrono::duration<double>(
                Clock::now().time_since_epoch()).count();
            const auto tracking = tracker.update(
                detections, nowSeconds, firstLine, secondLine, xStart, xEnd,
                std::max(3.0F, frame.rows * 0.003F));

            int eventTotal = tracker.total() - static_cast<int>(tracking.countEvents.size());
            for (const int sackId : tracking.countEvents) {
                ++eventTotal;
                appendCount(currentReport, eventTotal, sackId, frameIndex);
                std::cout << "Sack #" << eventTotal << " terhitung (sack_id "
                          << sackId << ")\n";
            }

            cv::Mat annotated = frame.clone();
            for (std::size_t index = 0; index < detections.size(); ++index) {
                const auto& detection = detections[index];
                if (detection.confidence < kMinDisplayConfidence) {
                    continue;
                }
                const bool isLowConfidence = detection.confidence < kSolidDisplayConfidence;

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

            cv::line(annotated, {static_cast<int>(xStart), static_cast<int>(firstLine)},
                     {static_cast<int>(xEnd), static_cast<int>(firstLine)},
                     kPendingColor, 4);
            cv::line(annotated, {static_cast<int>(xStart), static_cast<int>(secondLine)},
                     {static_cast<int>(xEnd), static_cast<int>(secondLine)},
                     kCountedColor, 4);
            cv::putText(annotated, "LINE 1: CHECKPOINT",
                        {static_cast<int>(xStart), std::max(30, static_cast<int>(firstLine) - 12)},
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, kPendingColor, 2, cv::LINE_AA);
            cv::putText(annotated, "LINE 2: COUNT",
                        {static_cast<int>(xStart), std::max(30, static_cast<int>(secondLine) - 12)},
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, kCountedColor, 2, cv::LINE_AA);
            cv::putText(annotated, "TOTAL SACK: " + std::to_string(tracker.total()),
                        {40, frame.rows - 40}, cv::FONT_HERSHEY_SIMPLEX, 1.5,
                        kCountedColor, 4, cv::LINE_AA);
            std::ostringstream fpsLabel;
            const double sourceFps = networkSource
                                         ? asyncCapture->sourceFps()
                                         : capture.get(cv::CAP_PROP_FPS);
            fpsLabel << "FPS: " << std::fixed << std::setprecision(1) << displayedFps
                     << " | SOURCE: " << sourceFps;
            cv::putText(annotated, fpsLabel.str(), {40, frame.rows - 95},
                        cv::FONT_HERSHEY_SIMPLEX, 1.0, {255, 255, 255}, 3,
                        cv::LINE_AA);

            if (options.save) {
                if (!writer.isOpened()) {
                    double fps = sourceFps;
                    if (fps <= 0.0 || fps > 120.0) {
                        fps = 25.0;
                    }
                    writer.open(outputPath.string(), cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                                fps, annotated.size());
                    if (!writer.isOpened()) {
                        throw std::runtime_error("Gagal membuat video: " + outputPath.string());
                    }
                }
                writer.write(annotated);
            }
            if (options.display) {
                if (windowInitialized && isWindowVisible) {
                    const double visibleProp =
                        cv::getWindowProperty(kWindowName, cv::WND_PROP_VISIBLE);
                    if (visibleProp < 1.0) {
                        std::cout << "Jendela ditutup oleh pengguna. Menghentikan program...\n";
                        break;
                    }
                }

                if (isWindowVisible) {
                    const cv::Rect windowRect = cv::getWindowImageRect(kWindowName);
                    if (windowRect.width > 10 && windowRect.height > 10) {
                        const cv::Mat displayFrame =
                            fitFrameToWindow(annotated, windowRect.width, windowRect.height);
                        cv::imshow(kWindowName, displayFrame);
                    } else {
                        cv::imshow(kWindowName, annotated);
                    }
                }

                int key = -1;
                if (isWindowVisible) {
                    key = cv::waitKey(1) & 0xFF;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

#ifdef _WIN32
                if (key == 255 || key == -1) {
                    if (_kbhit()) {
                        key = _getch();
                    }
                }
#endif

                if (key == 'q' || key == 'Q' || key == 27) {
                    break;
                } else if (key == 'h' || key == 'H') {
                    isWindowVisible = !isWindowVisible;
                    if (!isWindowVisible) {
#ifdef _WIN32
                        HWND hwnd = FindWindowA(nullptr, kWindowName);
                        if (hwnd != nullptr) {
                            ShowWindow(hwnd, SW_HIDE);
                        } else {
                            cv::destroyWindow(kWindowName);
                            windowInitialized = false;
                        }
#else
                        cv::destroyWindow(kWindowName);
                        windowInitialized = false;
#endif
                        std::cout << "[UI] Preview disembunyikan (Hide). Inferensi tetap berjalan di latar belakang.\n"
                                  << "     Tekan 'H' di konsol untuk menampilkan kembali atau 'Q' untuk berhenti.\n";
                    } else {
#ifdef _WIN32
                        HWND hwnd = FindWindowA(nullptr, kWindowName);
                        if (hwnd != nullptr) {
                            ShowWindow(hwnd, SW_SHOW);
                            ShowWindow(hwnd, SW_RESTORE);
                            SetForegroundWindow(hwnd);
                        } else {
                            cv::namedWindow(kWindowName, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
                            const int initW = options.displayWidth > 0 ? options.displayWidth : 1280;
                            const int initH =
                                std::max(1, static_cast<int>(std::round(initW * 9.0 / 16.0)));
                            cv::resizeWindow(kWindowName, initW, initH);
                            windowInitialized = true;
                        }
#else
                        cv::namedWindow(kWindowName, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
                        const int initW = options.displayWidth > 0 ? options.displayWidth : 1280;
                        const int initH =
                            std::max(1, static_cast<int>(std::round(initW * 9.0 / 16.0)));
                        cv::resizeWindow(kWindowName, initW, initH);
                        windowInitialized = true;
#endif
                        std::cout << "[UI] Preview ditampilkan kembali (Show).\n";
                    }
                } else if (key == 'f' || key == 'F') {
                    if (isWindowVisible) {
                        isFullscreen = !isFullscreen;
                        cv::setWindowProperty(
                            kWindowName,
                            cv::WND_PROP_FULLSCREEN,
                            isFullscreen ? cv::WINDOW_FULLSCREEN : cv::WINDOW_NORMAL);
                    }
                } else if (key == 'm' || key == 'M') {
                    if (isWindowVisible) {
#ifdef _WIN32
                        HWND hwnd = FindWindowA(nullptr, kWindowName);
                        if (hwnd != nullptr) {
                            ShowWindow(hwnd, SW_MINIMIZE);
                        }
#endif
                    }
                }
            }
        }

        asyncCapture.reset();
        capture.release();
        writer.release();
        cv::destroyAllWindows();
        std::cout << "Total sack terhitung: " << tracker.total() << '\n';
        if (options.save) {
            std::cout << "Rekaman: " << outputPath << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        printUsage();
        return 1;
    }
}
