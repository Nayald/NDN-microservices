#pragma once

#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>

#include <memory>
#include <list>
#include <unordered_map>
#include <set>

#include "tree/named_tree.h"
#include "pit_entry.h"
#include "network/face.h"

class Pit {
private:
    static const ndn::time::milliseconds MINIMAL_INTEREST_LIFETIME;

    size_t _max_size;

    NamedTree<PitEntry> _tree;
    std::list<ndn::Name> _list;
    std::unordered_map<std::string, std::list<ndn::Name>::iterator> _list_index;

public:
    explicit Pit(size_t size);

    ~Pit() = default;

    size_t getSize() const;

    void setSize(size_t size);

    bool insert(const ndn::Interest &interest, const std::shared_ptr<Face> &face);

    std::set<std::shared_ptr<Face>> get(const ndn::Data &data);

    std::string toJSON() const;
};