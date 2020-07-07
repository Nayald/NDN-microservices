#include "pit_entry.h"

const ndn::time::milliseconds PitEntry::RETRANSMISSION_TIME {250};

PitEntry::PitEntry(const ndn::Interest &interest, const std::shared_ptr<Face> &face)
        : _keep_until(ndn::time::steady_clock::now() + interest.getInterestLifetime())
        , _last_update(ndn::time::steady_clock::now()) {
    _faces.emplace(face);
    //_nonces.emplace(interest.getNonce());
}

const std::set<std::shared_ptr<Face>> PitEntry::getAndResetFaces() {
    std::set<std::shared_ptr<Face>> faces;
    for (auto& face : _faces) {
        if (auto f = face.lock()) {
            faces.emplace(f);
        }
    }
    _faces.clear();
    return faces;
}

bool PitEntry::addFace(const ndn::Interest &interest, const std::shared_ptr<Face> &face) {
    _faces.emplace(face);
    //_nonces.emplace(interest.getNonce());
    auto time_point = ndn::time::steady_clock::now();
    _keep_until = time_point + interest.getInterestLifetime();
    bool need_retransmission = _last_update + RETRANSMISSION_TIME < time_point;
    _last_update = time_point;
    return need_retransmission;
}

bool PitEntry::isValid() const {
    return _keep_until > ndn::time::steady_clock::now();
}

std::string PitEntry::toJSON() {
    std::stringstream ss;
    ss << R"({"faces": [)";
    bool first_face = true;
    auto it = _faces.cbegin();
    while (it != _faces.cend()) {
        if (auto face = it->lock()) {
            if (first_face) {
                first_face = false;
            } else {
                ss << ", ";
            }
            ss << face->getFaceId();
            ++it;
        } else {
            it = _faces.erase(it);
        }
    }
    ss << R"(], "valid_for":)" << ndn::time::duration_cast<ndn::time::milliseconds>(_keep_until - ndn::time::steady_clock::now()).count() << "}";
    return ss.str();
}