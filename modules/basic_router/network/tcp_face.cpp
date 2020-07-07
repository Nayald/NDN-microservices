#include "tcp_face.h"

#include <boost/bind.hpp>

#include "../log/logger.h"

static void findPackets(std::string &stream, std::vector<ndn::Interest> &interests, std::vector<ndn::Data> &datas);

TcpFace::TcpFace(boost::asio::io_service &ios, std::string host, uint16_t port)
        : Face(ios)
        , _skip_connect(false)
        , _endpoint(boost::asio::ip::address::from_string(host), port)
        , _socket(ios)
        , _strand(ios)
        , _timer(ios) {
}

TcpFace::TcpFace(boost::asio::io_service &ios, const boost::asio::ip::tcp::endpoint &endpoint)
        : Face(ios)
        , _skip_connect(false)
        , _endpoint(endpoint)
        , _socket(ios)
        , _strand(ios)
        , _timer(ios) {
}

TcpFace::TcpFace(boost::asio::ip::tcp::socket &&socket)
        : Face(socket.get_io_service())
        , _skip_connect(true)
        , _endpoint(socket.remote_endpoint())
        , _socket(std::move(socket))
        , _strand(socket.get_io_service())
        , _timer(socket.get_io_service()) {

}

std::string TcpFace::getUnderlyingProtocol() const {
    return "TCP";
}

std::string TcpFace::getUnderlyingEndpoint() const {
    std::stringstream ss;
    ss << _endpoint;
    return ss.str();
}

void TcpFace::open(const InterestCallback &interest_callback,
                   const DataCallback &data_callback,
                   const ErrorCallback &error_callback) {
    _interest_callback = interest_callback;
    _data_callback = data_callback;
    _error_callback = error_callback;
    if(!_skip_connect && !_is_connected) {
        connect();
    } else {
        _is_connected = true;
        read();
    }
}

void TcpFace::close() {
    _is_connected = false;
    _socket.close();
}

void TcpFace::send(const std::string &message) {
    _strand.dispatch(boost::bind(&TcpFace::sendImpl, shared_from_this(), ndn::Block((const uint8_t*)message.c_str(), message.length())));
}

void TcpFace::send(const ndn::Interest &interest) {
    _strand.dispatch(boost::bind(&TcpFace::sendImpl, shared_from_this(), interest.wireEncode()));
}

void TcpFace::send(const ndn::Data &data) {
    _strand.dispatch(boost::bind(&TcpFace::sendImpl, shared_from_this(), data.wireEncode()));
}

void TcpFace::connect() {
    _timer.expires_from_now(boost::posix_time::seconds(2));
    _timer.async_wait(_strand.wrap(boost::bind(&TcpFace::timerHandler, shared_from_this(), _1)));
    _socket.async_connect(_endpoint, _strand.wrap(boost::bind(&TcpFace::connectHandler, shared_from_this(), _1)));
}

void TcpFace::connectHandler(const boost::system::error_code &err) {
    _timer.cancel();
    if (!err) {
        std::stringstream ss;
        ss << "TCP face with ID = " << _face_id << " successfully connected to tcp://" << _endpoint;
        logger::log(logger::INFO, ss.str());
        _is_connected = true;
        read();
    } else {
        std::stringstream ss;
        ss << "failed to connect to " << _endpoint;
        logger::log(logger::ERROR, ss.str());
        _error_callback(shared_from_this());
    }
}

void TcpFace::reconnect(size_t remaining_attempt) {
    std::stringstream ss;
    ss << "try to reconnect to " << _endpoint;
    logger::log(logger::INFO, ss.str());
    _socket.close();
    _timer.expires_from_now(boost::posix_time::seconds(2));
    _timer.async_wait(_strand.wrap(boost::bind(&TcpFace::timerHandler, shared_from_this(), _1)));
    _socket.async_connect(_endpoint, boost::bind(&TcpFace::reconnectHandler, shared_from_this(), _1, remaining_attempt - 1));
}

