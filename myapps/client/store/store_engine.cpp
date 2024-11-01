#include "store_engine.hpp"
#include "myapps/common/utility/encode.hpp"
#include "solid/system/log.hpp"
#include <unordered_map>
#include "myapps/client/utility/app_list_file.hpp"

#include <deque>

using namespace std;
using namespace solid;

namespace myapps {
namespace client {
namespace store {
namespace {
const solid::LoggerT logger("myapps::client::store::engine");
}

struct ApplicationStub {
    enum struct StatusE : uint8_t {
        NotFetched,
        Fetched,
        Errored,
    };

    string  app_id_;
    string  app_uid_;
    StatusE status_      = StatusE::NotFetched;
    size_t  model_index_ = InvalidIndex();
    uint8_t flags_       = 0;

    void flag(const ApplicationFlagE _flag)
    {
        set_application_flag(flags_, _flag);
    }

    bool hasFlag(const ApplicationFlagE _flag) const
    {
        return has_application_flag(flags_, _flag);
    }

    ApplicationStub(string&& _app_id, string&& _app_uid)
        : app_id_(std::move(_app_id))
        , app_uid_(std::move(_app_uid))
    {
    }
};

struct Hash {
    size_t operator()(const std::reference_wrapper<const string>& _rrw) const
    {
        std::hash<string> h;
        return h(_rrw.get());
    }
};

struct Equal {
    bool operator()(const std::reference_wrapper<const string>& _rrw1, const std::reference_wrapper<const string>& _rrw2) const
    {
        return _rrw1.get() == _rrw2.get();
    }
};

using ApplicationDequeT = std::deque<ApplicationStub>;
using AtomicSizeT       = std::atomic<size_t>;
using ApplicationMapT   = std::unordered_map<const std::reference_wrapper<const string>, size_t, Hash, Equal>;
using AppListFileT = myapps::client::utility::AppListFile;

struct Engine::Implementation {
    Configuration           config_;
    frame::mprpc::ServiceT& rrpc_service_;
    ApplicationDequeT       app_dq_;
    ApplicationMapT         app_map_;
    size_t                  fetch_count_ = 0;
    mutex                   mutex_;
    AppListFileT            app_list_file_;

public:
    Implementation(frame::mprpc::ServiceT& _rrpc_service)
        : rrpc_service_(_rrpc_service)
    {
    }
    void config(Configuration&& _rcfg)
    {
        config_      = std::move(_rcfg);
        fetch_count_ = config_.start_fetch_count_;
    }

    string localMediaPath(const string& _path, const uint32_t _shard_id, const string& _storage_id) const
    {
        //TODO:
        //return "c:\\MyApps.space\\.m\\" + to_string(_shard_id) + "\\" + myapps::utility::hex_encode(_storage_id) + '\\' + _path;
        return config_.myapps_fs_path_ + "\\.m\\" + to_string(_shard_id) + "\\" + myapps::utility::hex_encode(_storage_id) + '\\' + _path;
    }
};

Engine::Engine(frame::mprpc::ServiceT& _rrpc_service)
    : pimpl_(make_pimpl<Implementation>(_rrpc_service))
{
}

Engine::~Engine()
{
}

void Engine::start(Configuration&& _rcfg)
{
    pimpl_->config(std::move(_rcfg));


    pimpl_->app_list_file_.load(pimpl_->config_.app_list_file_path_);

    auto req_ptr = frame::mprpc::make_message<front::main::ListAppsRequest>();
    solid_log(logger, Info, "Request all Applications");
    //A - all applications
    req_ptr->choice_ = 'A';
    auto lambda      = [this](
                      frame::mprpc::ConnectionContext&          _rctx,
                      frame::mprpc::MessagePointerT<front::main::ListAppsRequest>&  _rsent_msg_ptr,
                      frame::mprpc::MessagePointerT<front::main::ListAppsResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                    _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& app : _rrecv_msg_ptr->app_vec_) {
                pimpl_->app_dq_.emplace_back(std::move(app.id_), std::move(app.unique_));
                pimpl_->app_map_[pimpl_->app_dq_.back().app_uid_] = pimpl_->app_dq_.size() - 1;
                if (app.isFlagSet(myapps::utility::AppFlagE::Owned)) {
                    pimpl_->app_dq_.back().flag(ApplicationFlagE::Owned);
                }
                if (app.isFlagSet(myapps::utility::AppFlagE::ReviewRequest)) {
                    pimpl_->app_dq_.back().flag(ApplicationFlagE::ReviewRequest);
                }
            }
            solid_log(logger, Info, "Response all Applications: " << _rrecv_msg_ptr->app_vec_.size());
            requestAquired(_rsent_msg_ptr);
        } else if (!_rrecv_msg_ptr) {
            solid_log(logger, Info, "no ListAppsResponse: " << _rerror.message());
        } else {
            solid_log(logger, Info, "ListAppsResponse error: " << _rrecv_msg_ptr->error_);
        }
    };
    pimpl_->rrpc_service_.sendRequest({pimpl_->config_.front_endpoint_}, req_ptr, lambda);


}

void Engine::stop()
{
}

void Engine::requestAquired(frame::mprpc::MessagePointerT<front::main::ListAppsRequest>& _rreq_msg)
{
    auto req_ptr = std::move(_rreq_msg);

    //a - aquired applications
    req_ptr->choice_ = 'a';
    auto lambda      = [this](
                      frame::mprpc::ConnectionContext&          _rctx,
                      frame::mprpc::MessagePointerT<front::main::ListAppsRequest>&  _rsent_msg_ptr,
                      frame::mprpc::MessagePointerT<front::main::ListAppsResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                    _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& a : _rrecv_msg_ptr->app_vec_) {
                const auto it = pimpl_->app_map_.find(a.unique_);
                if (it != pimpl_->app_map_.end()) {
                    pimpl_->app_dq_[it->second].flag(ApplicationFlagE::Aquired);
                }
            }
            solid_log(logger, Info, "Response Aquired Applications: " << _rrecv_msg_ptr->app_vec_.size());
            requestDefault(_rsent_msg_ptr);
        } else if (!_rrecv_msg_ptr) {
            solid_log(logger, Info, "no ListAppsResponse: " << _rerror.message());
        } else {
            solid_log(logger, Info, "ListAppsResponse error: " << _rrecv_msg_ptr->error_);
        }
    };
    pimpl_->rrpc_service_.sendRequest({pimpl_->config_.front_endpoint_}, req_ptr, lambda);
}

