#include "lru_cache.h"

LruCache::LruCache(size_t size) : _max_size(size) {

}

size_t LruCache::getSize() const {
    return _max_size;
}

void LruCache::setSize(size_t size) {
    _max_size = size;
}

void LruCache::insert(const ndn::Data &data) {
    if (data.getFreshnessPeriod().count() > 0) {
        _tree.insert(data.getName(), std::make_shared<CacheEntry>(data), true);
        _list_index[data.getName().toUri()] = _list.emplace(_list.begin(), data.getName());
        if (_list.size() > _max_size) {
            _tree.remove(*_list.rbegin());
            _list_index.erase(_list.rbegin()->toUri());
            _list.erase(--_list.end());
        }
        //std::cout << _tree.getPopulatedNodes() << "/" << _max_size << std::endl;
    }
}

std::shared_ptr<CacheEntry> LruCache::get(const ndn::Interest &interest) {
    auto pair = _tree.findFirstFrom(interest.getName(), interest.getChildSelector());
    while (pair.second) {
        if (pair.second->isValid()) {
            //std::cout << pair.first << " valid for " << pair.second->remainingTime() << std::endl;
            _list.splice(_list.begin(), _list, _list_index.at(pair.second->getData().getName().toUri()));
            return pair.second;
        } else {
            //std::cout << pair.first << "not valid" << std::endl;
            _tree.remove(pair.first);
            _list.erase(_list_index.at(pair.first.toUri()));
            _list_index.erase(pair.first.toUri());
        }
        pair = _tree.findFirstFrom(interest.getName(), interest.getChildSelector());
    }
    return nullptr;
}