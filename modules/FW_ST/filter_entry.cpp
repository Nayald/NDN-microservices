#include "filter_entry.h"

FilterEntry::FilterEntry(bool drop) : _drop(drop) {

}

bool FilterEntry::getDrop() const {
    return _drop;
}

std::string FilterEntry::toJSON() const {
    std::stringstream ss;
    ss << R"({"drop": )" << (_drop ? "true" : "false") << R"(})";
    return ss.str();
}