void Engine::requestDefault(frame::mprpc::MessagePointerT<front::main::ListAppsRequest>& _rreq_msg)
{
    auto req_ptr = std::move(_rreq_msg);

    //d - default applications
    req_ptr->choice_ = 'd';
    auto lambda      = [this](
                      frame::mprpc::ConnectionContext&          _rctx,
                      frame::mprpc::MessagePointerT<front::main::ListAppsRequest>&  _rsent_msg_ptr,
                      frame::mprpc::MessagePointerT<front::main::ListAppsResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                    _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& a : _rrecv_msg_ptr->app_vec_) {
                const auto it = pimpl_->app_map_.find(a.unique_);
                if (it != pimpl_->app_map_.end()) {
                    pimpl_->app_dq_[it->second].flag(ApplicationFlagE::Default);
                }
            }
            solid_log(logger, Info, "Response Default Applications: " << _rrecv_msg_ptr->app_vec_.size());
            requestMore(0, pimpl_->config_.start_fetch_count_);
        } else if (!_rrecv_msg_ptr) {
            solid_log(logger, Info, "no ListAppsResponse: " << _rerror.message());
        } else {
            solid_log(logger, Info, "ListAppsResponse error: " << _rrecv_msg_ptr->error_);
        }
    };
    pimpl_->rrpc_service_.sendRequest({pimpl_->config_.front_endpoint_}, req_ptr, lambda);
}

