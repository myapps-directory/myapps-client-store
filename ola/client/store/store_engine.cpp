#include "store_engine.hpp"
#include "ola/common/utility/encode.hpp"
#include "solid/system/log.hpp"
#include <unordered_map>

#include <deque>

using namespace std;
using namespace solid;

namespace ola {
namespace client {
namespace store {
namespace {
const solid::LoggerT logger("ola::client::store::engine");
}

struct ApplicationStub {
    enum struct StatusE : uint8_t {
        NotFetched,
        Fetched,
        Errored,
    };
    enum struct FlagsE : uint8_t {
        Aquired = 0,
        Owned,
        Default,
    };

    string  app_id_;
    string  app_uid_;
    StatusE status_      = StatusE::NotFetched;
    size_t  model_index_ = InvalidIndex();
    uint8_t flags_       = 0;

    void flag(const FlagsE _flag)
    {
        flags_ |= (1 << static_cast<uint8_t>(_flag));
    }

    bool hasFlag(const FlagsE _flag) const
    {
        return (flags_ & (1 << static_cast<uint8_t>(_flag))) != 0;
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

struct Engine::Implementation {
    Configuration           config_;
    frame::mprpc::ServiceT& rrpc_service_;
    ApplicationDequeT       app_dq_;
    ApplicationMapT         app_map_;
    size_t                  fetch_count_ = 0;
    mutex                   mutex_;

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

    string localMediaPath(const string& _path, const string& _storage_id) const
    {
        //TODO:
        return "c:\\MyApps.space\\.m\\" + utility::hex_encode(_storage_id) + '\\' + _path;
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

    auto req_ptr = make_shared<front::ListAppsRequest>();

    //A - all applications
    req_ptr->choice_ = 'A';
    auto lambda      = [this](
                      frame::mprpc::ConnectionContext&          _rctx,
                      std::shared_ptr<front::ListAppsRequest>&  _rsent_msg_ptr,
                      std::shared_ptr<front::ListAppsResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                    _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& app_id : _rrecv_msg_ptr->app_vec_) {
                pimpl_->app_dq_.emplace_back(std::move(app_id.id_), std::move(app_id.unique_));
                pimpl_->app_map_[pimpl_->app_dq_.back().app_uid_] = pimpl_->app_dq_.size() - 1;
            }
            requestAquired(_rsent_msg_ptr);
        } else if (!_rrecv_msg_ptr) {
            solid_log(logger, Info, "no ListAppsResponse: " << _rerror.message());
        } else {
            solid_log(logger, Info, "ListAppsResponse error: " << _rrecv_msg_ptr->error_);
        }
    };
    pimpl_->rrpc_service_.sendRequest(pimpl_->config_.front_endpoint_.c_str(), req_ptr, lambda);
}

void Engine::stop()
{
}

void Engine::requestAquired(std::shared_ptr<front::ListAppsRequest>& _rreq_msg)
{
    auto req_ptr = std::move(_rreq_msg);

    //a - aquired applications
    req_ptr->choice_ = 'a';
    auto lambda      = [this](
                      frame::mprpc::ConnectionContext&          _rctx,
                      std::shared_ptr<front::ListAppsRequest>&  _rsent_msg_ptr,
                      std::shared_ptr<front::ListAppsResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                    _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& a : _rrecv_msg_ptr->app_vec_) {
                const auto it = pimpl_->app_map_.find(a.unique_);
                if (it != pimpl_->app_map_.end()) {
                    pimpl_->app_dq_[it->second].flag(ApplicationStub::FlagsE::Aquired);
                }
            }
            requestOwned(_rsent_msg_ptr);
        } else if (!_rrecv_msg_ptr) {
            solid_log(logger, Info, "no ListAppsResponse: " << _rerror.message());
        } else {
            solid_log(logger, Info, "ListAppsResponse error: " << _rrecv_msg_ptr->error_);
        }
    };
    pimpl_->rrpc_service_.sendRequest(pimpl_->config_.front_endpoint_.c_str(), req_ptr, lambda);
}

void Engine::requestOwned(std::shared_ptr<front::ListAppsRequest>& _rreq_msg)
{
    auto req_ptr = std::move(_rreq_msg);

    //o - owned applications
    req_ptr->choice_ = 'o';
    auto lambda      = [this](
                      frame::mprpc::ConnectionContext&          _rctx,
                      std::shared_ptr<front::ListAppsRequest>&  _rsent_msg_ptr,
                      std::shared_ptr<front::ListAppsResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                    _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& a : _rrecv_msg_ptr->app_vec_) {
                const auto it = pimpl_->app_map_.find(a.unique_);
                if (it != pimpl_->app_map_.end()) {
                    pimpl_->app_dq_[it->second].flag(ApplicationStub::FlagsE::Owned);
                }
            }
            requestDefault(_rsent_msg_ptr);
        } else if (!_rrecv_msg_ptr) {
            solid_log(logger, Info, "no ListAppsResponse: " << _rerror.message());
        } else {
            solid_log(logger, Info, "ListAppsResponse error: " << _rrecv_msg_ptr->error_);
        }
    };
    pimpl_->rrpc_service_.sendRequest(pimpl_->config_.front_endpoint_.c_str(), req_ptr, lambda);
}

