#include "storage/count_logger.hpp"
#include "storage/report_paths.hpp"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::tm localTime(std::time_t time) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif
    return result;
}

std::string formatTime(const std::chrono::system_clock::time_point& point, const char* format) {
    const auto time = std::chrono::system_clock::to_time_t(point);
    const auto calendar = localTime(time);
    std::ostringstream stream;
    stream << std::put_time(&calendar, format);
    return stream.str();
}

}  // namespace

CountLogger::CountLogger(fs::path projectRoot, std::string source)
    : projectRoot_(std::move(projectRoot)), source_(std::move(source)) {}

void CountLogger::updatePeriod(const std::chrono::system_clock::time_point& now) {
    const std::string period = formatTime(now, "%Y%m%d%H");
    if (period != reportPeriod_) {
        currentReport_ = hourlyReportPath(projectRoot_, source_, now);
        std::ofstream(currentReport_, std::ios::app);
        reportPeriod_ = period;
        std::cout << "Report jam ini: " << currentReport_.string() << '\n';
    }
}

void CountLogger::logCount(int total,
                           int sackId,
                           int frameIndex,
                           const std::chrono::system_clock::time_point& now) {
    updatePeriod(now);

    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now.time_since_epoch()) %
                              1000;

    std::ofstream report(currentReport_, std::ios::app);
    report << formatTime(now, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3)
           << std::setfill('0') << milliseconds.count() << " | sack=" << total
           << " | sack_id=" << sackId << " | frame=" << frameIndex << '\n';
}
