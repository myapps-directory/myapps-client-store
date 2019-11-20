#pragma once

#include <string>
#include "solid/system/pimpl.hpp"
#include "solid/frame/mprpc/mprpcservice.hpp"

namespace ola {
namespace client {
namespace store {

struct Configuration{
    std::string       front_endpoint_;
    std::string       os_;
    std::string       language_;
};

class Engine{
    struct Implementation;
    solid::PimplT<Implementation> pimpl_;
public:
    Engine(solid::frame::mprpc::ServiceT& _rrpc_service);
    ~Engine();
    void start(Configuration&& _rcfg);
    void stop();
};

} //namespace store
} //namespace client
} //namespace ola
