#include "failover_strategy.h"

std::vector<std::shared_ptr<Face>> FailoverStrategy::selectFaces(const std::vector<std::shared_ptr<Face>> &faces) {
    return !faces.empty() ? std::vector<std::shared_ptr<Face>>{faces[0]} : std::vector<std::shared_ptr<Face>>();
}