#pragma once

#include "strategy.h"

class LoadbalancingStrategy : public Strategy {
private:
    size_t _index = 0;

public:
    LoadbalancingStrategy();

    ~LoadbalancingStrategy() = default;

    std::vector<std::shared_ptr<Face>> selectFaces(const std::vector<std::shared_ptr<Face>> &faces) override;
};