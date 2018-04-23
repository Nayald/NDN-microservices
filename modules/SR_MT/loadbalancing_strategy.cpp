#include "loadbalancing_strategy.h"

std::vector<std::shared_ptr<Face>> LoadbalancingStrategy::selectFaces(const std::vector<std::shared_ptr<Face>> &faces) {
    std::vector<std::shared_ptr<Face>> v;
    if (faces.empty()) {
        return v;
    } else {
        tbb::spin_mutex::scoped_lock lock(_mutex);
        if (_index >= faces.size()) {
            _index = 0;
        }
        v.emplace_back(faces[_index++]);
        return v;
    }
}