#pragma once

#include "app/app_config.hpp"

class SackCounterApp {
public:
    explicit SackCounterApp(AppConfig config);
    ~SackCounterApp();

    int run();

private:
    AppConfig config_;
};
