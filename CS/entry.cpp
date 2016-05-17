#include "entry.h"

#include <boost/filesystem.hpp>

#include <fstream>
#include <vector>

#include "entry_exception.h"

std::string entry::root_dir;

entry::entry(std::shared_ptr<ndn::Data> data): data_ptr(data), expire_time_point(boost::chrono::steady_clock::now() + data->getFreshnessPeriod()) {}

entry::~entry() {}

bool entry::isValid() {
    return expire_time_point > boost::chrono::steady_clock::now();
}

bool entry::isInRam(){
    return data_ptr!=0;
}

void entry::storeToDisk() {
    int dir_depth = data_ptr->getName().size();
    for (int i = 1; i < dir_depth; ++i) {
        if (boost::filesystem::exists(boost::filesystem::path(root_dir + data_ptr->getName().getSubName(0,i).toUri())))
            continue;
        if (!boost::filesystem::create_directory(
                boost::filesystem::path(root_dir + data_ptr->getName().getSubName(0,i).toUri())))
            throw entry_exception();
    }
    std::ofstream file(root_dir + data_ptr->getName().toUri());
    if (file) {
        file.write(reinterpret_cast<const char *>(data_ptr->wireEncode().wire()), data_ptr->wireEncode().size());
        file.close();
    }
}

std::shared_ptr<ndn::Data> entry::getData(ndn::Name name){
    std::ifstream file(root_dir + name.toUri());
    if(file) {
        std::vector<char> data(file.tellg());
        file.read(&data[0], data.size());
        file.close();
        data_ptr = std::make_shared<ndn::Data>(ndn::Block(reinterpret_cast<const uint8_t *>(&data[0]), data.size()));
    }
    return data_ptr;
}