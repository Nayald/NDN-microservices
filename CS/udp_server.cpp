#include <ndn-cxx/common.hpp>
#include <ndn-cxx/interest.hpp>

#include <boost/bind.hpp>

#include "udp_server.h"

udp_server::udp_session::udp_session(udp_server &udp_server): _udp_server(udp_server) { }

udp_server::udp_session::~udp_session() { }

void udp_server:: udp_session::process(size_t bytes_transferred) {
    try {
        ndn::Interest i(ndn::Block(_buffer, bytes_transferred));
        //std::cout << i << std::endl;
        std::shared_ptr<ndn::Data> data_ptr = _udp_server._cs.tryGet(i.getName());
        if(data_ptr) {
            _udp_server._socket.send_to(boost::asio::buffer(data_ptr->wireEncode().wire(), data_ptr->wireEncode().size()),
                            _endpoint);
        }
    } catch (std::exception &e) {
        std::cout << e.what() << std::endl;
    }
}

void udp_server::udp_session::handle_send(const boost::system::error_code &err, size_t bytes_transferred){
    if(!err) {
        std::cout << bytes_transferred << std::endl;
    }else{
        std::cout << err.message() << std::endl;
    }
}

udp_server::udp_server(boost::asio::io_service &ios, cs_cache &cs)
        : _socket(ios, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 3000)), _cs(cs) {
    receive();
}

udp_server::~udp_server() { }

void udp_server::receive() {
    auto session = std::make_shared<udp_session>(*this);
    _socket.async_receive_from(boost::asio::buffer(session->_buffer, udp_session::buffer_size), session->_endpoint,
                               boost::bind(&udp_server::handle_receive, this, session,
                                           boost::asio::placeholders::error,
                                           boost::asio::placeholders::bytes_transferred));
}

void udp_server::handle_receive(std::shared_ptr<udp_session> session, const boost::system::error_code &err,
                                size_t bytes_transferred) {
    if (!err) {
        //std::cout << bytes_transferred << std::endl;
        _socket.get_io_service().post(boost::bind(&udp_session::process, session, bytes_transferred));
        receive();
    }
}

