#pragma once

#include <exception>

class entry_exception : std::exception {
public:
    virtual const char *what() const throw() {
        return "Fail to create directory";
    }
};