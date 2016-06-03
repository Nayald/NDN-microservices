#include <ndn-cxx/common.hpp>
#include <ndn-cxx/interest.hpp>

#include <boost/bind.hpp>

#include "udp_server.h"

udp_server::udp_session::udp_session(udp_server &udp_server) : _udps(udp_server) { }

udp_server::udp_session::~udp_session() { }

void udp_server::udp_session::process(size_t bytes_transferred) {
    try {
        ndn::Interest interest(ndn::Block(_buffer, bytes_transferred));
        //std::cout << interest << " from " << _endpoint_in << std::endl;
        std::shared_ptr<ndn::Data> data_ptr = _udps._cs.tryGet(interest.getName());
        if (data_ptr) {
            //std::cout << *data_ptr << std::endl;
            _udps._socket_in.send_to(
                    boost::asio::buffer(data_ptr->wireEncode().wire(), data_ptr->wireEncode().size()),
                    _udps._endpoint_in);
        } else {
            std::cout << "Forward interest (" << bytes_transferred << " octets) with name " << interest.getName() <<
            " from " << _udps._endpoint_in << " to " << _udps._endpoint_out << std::endl;
            _udps._socket_out.async_send_to(boost::asio::buffer(_buffer, bytes_transferred), _udps._endpoint_out,
                                            std::bind(&udp_session::receiveFromNext, shared_from_this(),
                                                      std::placeholders::_1,
                                                      std::placeholders::_2));
        }
    } catch (std::exception &e) {
        std::cout << e.what() << std::endl;
    }
}

void udp_server::udp_session::receiveFromNext(const boost::system::error_code &err, size_t bytes_transferred) {
    if (!err) {
        _udps._socket_out.async_receive_from(boost::asio::buffer(_buffer, 8800), _udps._endpoint_out,
                                             boost::bind(&udp_session::sendToPrev, shared_from_this(),
                                                         boost::asio::placeholders::error,
                                                         boost::asio::placeholders::bytes_transferred));
    }
}

void udp_server::udp_session::sendToPrev(const boost::system::error_code &err, size_t bytes_transferred) {
    if (!err) {
        auto data_ptr = std::make_shared<ndn::Data>(ndn::Block(_buffer, bytes_transferred));
        //_udps._cs.insert(data_ptr);
        std::cout << "Forward data (" << bytes_transferred << " octets) with name " << data_ptr->getName() <<
        " from " << _udps._endpoint_out << " to " << _udps._endpoint_in << std::endl;
        _udps._socket_in.send_to(boost::asio::buffer(_buffer, bytes_transferred), _udps._endpoint_in);
    }
}

udp_server::udp_server(boost::asio::io_service &ios, cs_cache &cs, std::string host, std::string port)
        : _socket_in(ios, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 3000)),
          _socket_out(ios, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)), strand(ios), _cs(cs) {
    _endpoint_out = *boost::asio::ip::udp::resolver(ios).resolve({boost::asio::ip::udp::v4(), host, port});
    receive();
}

udp_server::~udp_server() { }

void udp_server::receive() {
    auto session = std::make_shared<udp_session>(*this);
    //set.insert(session);
    _socket_in.async_receive_from(boost::asio::buffer(session->_buffer, 8800), _endpoint_in,
                                  std::bind(&udp_server::handle_receive, this,
                                            session,
                                            std::placeholders::_1,
                                            std::placeholders::_2));
}

void udp_server::handle_receive(std::shared_ptr<udp_session> session, const boost::system::error_code &err,
                                size_t bytes_transferred) {
    if (!err) {
        _socket_in.get_io_service().post(boost::bind(&udp_session::process, session, bytes_transferred));
    }
    receive();
}
