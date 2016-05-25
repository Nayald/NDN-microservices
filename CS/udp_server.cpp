#include <ndn-cxx/interest.hpp>

#include <boost/bind.hpp>

#include "udp_server.h"

udp_server::udp_session::udp_session() { }

udp_server::udp_session::~udp_session() { }

void udp_server::udp_session::process(size_t bytes_transferred) {
    try {
        ndn::Data i(ndn::Block(_buffer, bytes_transferred));
        std::cout << i << std::endl;
    } catch (std::exception &e) {
        std::cout << e.what() << std::endl;
    }
}

udp_server::udp_server(boost::asio::io_service &ios)
        : _ios(&ios), _socket(ios, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 3000)) {
    receive_session();
}

udp_server::~udp_server() { }

void udp_server::receive_session() {
    auto session = std::make_shared<udp_server::udp_session>();
    _socket.async_receive_from(boost::asio::buffer(session->_buffer, udp_session::buffer_size), session->_endpoint,
                               boost::bind(&udp_server::handle_session, this, session,
                                           boost::asio::placeholders::error,
                                           boost::asio::placeholders::bytes_transferred));
}

void udp_server::handle_session(std::shared_ptr<udp_server::udp_session> session, const boost::system::error_code &err,
                                size_t bytes_transferred) {
    if (!err) {
        std::cout << bytes_transferred << std::endl;
        _socket.get_io_service().post(bind(&udp_server::udp_session::process, session, bytes_transferred));
        receive_session();
    }
}
