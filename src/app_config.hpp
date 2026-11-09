#pragma once

#include <filesystem>
#include <string>

struct AppConfig {
    std::string source;
    std::filesystem::path model;
    std::string device = "gpu";
    bool save = true;
    bool display = true;
    int displayWidth = 1600;
    int maxFrames = 0;

    static AppConfig parse(int argc, char** argv);
    static void printUsage();
};
