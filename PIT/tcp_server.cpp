#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>

#include <boost/bind.hpp>

#include "tcp_server.h"

Tcp_server::Tcp_session::Tcp_session(Tcp_server *tcps, std::shared_ptr<boost::asio::ip::tcp::socket> socket_in)
        : _tcps(tcps)
        , _socket_in(std::move(socket_in)) {
    std::cout << "session start by " << _socket_in->remote_endpoint() << std::endl;
}

void Tcp_server::Tcp_session::process() {
        _socket_in->async_read_some(boost::asio::buffer(_buffer_in, 8800),
                                    boost::bind(&Tcp_session::sendToNext,
                                                shared_from_this(),
                                                boost::asio::placeholders::error,
                                                boost::asio::placeholders::bytes_transferred));
}

void Tcp_server::Tcp_session::sendToNext(const boost::system::error_code &err, size_t bytes_transferred) {
    if (!err) {
        std::vector<ndn::Interest> interests;
        interest_flow.append(_buffer_in, bytes_transferred);

        //find the interests in the stream
        findPacket(0x05, interest_flow, interests);

        //add to pit and append to buffer if transmission is needed
        std::string forward_buffer;
        for(auto& interest : interests){
            if(_tcps->_pit.insert(interest, _socket_in)) {
                forward_buffer.append(reinterpret_cast<const char *>(interest.wireEncode().wire()),
                                      interest.wireEncode().size());
            }else{
                std::cout << interest.getName() << " too early for retransmission" << std::endl;
            }
        }

        //forward the interest(s) to the next hop
        boost::asio::write(_tcps->_socket_out, boost::asio::buffer(forward_buffer));
        process();
    } else {
        _tcps->_pit.remove(_socket_in);
    }
}

//----------------------------------------------------------------------------------------------------------------------

Tcp_server::Tcp_server(boost::asio::io_service &ios, std::string host, std::string port)
        : _acceptor(ios, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 6361))
        , _socket_out(ios) {
    _endpoint_out = *boost::asio::ip::tcp::resolver(ios).resolve({boost::asio::ip::tcp::v4(), host, port});
    _socket_out.connect(_endpoint_out);
    accept();
    receiveFromNext();
}

Tcp_server::~Tcp_server() { }

void Tcp_server::accept() {
    auto socket_in = std::make_shared<boost::asio::ip::tcp::socket>(_socket_out.get_io_service());
    _acceptor.async_accept(*socket_in, [this, socket_in](const boost::system::error_code &err) {
        if (!err) {
            std::make_shared<Tcp_session>(this, std::move(socket_in))->process();
        }
        accept();
    });
}

void Tcp_server::receiveFromNext() {
        _socket_out.async_read_some(boost::asio::buffer(_buffer_out, 8800),
                                    boost::bind(&Tcp_server::sendToPrev,
                                                this,
                                                boost::asio::placeholders::error,
                                                boost::asio::placeholders::bytes_transferred));
}

void Tcp_server::sendToPrev(const boost::system::error_code &err, size_t bytes_transferred) {
    if (!err) {
        std::vector<ndn::Data> datas;
        data_flow.append(_buffer_out, bytes_transferred);

        //find the data in the stream
        findPacket(0x06, data_flow, datas);

        //send all matching datas to client
        for(const auto& data : datas){
            //std::cout << data.getName() << std::endl;
            //if(_pit.get(data.getName()).getFaces().size() > 1)
            //    std::cout << data.getName() << " " << _pit.get(data.getName()).getFaces().size() << std::endl;
            for(const auto& socket : _pit.get(data.getName()).getFaces()){
                try{
                    //socket->send(boost::asio::buffer(data.wireEncode().wire(), data.wireEncode().size()));
                    boost::asio::write(*socket, boost::asio::buffer(data.wireEncode().wire(), data.wireEncode().size()));
                    //std::cout << socket->remote_endpoint() << ": " << data.getName() << std::endl;
                } catch (const std::exception &e){
                    std::cerr << e.what() <<  std::endl;
                }
            }
            _pit.remove(data.getName());
        }

        receiveFromNext();
    }
}

//----------------------------------------------------------------------------------------------------------------------

template<class T>
void Tcp_server::findPacket(uint8_t packetDelimiter, std::string &stream, std::vector<T> &structure){
    //find as much as possible packets in the stream
    try {
        do {
            //packet start with the delimiter (for ndn 0x05 or 0x06) so we remove any starting bytes that is not one of them
            stream.erase(0, stream.find(packetDelimiter));

            //in tlv spec the size doesn't count the first byte and itself (at least 1 byte)
            uint64_t size = 2;

            //select the lenght of the size, according to tlv format (1, 2, 4 or 8 bytes to read)
            switch (static_cast<uint8_t>(stream[1])) {
                default:
                    size += static_cast<uint8_t>(stream[1]);
                    break;
                case 0xFD:
                    size += static_cast<uint16_t>(stream[2]) << 8;
                    size += static_cast<uint16_t>(stream[3]) + 2;
                    break;
                case 0xFE:
                    size += static_cast<uint32_t>(stream[2]) << 24;
                    size += static_cast<uint32_t>(stream[3]) << 16;
                    size += static_cast<uint32_t>(stream[4]) << 8;
                    size += static_cast<uint32_t>(stream[5]) + 4;
                    break;
                case 0xFF:
                    size += static_cast<uint64_t>(stream[2]) << 56;
                    size += static_cast<uint64_t>(stream[3]) << 48;
                    size += static_cast<uint64_t>(stream[4]) << 40;
                    size += static_cast<uint64_t>(stream[5]) << 32;
                    size += static_cast<uint64_t>(stream[6]) << 24;
                    size += static_cast<uint64_t>(stream[7]) << 16;
                    size += static_cast<uint64_t>(stream[8]) << 8;
                    size += static_cast<uint64_t>(stream[9]) + 8;
                    break;
            }
            //std::cerr << size << "/" << stream.size() << std::endl;

            //check if the stream have enough bytes else wait for more data in the stream
            if(size > ndn::MAX_NDN_PACKET_SIZE) {
                stream.erase(0, 1);
            } else if (size <= stream.size()) {
                //try to build the packet object then remove the read bytes from stream if fail remove the first byte in order to find the next delimiter
                try {
                    structure.emplace_back(ndn::Block(stream.c_str(), size));
                    stream.erase(0, size);
                } catch (const std::exception &e) {
                    std::cerr << e.what() << std::endl;
                    stream.erase(0, 1);
                }
            } else {
                break;
            }
        } while (!stream.empty());
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }
}