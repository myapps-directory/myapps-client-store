#pragma once

#include <string>
#include <functional>
#include <vector>
#include "solid/system/pimpl.hpp"
#include "solid/frame/mprpc/mprpcservice.hpp"
#include "ola/common/ola_front_protocol.hpp"

namespace ola {
namespace client {
namespace store {

struct Configuration{
    using OnFetchFunctionT = std::function<void(size_t, size_t, std::string&&, std::string&&, std::string&&, std::vector<char>&&, bool, bool)>;
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
    using OnFetchItemDataT = std::function<void(const std::string&, const std::string&)>;
    using OnFetchItemMediaT = std::function<void(const std::vector<std::pair<std::string, std::string>>&)>;

    Engine(solid::frame::mprpc::ServiceT& _rrpc_service);
    ~Engine();
    void start(Configuration&& _rcfg);
    void stop();
    
    void requestAquired(std::shared_ptr<front::ListAppsRequest>);
    void requestOwned(std::shared_ptr<front::ListAppsRequest>);
    bool requestMore(const size_t _index, const size_t _count_hint);

    void onModelFetchedItems(size_t _model_index, size_t _engine_current_index, size_t _count);

    void fetchItemData(const size_t _index, OnFetchItemDataT _fetch_fnc);
    void fetchItemMedia(const size_t _index, OnFetchItemMediaT _fetch_fnc);
};

} //namespace store
} //namespace client
} //namespace ola
