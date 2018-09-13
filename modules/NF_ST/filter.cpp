#include "filter.h"

Filter::Filter() {
    _tree.insert("/", std::make_shared<FilterEntry>(false));
}

void Filter::insert(const ndn::Name &name, bool drop) {
    _tree.insert(name, std::make_shared<FilterEntry>(drop), true);
}

void Filter::remove(const ndn::Name &name) {
    _tree.remove(name);
}

bool Filter::get(const ndn::Name &name) {
    return _tree.findLastUntil(name).second->getDrop();
}

std::string Filter::toJSON() const {
    return _tree.toJSON();
}