#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>

#include <boost/bind.hpp>

#include "udp_server.h"

udp_server::udp_session::udp_session(udp_server &udp_server) : _udps(udp_server) { }

void udp_server::udp_session::process(size_t bytes_transferred) {
    try {
        //ndn::Interest interest(ndn::Block(_bufferPrev.data(), bytes_transferred));
        //std::cout << interest << " from " << _endpoint_in << std::endl;
        //std::shared_ptr<ndn::Data> data_ptr = _udps._cs.tryGet(interest.getName());
        //if (data_ptr) {
        //    std::cout << "Forward data (" << data_ptr->wireEncode().size() << " octets) with name " << interest.getName() <<
        //    " from cache to " << _endpoint_in << std::endl;
        //    _udps._socket_in.send_to(
        //            boost::asio::buffer(data_ptr->wireEncode().wire(), data_ptr->wireEncode().size()),
        //            _endpoint_in);
        //} else {
            //std::cout << "Forward interest (" << bytes_transferred << " octets) with name " << interest.getName() <<
            //" from " << _endpoint_in << " to " << _udps._endpoint_out << std::endl;
            //auto it = _udps._endpoints.find(interest.getName());
            //if(it == _udps._endpoints.end()){
            //    std::list<boost::asio::ip::udp::endpoint> list;
            //    list.emplace_front(_endpoint_in);
            //    _udps._endpoints.emplace(interest.getName(), list);
            //}else{
            //    it->second.emplace_back(_endpoint_in);
            //}

            if(!_udps._sockets.empty()){
                std::cerr << _udps._sockets.size() << std::endl;
                _socket_out = _udps._sockets.back();
                _udps._sockets.pop();
            }else{
                std::cerr << _udps._sockets.size() << std::endl;
                _socket_out = std::make_shared<boost::asio::ip::udp::socket>(_udps._ios, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
            }
            //for (auto c = _bufferPrev.begin(); c != _bufferPrev.end(); ++c){
            //    printf("%02x ", *c);
            //}
            //printf("\n");
            _socket_out->async_send_to(boost::asio::buffer(_buffer, bytes_transferred), _udps._endpoint_out,
                                       boost::bind(&udp_session::receiveFromNext, shared_from_this(),
                                                   boost::asio::placeholders::error,
                                                   boost::asio::placeholders::bytes_transferred));
        //}
    } catch (std::exception &e) {
        std::cout << e.what() << std::endl;
    }

}

void udp_server::udp_session::receiveFromNext(const boost::system::error_code &err, size_t bytes_transferred) {
    if (!err) {
        _socket_out->async_receive_from(boost::asio::buffer(_buffer, 8800), _udps._endpoint_out,
                                             boost::bind(&udp_session::sendToPrev, shared_from_this(),
                                                         boost::asio::placeholders::error,
                                                         boost::asio::placeholders::bytes_transferred));
    }
}

void udp_server::udp_session::sendToPrev(const boost::system::error_code &err, size_t bytes_transferred) {
    if (!err) {
        //auto data_ptr = std::make_shared<ndn::Data>(ndn::Block(_buffer, bytes_transferred));
        //_udps._cs.insert(data_ptr);
        //auto it = _udps._endpoints.find(data_ptr->getName());
        //for(auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
        //    std::cout << "Forward data (" << bytes_transferred << " octets) with name " << data_ptr->getName() <<
        //    " from " << _udps._endpoint_out << " to " << *it2 << std::endl;
        _socket_out->send_to(boost::asio::buffer(_buffer, 8800), _udps._endpoint_out);
        _udps._sockets.push(_socket_out);
        //}
        //_udps._endpoints.erase(it);
    }
}



void udp_server::udp_session::handle_sent(const boost::system::error_code &err, size_t bytes_transferred) {
    if(err){
        std::cout << "Error sending response to " << _endpoint_in << ": " << err.message() << std::endl;
    }
}

udp_server::udp_server(boost::asio::io_service &ios, cs_cache &cs, std::string host, std::string port)
        : _ios(ios), _strand(ios), _socket_in(ios, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 3000))
        , /*_socket_out(ios, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)),*/ _cs(cs) {
    _endpoint_out = *boost::asio::ip::udp::resolver(ios).resolve({boost::asio::ip::udp::v4(), host, port});
    receive();
}

udp_server::~udp_server() { }

void udp_server::receive() {
    auto session = std::make_shared<udp_session>(*this);
    _socket_in.async_receive_from(boost::asio::buffer(session->_buffer, 8800), session->_endpoint_in,
                                  _strand.wrap(boost::bind(&udp_server::handle_receive, this,
                                                           session,
                                                           boost::asio::placeholders::error,
                                                           boost::asio::placeholders::bytes_transferred)));
}

void udp_server::handle_receive(std::shared_ptr<udp_session> session, const boost::system::error_code &err,
                                size_t bytes_transferred) {
    if (!err) {
        _ios.post(boost::bind(&udp_session::process, session, bytes_transferred));
    }
    receive();
}