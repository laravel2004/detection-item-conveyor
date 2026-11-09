#pragma once

#include <opencv2/core.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class AsyncCapture {
public:
    explicit AsyncCapture(std::string source);
    ~AsyncCapture();

    AsyncCapture(const AsyncCapture&) = delete;
    AsyncCapture& operator=(const AsyncCapture&) = delete;

    bool waitForFrame(cv::Mat& frame,
                      std::uint64_t& lastSequence,
                      std::chrono::milliseconds timeout);

    [[nodiscard]] double sourceFps() const noexcept {
        return sourceFps_.load();
    }

private:
    void run();

    std::string source_;
    std::atomic<bool> stop_{false};
    std::atomic<double> sourceFps_{0.0};
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::shared_ptr<cv::Mat> latestFrame_;
    std::uint64_t sequence_ = 0;
};

