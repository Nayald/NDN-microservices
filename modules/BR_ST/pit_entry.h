#pragma once

#include <ndn-cxx/interest.hpp>

#include <memory>
#include <set>

#include "network/face.h"

class PitEntry {
private:
    static const ndn::time::milliseconds RETRANSMISSION_TIME;

    std::set<std::weak_ptr<Face>, std::owner_less<std::weak_ptr<Face>>> _faces;
    //std::set<uint32_t > _nonces;
    ndn::time::steady_clock::time_point _keep_until;
    ndn::time::steady_clock::time_point _last_update;

public:
    PitEntry(const ndn::Interest &interest, const std::shared_ptr<Face> &face);

    ~PitEntry() = default;

    const std::set<std::shared_ptr<Face>> getAndResetFaces();

    bool addFace(const ndn::Interest &interest, const std::shared_ptr<Face> &face);

    bool isValid() const;

    std::string toJSON();
};