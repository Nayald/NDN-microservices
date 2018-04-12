#include "pit.h"

const ndn::time::milliseconds Pit::MINIMAL_INTEREST_LIFETIME {5};

Pit::Pit(size_t size) : _max_size(size) {

}

size_t Pit::getSize() const {
    return _max_size;
}

void Pit::setSize(size_t size) {
    _max_size = size;
}

bool Pit::insert(const ndn::Interest &interest, const std::shared_ptr<Face> &face) {
    if (interest.getInterestLifetime() < MINIMAL_INTEREST_LIFETIME) {
        return false;
    }

    if (auto entry = _tree.find(interest.getName())) {
        _list.splice(_list.begin(), _list, _list_index.at(interest.getName().toUri()));
        return entry->addFace(interest, face);
    } else {
        _tree.insert(interest.getName(), std::make_shared<PitEntry>(interest, face));
        _list_index[interest.getName().toUri()] = _list.emplace(_list.begin(), interest.getName());
        if (_list.size() > _max_size) {
            _tree.remove(*_list.rbegin());
            _list_index.erase(_list.rbegin()->toUri());
            _list.erase(--_list.end());
        }
        //std::cout << _tree.getPopulatedNodes() << "/" << _max_size << " (" << _tree.size() << " total nodes)" << std::endl;
        return true;
    }
}

std::set<std::shared_ptr<Face>> Pit::get(const ndn::Data &data) {
    std::set<std::shared_ptr<Face>> faces;
    auto list = _tree.findAllUntil(data.getName());
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        auto&& entry_faces = it->second->getAndResetFaces();
        faces.insert(std::make_move_iterator(entry_faces.begin()), std::make_move_iterator(entry_faces.end()));
        //_tree.remove(it->first);
    }
    return faces;
}

std::string Pit::toJSON() const {
    std::stringstream ss;
    ss << R"({"type": "pit", "tree":)" << _tree.toJSON() << "}";
    return ss.str();
}
