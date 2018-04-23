#pragma once

#include "strategy.h"

class FailoverStrategy : public Strategy {
private:

public:
    FailoverStrategy() = default;

    ~FailoverStrategy() override = default;

    std::vector<std::shared_ptr<Face>> selectFaces(const std::vector<std::shared_ptr<Face>> &faces);
};
