#include "cache_entry.h"

CacheEntry::CacheEntry(const ndn::Data &data)
        : _data(data)
        , expire_time_point(ndn::time::steady_clock::now() + data.getFreshnessPeriod()) {

}

const ndn::Data CacheEntry::getData() const {
    return _data;
}

bool CacheEntry::isValid() const {
    return expire_time_point > ndn::time::steady_clock::now();
}

ndn::time::milliseconds CacheEntry::remainingTime() const {
    return ndn::time::duration_cast<ndn::time::milliseconds>(expire_time_point - ndn::time::steady_clock::now());
}