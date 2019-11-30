#pragma once

#include <string>
#include <functional>
#include <vector>
#include "solid/system/pimpl.hpp"
#include "solid/frame/mprpc/mprpcservice.hpp"

namespace ola {
namespace client {
namespace store {

struct Configuration{
    using OnFetchFunctionT = std::function<void(size_t, size_t, std::string&&, std::string&&, std::string&&, std::vector<char>&&)>;
    using OnFetchErrorFunctionT = std::function<void(size_t, size_t)>;

    std::string       front_endpoint_;
    std::string       os_;
    std::string       language_;
    size_t            start_fetch_count_ = 10;
    OnFetchFunctionT  on_fetch_fnc_;
    OnFetchErrorFunctionT on_fetch_error_fnc_;
};

class Engine{
    struct Implementation;
    solid::PimplT<Implementation> pimpl_;
public:
    Engine(solid::frame::mprpc::ServiceT& _rrpc_service);
    ~Engine();
    void start(Configuration&& _rcfg);
    void stop();
    bool requestMore(const size_t _index, const size_t _count_hint);

    void onModelFetchedItems(size_t _model_index, size_t _engine_current_index, size_t _count);
};

} //namespace store
} //namespace client
} //namespace ola
