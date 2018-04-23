#pragma once

#include <map>
#include <deque>

#include "master_face.h"
#include "face.h"

class UdpSubFace;

class UdpMasterFace : public MasterFace, public std::enable_shared_from_this<UdpMasterFace> {
public:
    static const size_t BUFFER_SIZE = 1 << 16;

    class UdpSubFace : public Face, public std::enable_shared_from_this<UdpSubFace> {
    private:
        UdpMasterFace &_master_face;

        boost::asio::ip::udp::endpoint _endpoint;
        boost::asio::deadline_timer _timer;

    public:
        UdpSubFace(UdpMasterFace &master_face, const boost::asio::ip::udp::endpoint &endpoint);

        ~UdpSubFace() override = default;

        std::string getUnderlyingProtocol() const override;

        std::string getUnderlyingEndpoint() const override;

        const boost::asio::ip::udp::endpoint& getEndpoint();

        void open(const Callback &callback, const ErrorCallback &error_callback) override;

        void close() override;

        void send(const NdnPacket &packet) override;

        void proceedPacket(const char* buffer, size_t size);

    private:
        void timerHandler(const boost::system::error_code &err, bool last_chance);
    };

private:
    boost::asio::ip::udp::endpoint _local_endpoint;
    boost::asio::ip::udp::endpoint _remote_endpoint;
    boost::asio::ip::udp::socket _socket;
    boost::asio::strand _strand;
    char _buffer[BUFFER_SIZE];

    std::map<boost::asio::ip::udp::endpoint, std::shared_ptr<UdpSubFace>> _faces;
    std::deque<std::pair<const NdnPacket, const boost::asio::ip::udp::endpoint>> _queue;

public:
    UdpMasterFace(boost::asio::io_service &ios, uint16_t port);

    ~UdpMasterFace() override = default;

    std::string getUnderlyingProtocol() const override;

    void listen(const NotificationCallback &notification_callback, const Face::Callback &face_callback, const ErrorCallback &error_callback) override;

    void close() override;

    void sendToAllFaces(const NdnPacket &packet) override;

private:
    void read();

    void readHandler(const boost::system::error_code &err, size_t bytes_transferred);

    void sendImpl(const NdnPacket &packet, const boost::asio::ip::udp::endpoint &endpoint);

    void write();

    void writeHandler(const boost::system::error_code &err, size_t bytesTransferred);

    void onFaceError(const std::shared_ptr<Face> &face);
};