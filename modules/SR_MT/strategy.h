#pragma once

#include <memory>
#include <vector>

#include "network/face.h"

class Strategy {
private:

public:
    Strategy() {

    }

    virtual ~Strategy() = default;

    virtual std::vector<std::shared_ptr<Face>> selectFaces(const std::vector<std::shared_ptr<Face>> &faces) = 0;
};