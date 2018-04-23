#pragma once

#include "atomic"

#include <tbb/spin_mutex.h>

#include "strategy.h"

class LoadbalancingStrategy : public Strategy {
private:
    tbb::spin_mutex _mutex;
    std::atomic<size_t> _index {0};

public:
    LoadbalancingStrategy() = default;

    ~LoadbalancingStrategy() override = default;

    std::vector<std::shared_ptr<Face>> selectFaces(const std::vector<std::shared_ptr<Face>> &faces) override;
};