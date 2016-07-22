#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>

#include <boost/bind.hpp>

#include "tcp_server.h"

tcp_server::tcp_session::tcp_session(tcp_server *tcps, boost::asio::ip::tcp::socket socket_in)
        : _tcps(tcps)
        , _socket_in(std::move(socket_in))
        , _socket_out(_socket_in.get_io_service()) { }

void tcp_server::tcp_session::process(boost::asio::ip::tcp::endpoint endpoint_out) {
    std::cout << "session start by " << _socket_in.remote_endpoint() << std::endl;
    boost::system::error_code err;
    _socket_out.connect(endpoint_out, err);
    if (!err) {
        _socket_in.async_read_some(boost::asio::buffer(_buffer_in, 8800),
                                    boost::bind(&tcp_session::sendToNext,
                                                shared_from_this(),
                                                boost::asio::placeholders::error,
                                                boost::asio::placeholders::bytes_transferred));

        _socket_out.async_read_some(boost::asio::buffer(_buffer_out, 8800),
                                     boost::bind(&tcp_session::sendToPrev,
                                                 shared_from_this(),
                                                 boost::asio::placeholders::error,
                                                 boost::asio::placeholders::bytes_transferred));
    } else {
        std::cout << err.message() << std::endl;
    }
}

void tcp_server::tcp_session::sendToNext(const boost::system::error_code &err, size_t bytes_transferred) {
    if (!err) {
        std::vector<ndn::Interest> interests;
        interest_flow.append(_buffer_in, bytes_transferred);

        //find the interests in the stream
        findPacket<ndn::Interest>(0x05, interest_flow, interests);

        std::string backward_buffer;
        std::string forward_buffer;

        //for each found interest, check if a data matches in the cache
        for (auto &interest : interests) {
            auto data_ptr = _tcps->_cs.tryGet(interest.getName());
            std::cout << _socket_in.remote_endpoint() << " send Interest with name=" << interest.getName();
            if (data_ptr) {
                std::cout << " -> respond with cache" << std::endl;
                backward_buffer.append(reinterpret_cast<const char *>(data_ptr->wireEncode().wire()),
                                       data_ptr->wireEncode().size());
            } else {
                std::cout << " -> forward to next hop" << std::endl;
                forward_buffer.append(reinterpret_cast<const char *>(interest.wireEncode().wire()),
                                      interest.wireEncode().size());
            }
        }

        //send back the matching data(s) to the client
        _socket_in.send(boost::asio::buffer(backward_buffer, backward_buffer.size()));

        //forward the other interest(s) to the next hop
        _socket_out.async_send(boost::asio::buffer(forward_buffer, forward_buffer.size()),
                                boost::bind(&tcp_session::receiveFromPrev,
                                            shared_from_this(),
                                            boost::asio::placeholders::error));
    }
}

void tcp_server::tcp_session::receiveFromPrev(const boost::system::error_code &err) {
    if (!err) {
        _socket_in.async_read_some(boost::asio::buffer(_buffer_in, 8800),
                                    boost::bind(&tcp_session::sendToNext,
                                                shared_from_this(),
                                                boost::asio::placeholders::error,
                                                boost::asio::placeholders::bytes_transferred));
    }
}

void tcp_server::tcp_session::sendToPrev(const boost::system::error_code &err, size_t bytes_transferred) {
    if (!err) {
        std::vector<ndn::Data> datas;
        data_flow.append(_buffer_out, bytes_transferred);

        //find the data in the stream
        findPacket<ndn::Data>(0x06, data_flow, datas);

        std::string buffer;

        //insert all found datas in the cache
        for (auto &data : datas) {
            _tcps->_cs.insert(data);
            buffer.append(reinterpret_cast<const char *>(data.wireEncode().wire()),
                          data.wireEncode().size());
        }

        //send all found datas to client
        _socket_in.async_send(boost::asio::buffer(buffer, buffer.size()),
                                   boost::bind(&tcp_session::receiveFromNext,
                                               shared_from_this(),
                                               boost::asio::placeholders::error));
    }
}

void tcp_server::tcp_session::receiveFromNext(const boost::system::error_code &err) {
    if (!err) {
        _socket_out.async_read_some(boost::asio::buffer(_buffer_out, 8800),
                                     boost::bind(&tcp_session::sendToPrev,
                                                 shared_from_this(),
                                                 boost::asio::placeholders::error,
                                                 boost::asio::placeholders::bytes_transferred));
    }
}

template <class T>
void tcp_server::tcp_session::findPacket(uint8_t packetDelimiter, std::string &stream, std::vector<T> &structure){
    //find as much as possible packets in the stream
    try {
        do {
            //packet start with the delimiter (for ndn 0x05 or 0x06) so we remove any starting bytes that is not one of them
            stream.erase(0, stream.find(packetDelimiter));

            //in tlv spec the size doesn't the first bytes and itself (at least 1 byte)
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
                } catch (std::exception &e) {
                    std::cerr << e.what() << std::endl;
                    stream.erase(0, 1);
                }
            } else {
                break;
            }
        } while (!stream.empty());
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
    }
}

//----------------------------------------------------------------------------------------------------------------------

tcp_server::tcp_server(boost::asio::io_service &ios, cs_cache &cs, std::string host, std::string port)
        : _acceptor(ios, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 3000))
        , _socket_in(ios)
        , _cs(cs) {
    _endpoint_out = *boost::asio::ip::tcp::resolver(ios).resolve({boost::asio::ip::tcp::v4(), host, port});
    accept();
}

tcp_server::~tcp_server() { }

void tcp_server::accept() {
    _acceptor.async_accept(_socket_in, [this](const boost::system::error_code &err) {
        if (!err) {
            std::make_shared<tcp_session>(this, std::move(_socket_in))->process(_endpoint_out);
        }
        accept();
    });
}