void Engine::requestDefault(std::shared_ptr<front::ListAppsRequest>& _rreq_msg)
{
    auto req_ptr = std::move(_rreq_msg);

    //d - default applications
    req_ptr->choice_ = 'd';
    auto lambda      = [this](
                      frame::mprpc::ConnectionContext&          _rctx,
                      std::shared_ptr<front::ListAppsRequest>&  _rsent_msg_ptr,
                      std::shared_ptr<front::ListAppsResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                    _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& a : _rrecv_msg_ptr->app_vec_) {
                const auto it = pimpl_->app_map_.find(a.unique_);
                if (it != pimpl_->app_map_.end()) {
                    pimpl_->app_dq_[it->second].flag(ApplicationStub::FlagsE::Default);
                }
            }
            requestMore(0, pimpl_->config_.start_fetch_count_);
        } else if (!_rrecv_msg_ptr) {
            solid_log(logger, Info, "no ListAppsResponse: " << _rerror.message());
        } else {
            solid_log(logger, Info, "ListAppsResponse error: " << _rrecv_msg_ptr->error_);
        }
    };
    pimpl_->rrpc_service_.sendRequest(pimpl_->config_.front_endpoint_.c_str(), req_ptr, lambda);
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
        auto lambda = [this, i](
                          frame::mprpc::ConnectionContext&                              _rctx,
                          std::shared_ptr<ola::front::FetchBuildConfigurationRequest>&  _rsent_msg_ptr,
                          std::shared_ptr<ola::front::FetchBuildConfigurationResponse>& _rrecv_msg_ptr,
                          ErrorConditionT const&                                        _rerror) {
            if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
                pimpl_->config_.on_fetch_fnc_(i, pimpl_->fetch_count_,
                    std::move(_rrecv_msg_ptr->configuration_.property_vec_[0].second), //name
                    std::move(_rrecv_msg_ptr->configuration_.property_vec_[1].second), //company
                    std::move(_rrecv_msg_ptr->configuration_.property_vec_[2].second), //brief
                    std::move(_rrecv_msg_ptr->image_blob_),
                    pimpl_->app_dq_[i].hasFlag(ApplicationStub::FlagsE::Aquired),
                    pimpl_->app_dq_[i].hasFlag(ApplicationStub::FlagsE::Owned),
                    pimpl_->app_dq_[i].hasFlag(ApplicationStub::FlagsE::Default));
            } else /*if (_rrecv_msg_ptr->error_)*/ {
                {
                    lock_guard<mutex> lock(pimpl_->mutex_);
                    pimpl_->app_dq_[i].status_ = ApplicationStub::StatusE::Errored;
                }
                pimpl_->config_.on_fetch_error_fnc_(i, pimpl_->fetch_count_);
            }
        };
        auto req_ptr = make_shared<ola::front::FetchBuildConfigurationRequest>();
        {
            lock_guard<mutex> lock(pimpl_->mutex_);
            req_ptr->app_id_ = pimpl_->app_dq_[i].app_id_;
        }

        req_ptr->lang_  = pimpl_->config_.language_;
        req_ptr->os_id_ = pimpl_->config_.os_;

        ola::utility::Build::set_option(req_ptr->fetch_options_, ola::utility::Build::FetchOptionsE::Image);
        req_ptr->property_vec_.emplace_back("name");
        req_ptr->property_vec_.emplace_back("company");
        req_ptr->property_vec_.emplace_back("brief");

        pimpl_->rrpc_service_.sendRequest(pimpl_->config_.front_endpoint_.c_str(), req_ptr, lambda);
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
        std::shared_ptr<ola::front::FetchBuildConfigurationRequest>& _rsent_msg_ptr,
        std::shared_ptr<ola::front::FetchBuildConfigurationResponse>& _rrecv_msg_ptr,
        ErrorConditionT const& _rerror) {
        
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& e : _rrecv_msg_ptr->configuration_.media_.entry_vec_) {
                e.thumbnail_path_ = pimpl_->localMediaPath(e.thumbnail_path_, _rrecv_msg_ptr->media_storage_id_);
                e.path_ = pimpl_->localMediaPath(e.path_, _rrecv_msg_ptr->media_storage_id_);
            }
        }
        _fetch_fnc(_rrecv_msg_ptr);
    };

    auto req_ptr = make_shared<ola::front::FetchBuildConfigurationRequest>();
    {
        lock_guard<mutex> lock(pimpl_->mutex_);
        req_ptr->app_id_ = pimpl_->app_dq_[_index].app_id_;
    }

    req_ptr->lang_  = pimpl_->config_.language_;
    req_ptr->os_id_ = pimpl_->config_.os_;
    req_ptr->build_id_ = _build_name;

    ola::utility::Build::set_option(req_ptr->fetch_options_, ola::utility::Build::FetchOptionsE::Image);
    ola::utility::Build::set_option(req_ptr->fetch_options_, ola::utility::Build::FetchOptionsE::Media);
    req_ptr->property_vec_.emplace_back("name");
    req_ptr->property_vec_.emplace_back("company");
    req_ptr->property_vec_.emplace_back("brief");
    req_ptr->property_vec_.emplace_back("description");
    req_ptr->property_vec_.emplace_back("release");

    pimpl_->rrpc_service_.sendRequest(pimpl_->config_.front_endpoint_.c_str(), req_ptr, lambda);
}

