#include "report_paths.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

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

std::string timePart(const std::chrono::system_clock::time_point& point,
                     const char* format) {
    const auto time = std::chrono::system_clock::to_time_t(point);
    const auto calendar = localTime(time);
    std::ostringstream stream;
    stream << std::put_time(&calendar, format);
    return stream.str();
}

bool isRtsp(const std::string& source) {
    return source.rfind("rtsp://", 0) == 0 || source.rfind("rtsps://", 0) == 0;
}

std::string rtspHost(const std::string& source) {
    const auto schemeEnd = source.find("://");
    const auto authorityStart = schemeEnd == std::string::npos ? 0 : schemeEnd + 3;
    const auto authorityEnd = source.find_first_of("/?#", authorityStart);
    std::string authority = source.substr(
        authorityStart,
        authorityEnd == std::string::npos ? std::string::npos
                                          : authorityEnd - authorityStart);
    if (const auto userInfoEnd = authority.rfind('@'); userInfoEnd != std::string::npos) {
        authority.erase(0, userInfoEnd + 1);
    }
    if (!authority.empty() && authority.front() == '[') {
        if (const auto closingBracket = authority.find(']');
            closingBracket != std::string::npos) {
            return authority.substr(1, closingBracket - 1);
        }
    }
    if (const auto portStart = authority.find(':'); portStart != std::string::npos) {
        authority.resize(portStart);
    }
    return authority;
}

std::string safeDirectoryName(std::string value) {
    for (char& character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!(std::isalnum(byte) || character == '.' || character == '-' ||
              character == '_')) {
            character = '_';
        }
    }
    while (!value.empty() && (value.back() == '.' || value.back() == ' ')) {
        value.pop_back();
    }
    return value.empty() ? "unknown-source" : value;
}

}  // namespace

std::string reportSourceKey(const std::string& source) {
    if (isRtsp(source)) {
        return safeDirectoryName(rtspHost(source));
    }
    const std::filesystem::path path(source);
    const std::string filename = path.stem().string();
    return safeDirectoryName(filename.empty() ? source : filename);
}

std::filesystem::path hourlyReportPath(
    const std::filesystem::path& projectRoot,
    const std::string& source,
    const std::chrono::system_clock::time_point& point) {
    const auto directory = projectRoot / "report" / timePart(point, "%Y") /
                           timePart(point, "%m") / timePart(point, "%d") /
                           reportSourceKey(source);
    std::filesystem::create_directories(directory);
    return directory / ("rtsp_sack_count_" + timePart(point, "%H00") + ".txt");
}

