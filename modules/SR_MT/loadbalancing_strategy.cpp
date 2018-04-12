#include "loadbalancing_strategy.h"


LoadbalancingStrategy::LoadbalancingStrategy() : Strategy() {

}

std::vector<std::shared_ptr<Face>> LoadbalancingStrategy::selectFaces(const std::vector<std::shared_ptr<Face>> &faces) {
    if (faces.empty()) {
        return std::vector<std::shared_ptr<Face>>();
    } else {
        if (_index >= faces.size()) {
            _index = 0;
        }
        return {faces[_index++]};
    }
}