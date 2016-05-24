#pragma once

#include <exception>

class file_exception : std::exception {
public:
    virtual const char *what() const throw() {
        return "Fail to open file";
    }
};