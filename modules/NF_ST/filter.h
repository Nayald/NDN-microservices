#pragma once

#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>

#include <memory>
#include <list>
#include <unordered_map>

#include "tree/named_tree.h"
#include "filter_entry.h"

class Filter {
private:
    NamedTree<FilterEntry> _tree;

public:
    Filter();

    ~Filter() = default;

    void insert(const ndn::Name &name, bool drop);

    void remove(const ndn::Name &name);

    bool get(const ndn::Name &name);

    std::string toJSON() const;
};