void TcpFace::reconnectHandler(const boost::system::error_code &err, size_t remaining_attempt) {
    _timer.cancel();
    if(!err) {
        read();
        if(_queue_in_use) {
            write();
        }
    } else if (remaining_attempt > 0 && _is_connected) {
        std::stringstream ss;
        ss << "wait 1s before next reconnection to " << _endpoint;
        logger::log(logger::INFO, ss.str());
        _timer.expires_from_now(boost::posix_time::seconds(1));
        _timer.async_wait(boost::bind(&TcpFace::reconnect, shared_from_this(), remaining_attempt));
    } else {
        std::stringstream ss;
        ss << "failed to reconnect to " << _endpoint;
        logger::log(logger::ERROR, ss.str());
        _error_callback(shared_from_this());
    }
}

void TcpFace::read() {
    boost::asio::async_read(_socket, boost::asio::buffer(_buffer + _buffer_size, BUFFER_SIZE - _buffer_size),
                            boost::asio::transfer_at_least(1),
                            boost::bind(&TcpFace::readHandler, shared_from_this(), _1, _2));
}

void TcpFace::readHandler(const boost::system::error_code &err, size_t bytes_transferred) {
    if(!err) {
        _buffer_size += bytes_transferred;
        char *current = _buffer;
        char *end = _buffer + _buffer_size;
        while (current < end) {
            if ((uint8_t)current[0] == 0x5 || (uint8_t)current[0] == 0x6 /*|| (uint8_t)current[0] == 0x64*/) {
                //check length
                uint64_t size = 0;
                switch ((uint8_t)current[1]) {
                    default:
                        size += (uint8_t)current[1];
                        size += 2;
                        break;
                    case 0xFD:
                        size += (uint8_t)current[2];
                        size <<= 8;
                        size += (uint8_t)current[3];
                        size += 4;
                        break;
                    case 0xFE:
                        size += (uint8_t)current[2];
                        size <<= 8;
                        size += (uint8_t)current[3];
                        size <<= 8;
                        size += (uint8_t)current[4];
                        size <<= 8;
                        size += (uint8_t)current[5];
                        size += 6;
                        break;
                    case 0xFF:
                        size += (uint8_t)current[2];
                        size <<= 8;
                        size += (uint8_t)current[3];
                        size <<= 8;
                        size += (uint8_t)current[4];
                        size <<= 8;
                        size += (uint8_t)current[5];
                        size <<= 8;
                        size += (uint8_t)current[6];
                        size <<= 8;
                        size += (uint8_t)current[7];
                        size <<= 8;
                        size += (uint8_t)current[8];
                        size <<= 8;
                        size += (uint8_t)current[9];
                        size += 10;
                        break;
                }
                if (size > NDN_MAX_PACKET_SIZE) {
                    ++current;
                } else if (size <= end - current) {
                    try {
                        switch (current[0]) {
                            case 0x05:
                                _interest_callback(shared_from_this(), ndn::Interest(ndn::Block((uint8_t*)current, size)));
                                break;
                            case 0x06:
                                _data_callback(shared_from_this(), ndn::Data(ndn::Block((uint8_t*)current, size)));
                                break;
                            //case 0x64:
                                //Lp packets are not supported yet
                                //break;
                            default:
                                break;
                        }
                    } catch (const std::exception &e) {
                        std::cerr << e.what() << std::endl;
                    }
                    current += size;
                } else {
                    break;
                }
            } else {
                ++current;
            }
        }
        _buffer_size = end - current;
        std::copy(current, end, _buffer);
        read();
    } else {
        if(!_skip_connect && _is_connected) {
            std::stringstream ss;
            ss << "lost connection to " << _endpoint;
            logger::log(logger::WARNING, ss.str());
            reconnect(3);
        } else {
            _error_callback(shared_from_this());
        }
    }
}

void TcpFace::sendImpl(const ndn::Block &block) {
    _queue.push_back(block);
    if (_queue_in_use) {
        return;
    }

    _queue_in_use = true;
    write();
}

void TcpFace::write() {
    const ndn::Block &block = _queue.front();
    boost::asio::async_write(_socket, boost::asio::buffer(block.wire(), block.size()),
                             _strand.wrap(boost::bind(&TcpFace::writeHandler, shared_from_this(), _1, _2)));
}

void TcpFace::writeHandler(const boost::system::error_code &err, size_t bytesTransferred) {
    if(!err) {
        _queue.pop_front();

        if (!_queue.empty()) {
            write();
        } else {
            _queue_in_use = false;
        }
    }
}

void TcpFace::timerHandler(const boost::system::error_code &err) {
    if (!err) {
        _error_callback(shared_from_this());
    }
}
