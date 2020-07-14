#pragma once

#include <string>
#include <functional>
#include <vector>
#include "solid/system/pimpl.hpp"
#include "solid/frame/mprpc/mprpcservice.hpp"
#include "ola/common/ola_front_protocol.hpp"
#include <boost/filesystem.hpp>

namespace ola {
namespace client {
namespace store {

enum struct ApplicationFlagE : uint8_t {
    Aquired = 0,
    Owned,
    ReviewRequest,
    Default,
};

template <class T>
bool has_application_flag(const T _v, ApplicationFlagE _f) {
    return (_v & (static_cast<T>(1) << static_cast<uint8_t>(_f))) != 0;
}

template <class T>
void set_application_flag(T &_rflags, ApplicationFlagE _f, const bool _yes = true) {
    if (_yes) {
        _rflags |= (1 << static_cast<uint8_t>(_f));
    }
    else {
        _rflags &= (~(1 << static_cast<uint8_t>(_f)));
    }
}

struct Configuration{
    using OnFetchFunctionT = std::function<void(size_t, size_t, std::string&&, std::string&&, std::string&&, std::string&&, std::vector<char>&&, const uint32_t)>;
    using OnFetchErrorFunctionT = std::function<void(size_t, size_t)>;

    std::string       front_endpoint_;
    std::string       os_;
    std::string       language_;
    boost::filesystem::path       app_list_file_path_;
    size_t            start_fetch_count_ = 10;
    OnFetchFunctionT  on_fetch_fnc_;
    OnFetchErrorFunctionT on_fetch_error_fnc_;
};

class Engine{
    struct Implementation;
    solid::PimplT<Implementation> pimpl_;
public:
    using OnFetchItemDataT = std::function<void(std::shared_ptr<ola::front::FetchBuildConfigurationResponse>&)>;
    using OnFetchAppItemsT = std::function<void(std::shared_ptr<ola::front::FetchAppResponse>&)>;
    using OnAcquireItemT = std::function<void(bool)>;
    using OnResponseT = std::function<void(std::shared_ptr<ola::front::Response>&)>;

    Engine(solid::frame::mprpc::ServiceT& _rrpc_service);
    ~Engine();
    void start(Configuration&& _rcfg);
    void stop();
    
    void requestAquired(std::shared_ptr<front::ListAppsRequest> &_rreq_msg);
    void requestOwned(std::shared_ptr<front::ListAppsRequest>& _rreq_msg);
    void requestDefault(std::shared_ptr<front::ListAppsRequest>& _rreq_msg);

    bool requestMore(const size_t _index, const size_t _count_hint);

    void onModelFetchedItems(size_t _model_index, size_t _engine_current_index, size_t _count);

    void fetchItemData(const size_t _index, const std::string &_build_name, OnFetchItemDataT _fetch_fnc);
    void fetchItemEntries(const size_t _index, OnFetchAppItemsT _fetch_fnc);

    void acquireItem(const size_t _index, const bool _acquire, OnAcquireItemT _fetch_fnc);
    void acquireBuild(const size_t _index, const std::string& _build_id);

    void changeAppItemState(
        const size_t _index,
        const ola::utility::AppItemEntry &_app_item,
        const uint8_t _req_state,
        OnResponseT _on_response_fnc
    );
};

} //namespace store
} //namespace client
} //namespace ola
