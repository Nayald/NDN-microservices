#pragma once

#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>

#include <memory>
#include <list>
#include <unordered_map>

#include "tree/named_tree.h"
#include "cache_entry.h"

class LruCache {
private:
    size_t _max_size;

    NamedTree<CacheEntry> _tree;
    std::list<ndn::Name> _list;
    std::unordered_map<std::string, std::list<ndn::Name>::iterator> _list_index;

public:
    explicit LruCache(size_t size);

    ~LruCache() = default;

    size_t getSize() const;

    void setSize(size_t size);

    void insert(const ndn::Data &data);

    std::shared_ptr<CacheEntry> get(const ndn::Interest &interest);
};