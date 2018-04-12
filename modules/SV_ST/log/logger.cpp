#include "logger.h"

std::mutex _mutex;
logger::Level _level;
std::ofstream _file;

void logger::setFilename(const std::string &filename) {
    std::lock_guard<std::mutex> lock(_mutex);
    _file.open(filename);
    if (!_file) {
        std::cerr << "logger can't open file" << std::endl;
    }
}

void logger::setMinimalLogLevel(Level level) {
    _level = level;
}

void logger::log(Level level, const std::string &message) {
    static const std::vector<std::string> LEVEL_OUTPUT_STRINGS = {"[INFO] ", "[WARNING] ", "[ERROR] "};
    std::lock_guard<std::mutex> lock(_mutex);
    if (_file && level >= _level) {
        _file << LEVEL_OUTPUT_STRINGS[level] << message << std::endl;
    }
}