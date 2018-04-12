#pragma once

#include "strategy.h"

class MulticastStrategy : public Strategy {
private:

public:
    MulticastStrategy();

    ~MulticastStrategy() override = default;

    std::vector<std::shared_ptr<Face>> selectFaces(const std::vector<std::shared_ptr<Face>> &faces) override;
};
