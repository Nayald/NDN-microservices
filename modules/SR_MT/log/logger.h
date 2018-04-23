#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <vector>

#include <tbb/spin_mutex.h>

namespace logger {
    enum Level {
        INFO,
        WARNING,
        ERROR,
    };

    namespace {
        tbb::spin_mutex mutex;
        std::ofstream file;
        bool is_tee = false;
        Level level = INFO;
    }

    void setFilename(const std::string &filename);

    void isTee(bool state);

    void setMinimalLogLevel(Level level);

    void log(Level level, const std::string &message);
};