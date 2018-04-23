#pragma once

#include "face.h"

#include <boost/asio.hpp>

#include <iostream>
#include <string>
#include <deque>
#include <vector>

class TcpFace : public Face, public std::enable_shared_from_this<TcpFace> {
public:
    static const size_t NDN_MAX_PACKET_SIZE = 8800;
    static const size_t BUFFER_SIZE = 1 << 14;

private:
    bool _skip_connect;

    boost::asio::ip::tcp::endpoint _endpoint;
    boost::asio::ip::tcp::socket _socket;
    char _buffer[BUFFER_SIZE];
    size_t _buffer_size = 0;
    bool is_writing = false;
    std::deque<NdnPacket> _queue;

    boost::asio::strand _strand;
    boost::asio::deadline_timer _timer;

public:
    // use these when creating a face yourself
    TcpFace(boost::asio::io_service &ios, std::string host, uint16_t port);

    TcpFace(boost::asio::io_service &ios, const boost::asio::ip::tcp::endpoint &endpoint);

    // specific constructor for MasterFace, not recommended to use it yourself
    explicit TcpFace(boost::asio::ip::tcp::socket &&socket);

    ~TcpFace() override = default;

    std::string getUnderlyingProtocol() const override;

    std::string getUnderlyingEndpoint() const override;

    void open(const Callback &callback, const ErrorCallback &error_callback) override;

    void close() override;

    void send(const NdnPacket &packet) override;

private:
    void connect();

    void connectHandler(const boost::system::error_code &err);

    void reconnect(size_t remaining_attempt);

    void reconnectHandler(const boost::system::error_code &err, size_t remaining_attempt);

    void read();

    void readHandler(const boost::system::error_code &err, size_t bytes_transferred);

    std::vector<NdnPacket> findPackets();

    void sendImpl(const NdnPacket &packet);

    void write();

    void writeHandler(const boost::system::error_code &err, size_t bytesTransferred);

    void timerHandler(const boost::system::error_code &err);
};