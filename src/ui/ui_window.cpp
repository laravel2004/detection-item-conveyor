#include "ui_window.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

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

UIWindow::UIWindow(std::string windowName, bool enabled, int initialWidth)
    : windowName_(std::move(windowName)),
      enabled_(enabled),
      initialWidth_(initialWidth),
      isVisible_(enabled) {
    if (enabled_) {
        cv::namedWindow(windowName_, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
        const int initW = initialWidth_ > 0 ? initialWidth_ : 1280;
        const int initH = std::max(1, static_cast<int>(std::round(initW * 9.0 / 16.0)));
        cv::resizeWindow(windowName_, initW, initH);
        windowInitialized_ = true;
    }
}

UIWindow::~UIWindow() {
    if (windowInitialized_) {
        cv::destroyAllWindows();
    }
}

cv::Mat UIWindow::fitFrameToWindow(const cv::Mat& src, int targetWidth, int targetHeight) {
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

bool UIWindow::processFrame(const cv::Mat& frame) {
    if (!enabled_) {
        return true;
    }

    // Cek apakah user menutup jendela via tombol 'X' pada titlebar
    if (windowInitialized_ && isVisible_) {
        const double visibleProp = cv::getWindowProperty(windowName_, cv::WND_PROP_VISIBLE);
        if (visibleProp < 1.0) {
            std::cout << "Jendela ditutup oleh pengguna. Menghentikan program...\n";
            quitRequested_ = true;
            return false;
        }
    }

    if (isVisible_) {
        const cv::Rect windowRect = cv::getWindowImageRect(windowName_);
        if (windowRect.width > 10 && windowRect.height > 10) {
            const cv::Mat displayFrame = fitFrameToWindow(frame, windowRect.width, windowRect.height);
            cv::imshow(windowName_, displayFrame);
        } else {
            cv::imshow(windowName_, frame);
        }
    }

    int key = -1;
    if (isVisible_) {
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

    handleKey(key);
    return !quitRequested_;
}

void UIWindow::handleKey(int key) {
    if (key == 'q' || key == 'Q' || key == 27) { // 27 = ESC
        quitRequested_ = true;
    } else if (key == 'h' || key == 'H') {
        isVisible_ = !isVisible_;
        if (!isVisible_) {
#ifdef _WIN32
            HWND hwnd = FindWindowA(nullptr, windowName_.c_str());
            if (hwnd != nullptr) {
                ShowWindow(hwnd, SW_HIDE);
            } else {
                cv::destroyWindow(windowName_);
                windowInitialized_ = false;
            }
#else
            cv::destroyWindow(windowName_);
            windowInitialized_ = false;
#endif
            std::cout << "[UI] Preview disembunyikan (Hide). Inferensi tetap berjalan di latar belakang.\n"
                      << "     Tekan 'H' di konsol untuk menampilkan kembali atau 'Q' untuk berhenti.\n";
        } else {
#ifdef _WIN32
            HWND hwnd = FindWindowA(nullptr, windowName_.c_str());
            if (hwnd != nullptr) {
                ShowWindow(hwnd, SW_SHOW);
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            } else {
                cv::namedWindow(windowName_, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
                const int initW = initialWidth_ > 0 ? initialWidth_ : 1280;
                const int initH = std::max(1, static_cast<int>(std::round(initW * 9.0 / 16.0)));
                cv::resizeWindow(windowName_, initW, initH);
                windowInitialized_ = true;
            }
#else
            cv::namedWindow(windowName_, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
            const int initW = initialWidth_ > 0 ? initialWidth_ : 1280;
            const int initH = std::max(1, static_cast<int>(std::round(initW * 9.0 / 16.0)));
            cv::resizeWindow(windowName_, initW, initH);
            windowInitialized_ = true;
#endif
            std::cout << "[UI] Preview ditampilkan kembali (Show).\n";
        }
    } else if (key == 'f' || key == 'F') {
        if (isVisible_) {
            isFullscreen_ = !isFullscreen_;
            cv::setWindowProperty(
                windowName_,
                cv::WND_PROP_FULLSCREEN,
                isFullscreen_ ? cv::WINDOW_FULLSCREEN : cv::WINDOW_NORMAL);
        }
    } else if (key == 'm' || key == 'M') {
        if (isVisible_) {
#ifdef _WIN32
            HWND hwnd = FindWindowA(nullptr, windowName_.c_str());
            if (hwnd != nullptr) {
                ShowWindow(hwnd, SW_MINIMIZE);
            }
#endif
        }
    }
}
