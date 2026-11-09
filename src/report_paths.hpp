#pragma once

#include <chrono>
#include <filesystem>
#include <string>

std::string reportSourceKey(const std::string& source);

std::filesystem::path hourlyReportPath(
    const std::filesystem::path& projectRoot,
    const std::string& source,
    const std::chrono::system_clock::time_point& point);

