#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <vector>

namespace logger {
    enum Level {
        INFO,
        WARNING,
        ERROR,
    };

    void setFilename(const std::string &filename);

    void setMinimalLogLevel(Level level);

    void log(Level level, const std::string &message);
};