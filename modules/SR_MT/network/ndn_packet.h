#pragma once

#include <string>
#include <vector>

class NdnPacket {
private:
    const std::vector<char> _data;

public:
    enum Type {
        INTEREST,
        DATA,
        UNKNOWN,
    };

    explicit NdnPacket(const char* data, size_t size) : _data(data, data + size) {

    }

    ~NdnPacket() = default;

    Type getType() const {
        switch (_data[0]) {
            case 0x5:
                return INTEREST;
            case 0x6:
                return DATA;
            default:
                return UNKNOWN;
        }
    }

    const std::vector<char>& getData() const {
        return _data;
    }
};