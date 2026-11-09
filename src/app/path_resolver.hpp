#pragma once

#include <filesystem>

std::filesystem::path getExecutablePath();

std::filesystem::path findProjectRoot();

std::filesystem::path resolveModelPath(
    const std::filesystem::path& requestedModel,
    const std::filesystem::path& projectRoot);
