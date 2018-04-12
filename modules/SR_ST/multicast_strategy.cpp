#include "multicast_strategy.h"

MulticastStrategy::MulticastStrategy() : Strategy() {

}

std::vector<std::shared_ptr<Face>> MulticastStrategy::selectFaces(const std::vector<std::shared_ptr<Face>> &faces) {
    return faces;
}