void Engine::fetchItemBuilds(const size_t _index, OnFetchItemBuildsT _fetch_fnc)
{
    auto lambda = [this, _fetch_fnc](
        frame::mprpc::ConnectionContext& _rctx,
        std::shared_ptr<ola::front::FetchAppRequest>& _rsent_msg_ptr,
        std::shared_ptr<ola::front::FetchAppResponse>& _rrecv_msg_ptr,
        ErrorConditionT const& _rerror) {

            _fetch_fnc(_rrecv_msg_ptr);
    };

    auto req_ptr = make_shared<ola::front::FetchAppRequest>();
    {
        lock_guard<mutex> lock(pimpl_->mutex_);
        req_ptr->app_id_ = pimpl_->app_dq_[_index].app_id_;
    }

    req_ptr->os_id_ = pimpl_->config_.os_;

    pimpl_->rrpc_service_.sendRequest(pimpl_->config_.front_endpoint_.c_str(), req_ptr, lambda);
}

#if 0
void Engine::fetchItemMedia(const size_t _index, OnFetchItemMediaT _fetch_fnc)
{
    auto lambda = [this, _fetch_fnc](
                      frame::mprpc::ConnectionContext&                              _rctx,
                      std::shared_ptr<ola::front::FetchMediaConfigurationRequest>&  _rsent_msg_ptr,
                      std::shared_ptr<ola::front::FetchMediaConfigurationResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                                        _rerror) {
        vector<pair<string, string>> media_vec;

        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {

            for (const auto& m : _rrecv_msg_ptr->configuration_.entry_vec_) {
                media_vec.emplace_back(pimpl_->localMediaPath(m.thumbnail_path_, _rrecv_msg_ptr->storage_id_, _rrecv_msg_ptr->unique_), pimpl_->localMediaPath(m.path_, _rrecv_msg_ptr->storage_id_, _rrecv_msg_ptr->unique_));
            }
        }
        _fetch_fnc(media_vec);
    };
    auto req_ptr = make_shared<ola::front::FetchMediaConfigurationRequest>();
    {
        lock_guard<mutex> lock(pimpl_->mutex_);
        req_ptr->app_id_ = pimpl_->app_dq_[_index].app_id_;
    }

    req_ptr->lang_  = pimpl_->config_.language_;
    req_ptr->os_id_ = pimpl_->config_.os_;

    pimpl_->rrpc_service_.sendRequest(pimpl_->config_.front_endpoint_.c_str(), req_ptr, lambda);
}
#endif

void Engine::acquireItem(const size_t _index, const bool _acquire, OnAcquireItemT _fetch_fnc)
{
    auto lambda = [this, _fetch_fnc](
                      frame::mprpc::ConnectionContext&                _rctx,
                      std::shared_ptr<ola::front::AcquireAppRequest>& _rsent_msg_ptr,
                      std::shared_ptr<ola::front::Response>&          _rrecv_msg_ptr,
                      ErrorConditionT const&                          _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            _fetch_fnc(_rsent_msg_ptr->acquire_);
        } else {
            _fetch_fnc(false);
        }
    };
    auto req_ptr = make_shared<ola::front::AcquireAppRequest>();
    {
        lock_guard<mutex> lock(pimpl_->mutex_);
        req_ptr->app_id_ = pimpl_->app_dq_[_index].app_id_;
    }

    req_ptr->acquire_ = _acquire;

    pimpl_->rrpc_service_.sendRequest(pimpl_->config_.front_endpoint_.c_str(), req_ptr, lambda);
}

} //namespace store
} //namespace client
} //namespace ola