bool Engine::requestMore(const size_t _index, const size_t _count_hint)
{
    pimpl_->fetch_count_ = _count_hint;
    size_t last_index    = _index + _count_hint;
    {
        lock_guard<mutex> lock(pimpl_->mutex_);
        if (last_index > pimpl_->app_dq_.size()) {
            pimpl_->fetch_count_ -= (last_index - pimpl_->app_dq_.size());
            last_index = pimpl_->app_dq_.size();
        }
        if (_index >= last_index) {
            return false;
        }
    }
    for (size_t i = _index; i < last_index; ++i) {

        auto req_ptr = frame::mprpc::make_message<myapps::front::main::FetchBuildConfigurationRequest>();
        string build_request;
        {
            lock_guard<mutex> lock(pimpl_->mutex_);
            req_ptr->application_id_ = pimpl_->app_dq_[i].app_id_;
            build_request = pimpl_->app_list_file_.find(pimpl_->app_dq_[i].app_uid_).name_;
        }


        auto lambda = [this, i, build_request](
                          frame::mprpc::ConnectionContext&                              _rctx,
                          frame::mprpc::MessagePointerT<myapps::front::main::FetchBuildConfigurationRequest>&  _rsent_msg_ptr,
                          frame::mprpc::MessagePointerT<myapps::front::main::FetchBuildConfigurationResponse>& _rrecv_msg_ptr,
                          ErrorConditionT const&                                        _rerror) mutable {
            if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
                solid_log(logger, Info, "Response Application: " << _rrecv_msg_ptr->configuration_.property_vec_[0].second<< " "<< _rrecv_msg_ptr->image_blob_.size());
                pimpl_->config_.on_fetch_fnc_(i, pimpl_->fetch_count_,
                    std::move(_rrecv_msg_ptr->configuration_.property_vec_[0].second), //name
                    std::move(_rrecv_msg_ptr->configuration_.property_vec_[1].second), //company
                    std::move(_rrecv_msg_ptr->configuration_.property_vec_[2].second), //brief
                    std::move(build_request),
                    std::move(_rrecv_msg_ptr->image_blob_),
                    pimpl_->app_dq_[i].flags_);
            } else /*if (_rrecv_msg_ptr->error_)*/ {
                {
                    lock_guard<mutex> lock(pimpl_->mutex_);
                    pimpl_->app_dq_[i].status_ = ApplicationStub::StatusE::Errored;
                }
                pimpl_->config_.on_fetch_error_fnc_(i, pimpl_->fetch_count_);
            }
        };
        

        req_ptr->lang_  = pimpl_->config_.language_;
        req_ptr->os_id_ = pimpl_->config_.os_;
        req_ptr->build_id_ = build_request;
        if (req_ptr->build_id_ == myapps::utility::app_item_invalid) {
            req_ptr->build_id_.clear();
        }

        myapps::utility::Build::set_option(req_ptr->fetch_options_, myapps::utility::Build::FetchOptionsE::Image);
        req_ptr->property_vec_.emplace_back("name");
        req_ptr->property_vec_.emplace_back("company");
        req_ptr->property_vec_.emplace_back("brief");

        pimpl_->rrpc_service_.sendRequest({pimpl_->config_.front_endpoint_}, req_ptr, lambda);
    }
    return true;
}

void Engine::onModelFetchedItems(size_t _model_index, size_t _engine_current_index, size_t _count)
{
    const size_t      start_index = _engine_current_index - pimpl_->fetch_count_;
    lock_guard<mutex> lock(pimpl_->mutex_);
    solid_check(start_index < pimpl_->app_dq_.size());

    for (size_t i = start_index; i < _engine_current_index; ++i) {
        auto& rapps = pimpl_->app_dq_[i];
        if (rapps.status_ == ApplicationStub::StatusE::NotFetched) {
            rapps.status_      = ApplicationStub::StatusE::Fetched;
            rapps.model_index_ = _model_index;
            ++_model_index;
            --_count;
            if (_count == 0) {
                break;
            }
        } else if (rapps.status_ == ApplicationStub::StatusE::Errored) {

        } else {
            solid_throw("invalid status for app at index: " << i);
        }
    }
}

void Engine::fetchItemData(const size_t _index, const string &_build_name, OnFetchItemDataT _fetch_fnc)
{
    auto lambda = [this, _fetch_fnc](
        frame::mprpc::ConnectionContext& _rctx,
        frame::mprpc::MessagePointerT<myapps::front::main::FetchBuildConfigurationRequest>& _rsent_msg_ptr,
        frame::mprpc::MessagePointerT<myapps::front::main::FetchBuildConfigurationResponse>& _rrecv_msg_ptr,
        ErrorConditionT const& _rerror) {
        
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& e : _rrecv_msg_ptr->configuration_.media_.entry_vec_) {
                e.thumbnail_path_ = pimpl_->localMediaPath(e.thumbnail_path_, _rrecv_msg_ptr->media_shard_id_, _rrecv_msg_ptr->media_storage_id_);
                e.path_ = pimpl_->localMediaPath(e.path_, _rrecv_msg_ptr->media_shard_id_, _rrecv_msg_ptr->media_storage_id_);
                solid_log(logger, Info, "Thumbnail path: " << e.thumbnail_path_);
            }
        }
        _fetch_fnc(_rrecv_msg_ptr);
    };

    auto req_ptr = frame::mprpc::make_message<myapps::front::main::FetchBuildConfigurationRequest>();
    {
        lock_guard<mutex> lock(pimpl_->mutex_);
        req_ptr->application_id_ = pimpl_->app_dq_[_index].app_id_;
    }

    req_ptr->lang_  = pimpl_->config_.language_;
    req_ptr->os_id_ = pimpl_->config_.os_;
    req_ptr->build_id_ = _build_name;

    myapps::utility::Build::set_option(req_ptr->fetch_options_, myapps::utility::Build::FetchOptionsE::Image);
    myapps::utility::Build::set_option(req_ptr->fetch_options_, myapps::utility::Build::FetchOptionsE::Media);
    req_ptr->property_vec_.emplace_back("name");
    req_ptr->property_vec_.emplace_back("company");
    req_ptr->property_vec_.emplace_back("brief");
    req_ptr->property_vec_.emplace_back("description");
    req_ptr->property_vec_.emplace_back("release");

    pimpl_->rrpc_service_.sendRequest({pimpl_->config_.front_endpoint_}, req_ptr, lambda);
}

