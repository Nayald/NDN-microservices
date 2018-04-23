#pragma once

#include <memory>
#include <vector>

class RingBuffer {
private:
    const size_t _max;

    uint8_t *_buffer;
    size_t _start = 0;
    size_t _end = 0;

public:
    explicit RingBuffer(size_t size) : _max(size), _buffer(new uint8_t[_max]) {

    }

    ~RingBuffer() = default;

    void put(uint8_t *data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            _buffer[_end++ % _max] = data[i];
        }
    }

    std::vector<uint8_t> get(size_t size) {
        std::vector<uint8_t> buffer(size);
        for (size_t i = 0; i < size; ++i) {
            buffer[i] = _buffer[_start++ % _max];
        }
        return buffer;
    }

    uint8_t operator[](size_t index) {
        return _buffer[(_start + index) % _max];
    }
};