#include "app/app_config.hpp"
#include "app/sack_counter_app.hpp"

#include <iostream>

int main(int argc, char** argv) {
    try {
        const AppConfig config = AppConfig::parse(argc, argv);
        SackCounterApp app(config);
        return app.run();
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        AppConfig::printUsage();
        return 1;
    }
}
