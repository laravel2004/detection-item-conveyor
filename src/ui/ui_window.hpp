#pragma once

#include <opencv2/core.hpp>
#include <string>

class UIWindow {
public:
    UIWindow(std::string windowName, bool enabled, int initialWidth);
    ~UIWindow();

    // Mengembalikan false jika pengguna meminta keluar (tombol Q, ESC, atau klik X)
    bool processFrame(const cv::Mat& frame);

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }
    [[nodiscard]] bool isVisible() const noexcept { return isVisible_; }
    [[nodiscard]] bool isQuitRequested() const noexcept { return quitRequested_; }

private:
    cv::Mat fitFrameToWindow(const cv::Mat& src, int targetWidth, int targetHeight);
    void handleKey(int key);

    std::string windowName_;
    bool enabled_;
    int initialWidth_;
    bool isVisible_ = false;
    bool isFullscreen_ = false;
    bool windowInitialized_ = false;
    bool quitRequested_ = false;
};
