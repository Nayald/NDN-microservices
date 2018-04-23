#pragma once

#include <boost/asio.hpp>

#include <memory>
#include <vector>

#include <tbb/spin_rw_mutex.h>

#include "rapidjson/document.h"

#include "module.h"
#include "strategy.h"
#include "network/face.h"
#include "network/master_face.h"

class StrategyRouter : public Module {
private:
    const std::string _name;

    std::string _strategy_name;
    std::shared_ptr<Strategy> _strategy;

    char _command_buffer[65536];
    boost::asio::ip::udp::socket _command_socket;
    boost::asio::ip::udp::endpoint _remote_command_endpoint;

    tbb::speculative_spin_rw_mutex _egress_faces_mutex;
    std::vector<std::shared_ptr<Face>> _egress_faces;
    std::shared_ptr<MasterFace> _tcp_ingress_master_face;
    std::shared_ptr<MasterFace> _udp_ingress_master_face;

public:
    StrategyRouter(const std::string &name, uint16_t local_port, uint16_t local_command_port);

    ~StrategyRouter() override = default;

    void run() override;

    void onIngressPacket(const NdnPacket &packet);

    void onEgressPacket(const NdnPacket &packet);

    void onMasterFaceNotification(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face);

    void onMasterFaceError(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face);

    void onFaceError(const std::shared_ptr<Face> &face);

    void commandRead();

    void commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred);

    void commandEditConfig(const rapidjson::Document &document);

    void commandAddFace(const rapidjson::Document &document);

    void commandDelFace(const rapidjson::Document &document);

    void commandList(const rapidjson::Document &document);
};
