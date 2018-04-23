#pragma once

#include <memory>
#include <atomic>

#include "face.h"

class MasterFace {
public:
    using NotificationCallback = std::function<void(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face>&)>;
    using ErrorCallback = std::function<void(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face>&)>;

private:
    static size_t counter;

protected:
    const size_t _master_face_id;

    boost::asio::io_service &_ios;

    NotificationCallback _notification_callback;
    Face::Callback _face_callback;
    ErrorCallback _error_callback;

public:
    explicit MasterFace(boost::asio::io_service &ios) : _master_face_id(++counter), _ios(ios) {

    }

    virtual ~MasterFace() = default;

    boost::asio::io_service& get_io_service() const {
        return _ios;
    }

    size_t getMasterFaceId() const {
        return _master_face_id;
    }

    virtual std::string getUnderlyingProtocol() const = 0;

    virtual void listen(const NotificationCallback &notification_callback, const Face::Callback &callback, const ErrorCallback &error_callback) = 0;

    virtual void close() = 0;

    virtual void sendToAllFaces(const NdnPacket &packet) = 0;
};