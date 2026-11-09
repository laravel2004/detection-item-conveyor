#pragma once

#include <chrono>
#include <filesystem>
#include <string>

class CountLogger {
public:
    CountLogger(std::filesystem::path projectRoot, std::string source);

    void updatePeriod(const std::chrono::system_clock::time_point& now);
    void logCount(int total, int sackId, int frameIndex, const std::chrono::system_clock::time_point& now);

    [[nodiscard]] const std::filesystem::path& currentReportPath() const noexcept { return currentReport_; }

private:
    std::filesystem::path projectRoot_;
    std::string source_;
    std::string reportPeriod_;
    std::filesystem::path currentReport_;
};
