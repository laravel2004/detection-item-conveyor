#include "path_resolver.hpp"

#include <cstdlib>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

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
    // 1. Cek folder kerja saat ini
    const auto current = fs::current_path(ec);
    if (!ec) {
        if (fs::is_regular_file(current / "best.onnx", ec) ||
            fs::is_regular_file(current / "CMakeLists.txt", ec) ||
            fs::is_directory(current / "report", ec)) {
            return current;
        }
    }

    // 2. Cek folder executable dan folder-folder induknya
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

    // Jika user menentukan path model eksplisit
    if (!requestedModel.empty()) {
        if (fs::is_regular_file(requestedModel, ec)) {
            return requestedModel;
        }
        if (!projectRoot.empty() && fs::is_regular_file(projectRoot / requestedModel, ec)) {
            return projectRoot / requestedModel;
        }
        return requestedModel;
    }

    // Cek environment variable SACK_MODEL_PATH
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

    // Cek current working directory / best.onnx
    const auto current = fs::current_path(ec);
    if (!ec && fs::is_regular_file(current / "best.onnx", ec)) {
        return current / "best.onnx";
    }

    // Cek project root / best.onnx
    if (!projectRoot.empty() && fs::is_regular_file(projectRoot / "best.onnx", ec)) {
        return projectRoot / "best.onnx";
    }

    // Cek folder executable dan induknya
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
