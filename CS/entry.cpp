#include "entry.h"

#include <boost/filesystem.hpp>

#include <fstream>

#include "entry_exception.h"

std::string entry::root_dir;

entry::entry(std::shared_ptr<ndn::Data> data): data_ptr(data), expire_time_point(boost::chrono::steady_clock::now() + data->getFreshnessPeriod()) {}

entry::~entry() {}

bool entry::isValid() {
    return expire_time_point > boost::chrono::steady_clock::now();
}

long entry::remaining(){
    return boost::chrono::duration_cast<boost::chrono::milliseconds>(expire_time_point - boost::chrono::steady_clock::now()).count();
}

bool entry::isInRam(){
    return data_ptr!=0;
}

std::shared_ptr<ndn::Data> entry::getData(){
    return data_ptr;
}

void entry::storeToDisk() {
    size_t dir_depth = data_ptr->getName().size();
    for (size_t i = 0; i < dir_depth; ++i) {
        if (boost::filesystem::exists(boost::filesystem::path(root_dir + data_ptr->getName().getSubName(0,i).toUri())))
            continue;
        if (!boost::filesystem::create_directory(
                boost::filesystem::path(root_dir + data_ptr->getName().getSubName(0,i).toUri())))
            throw entry_exception();
    }
    std::ofstream file(root_dir + data_ptr->getName().toUri());
    if (file) {
        file.write(reinterpret_cast<const char *>(data_ptr->wireEncode().wire()), data_ptr->wireEncode().size());
    }
}

void entry::removeFromDisk(ndn::Name name) {
    boost::filesystem::path path(root_dir + name.toUri());
    boost::filesystem::remove(path);
    while((path=path.parent_path()) != root_dir){
        if(!boost::filesystem::is_empty(path)){
            break;
        }else{
            boost::filesystem::remove(path);
        }
    }
}

std::shared_ptr<ndn::Data> entry::getFromDisk(ndn::Name name){
    std::ifstream file(root_dir + name.toUri());
    if(file) {
        std::stringstream ss;
        ss << file.rdbuf();
        file.close();
        removeFromDisk(name);
        return std::make_shared<ndn::Data>(ndn::Block(reinterpret_cast<const uint8_t *>(ss.str().c_str()), ss.str().length()));
    }
    return nullptr;
}

