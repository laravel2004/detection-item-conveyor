#include "app_config.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

void AppConfig::printUsage() {
    std::cout
        << "Pemakaian:\n"
        << "  sack_rtsp --source <rtsp-url|video> [--model best.onnx] "
           "[--device cpu|gpu] [--display-width N] [--no-save] [--no-display] "
           "[--max-frames N]\n\n"
        << "Kontrol Keyboard saat preview:\n"
        << "  Q / ESC : Keluar dari program\n"
        << "  H       : Sembunyikan (Hide) / Tampilkan (Show) jendela preview\n"
        << "  M       : Minimize jendela ke taskbar\n"
        << "  F       : Toggle Fullscreen\n";
}

AppConfig AppConfig::parse(int argc, char** argv) {
    AppConfig config;

#ifdef _WIN32
    char* environmentSource = nullptr;
    std::size_t environmentSourceLength = 0;
    if (_dupenv_s(&environmentSource, &environmentSourceLength, "SACK_RTSP_URL") == 0 &&
        environmentSource != nullptr) {
        config.source = environmentSource;
        std::free(environmentSource);
    }
#else
    if (const char* environmentSource = std::getenv("SACK_RTSP_URL")) {
        config.source = environmentSource;
    }
#endif

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto valueAfter = [&](const std::string& option) -> std::string {
            if (index + 1 >= argc) {
                throw std::runtime_error("Nilai belum diisi untuk " + option);
            }
            return argv[++index];
        };

        if (argument == "--source") {
            config.source = valueAfter(argument);
        } else if (argument == "--model") {
            config.model = valueAfter(argument);
        } else if (argument == "--device") {
            config.device = valueAfter(argument);
        } else if (argument == "--max-frames") {
            config.maxFrames = std::stoi(valueAfter(argument));
        } else if (argument == "--display-width") {
            config.displayWidth = std::stoi(valueAfter(argument));
        } else if (argument == "--no-save") {
            config.save = false;
        } else if (argument == "--no-display") {
            config.display = false;
        } else if (argument == "--help" || argument == "-h") {
            printUsage();
            std::exit(0);
        } else {
            throw std::runtime_error("Argumen tidak dikenal: " + argument);
        }
    }

    if (config.source.empty()) {
        throw std::runtime_error("RTSP belum diisi. Gunakan --source atau SACK_RTSP_URL.");
    }
    if (config.device == "cuda" || config.device == "0") {
        config.device = "gpu";
    }
    if (config.device != "cpu" && config.device != "gpu") {
        throw std::runtime_error("--device harus cpu atau gpu.");
    }
    if (config.displayWidth < 0) {
        throw std::runtime_error("--display-width tidak boleh negatif.");
    }

    return config;
}
