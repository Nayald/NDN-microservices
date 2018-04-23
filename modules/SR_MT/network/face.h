#pragma once

#include <boost/asio.hpp>

#include <functional>

#include <memory>
#include <string>

#include "ndn_packet.h"

class Face {
public:
    using Callback = std::function<void(const NdnPacket&)>;
    using ErrorCallback = std::function<void(const std::shared_ptr<Face>&)>;

private:
    static size_t counter;

protected:
    const size_t _face_id;

    bool _is_connected = false;

    boost::asio::io_service &_ios;

    Callback _callback;
    ErrorCallback _error_callback;

public:
    explicit Face(boost::asio::io_service &ios) : _face_id(++counter), _ios(ios) {

    };

    virtual ~Face() = default;

    size_t getFaceId() const {
        return _face_id;
    }

    bool isConnected() {
        return _is_connected;
    }

    virtual std::string getUnderlyingProtocol() const = 0;

    virtual std::string getUnderlyingEndpoint() const = 0;

    virtual void open(const Callback &callback, const ErrorCallback &error_callback) = 0;

    virtual void close() = 0;

    virtual void send(const NdnPacket &packet) = 0;
};