void Engine::fetchItemEntries(const size_t _index, OnFetchAppItemsT _fetch_fnc)
{
    auto lambda = [this, _fetch_fnc](
        frame::mprpc::ConnectionContext& _rctx,
        frame::mprpc::MessagePointerT<myapps::front::main::FetchAppRequest>& _rsent_msg_ptr,
        frame::mprpc::MessagePointerT<myapps::front::main::FetchAppResponse>& _rrecv_msg_ptr,
        ErrorConditionT const& _rerror) {

            _fetch_fnc(_rrecv_msg_ptr);
    };

    auto req_ptr = frame::mprpc::make_message<myapps::front::main::FetchAppRequest>();
    {
        lock_guard<mutex> lock(pimpl_->mutex_);
        req_ptr->application_id_ = pimpl_->app_dq_[_index].app_id_;
    }

    req_ptr->os_id_ = pimpl_->config_.os_;

    pimpl_->rrpc_service_.sendRequest({pimpl_->config_.front_endpoint_}, req_ptr, lambda);
}

void Engine::acquireItem(const size_t _index, const bool _acquire, OnAcquireItemT _fetch_fnc)
{
    auto lambda = [this, _fetch_fnc](
                      frame::mprpc::ConnectionContext&                _rctx,
                      frame::mprpc::MessagePointerT<myapps::front::main::AcquireAppRequest>& _rsent_msg_ptr,
                      frame::mprpc::MessagePointerT<myapps::front::core::Response>&          _rrecv_msg_ptr,
                      ErrorConditionT const&                          _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            _fetch_fnc(_rsent_msg_ptr->acquire_);
        } else {
            _fetch_fnc(false);
        }
    };
    auto req_ptr = frame::mprpc::make_message<myapps::front::main::AcquireAppRequest>();
    {
        lock_guard<mutex> lock(pimpl_->mutex_);
        req_ptr->app_id_ = pimpl_->app_dq_[_index].app_id_;
    }

    req_ptr->acquire_ = _acquire;

    pimpl_->rrpc_service_.sendRequest({pimpl_->config_.front_endpoint_}, req_ptr, lambda);
}

void Engine::acquireBuild(const size_t _index, const std::string& _build_id) {
    lock_guard<mutex> lock(pimpl_->mutex_);
    
    if (_build_id.empty()) {
        pimpl_->app_list_file_.erase(pimpl_->app_dq_[_index].app_uid_);
    }
    else {
        myapps::utility::AppItemEntry entry;
        entry.name_ = _build_id;
        pimpl_->app_list_file_.insert(pimpl_->app_dq_[_index].app_uid_, entry);
    }
    pimpl_->app_list_file_.store(pimpl_->config_.app_list_file_path_);
}

void Engine::changeAppItemState(
    const size_t _index,
    const myapps::utility::AppItemEntry& _app_item,
    const uint8_t _req_state,
    OnResponseT _on_response_fnc
) {
    auto lambda = [this, _on_response_fnc](
        frame::mprpc::ConnectionContext& _rctx,
        frame::mprpc::MessagePointerT<myapps::front::main::ChangeAppItemStateRequest>& _rsent_msg_ptr,
        frame::mprpc::MessagePointerT<myapps::front::core::Response>& _rrecv_msg_ptr,
        ErrorConditionT const& _rerror) {

            _on_response_fnc(_rrecv_msg_ptr);
    };

    auto req_ptr = frame::mprpc::make_message<myapps::front::main::ChangeAppItemStateRequest>();
    {
        lock_guard<mutex> lock(pimpl_->mutex_);
        req_ptr->application_id_ = pimpl_->app_dq_[_index].app_id_;
    }

    req_ptr->os_id_ = pimpl_->config_.os_;
    req_ptr->item_ = _app_item;
    req_ptr->new_state_ = _req_state;

    pimpl_->rrpc_service_.sendRequest({pimpl_->config_.front_endpoint_}, req_ptr, lambda);
}

} //namespace store
} //namespace client
} //namespace myapps
