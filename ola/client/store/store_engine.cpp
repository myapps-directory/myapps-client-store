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
        Owned
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

    //o - owned applications
    //a - aquired applications
    //A - all applications
    req_ptr->choice_ = 'A';
    auto lambda      = [this](
                      frame::mprpc::ConnectionContext&          _rctx,
                      std::shared_ptr<front::ListAppsRequest>&  _rsent_msg_ptr,
                      std::shared_ptr<front::ListAppsResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                    _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& app_id : _rrecv_msg_ptr->app_id_vec_) {
                pimpl_->app_dq_.emplace_back(std::move(app_id.first), std::move(app_id.second));
                pimpl_->app_map_[pimpl_->app_dq_.back().app_uid_] = pimpl_->app_dq_.size() - 1;
            }
            //requestMore(0, pimpl_->config_.start_fetch_count_);
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

void Engine::requestAquired(std::shared_ptr<front::ListAppsRequest>)
{
    auto req_ptr = make_shared<front::ListAppsRequest>();

    //o - owned applications
    //a - aquired applications
    //A - all applications
    req_ptr->choice_ = 'a';
    auto lambda      = [this](
                      frame::mprpc::ConnectionContext&          _rctx,
                      std::shared_ptr<front::ListAppsRequest>&  _rsent_msg_ptr,
                      std::shared_ptr<front::ListAppsResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                    _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& a : _rrecv_msg_ptr->app_id_vec_) {
                const auto it = pimpl_->app_map_.find(a.second);
                if (it != pimpl_->app_map_.end()) {
                    pimpl_->app_dq_[it->second].flag(ApplicationStub::FlagsE::Aquired);
                }
            }
            //requestMore(0, pimpl_->config_.start_fetch_count_);
            requestOwned(_rsent_msg_ptr);
        } else if (!_rrecv_msg_ptr) {
            solid_log(logger, Info, "no ListAppsResponse: " << _rerror.message());
        } else {
            solid_log(logger, Info, "ListAppsResponse error: " << _rrecv_msg_ptr->error_);
        }
    };
    pimpl_->rrpc_service_.sendRequest(pimpl_->config_.front_endpoint_.c_str(), req_ptr, lambda);
}

void Engine::requestOwned(std::shared_ptr<front::ListAppsRequest>)
{
    auto req_ptr = make_shared<front::ListAppsRequest>();

    //o - owned applications
    //a - aquired applications
    //A - all applications
    req_ptr->choice_ = 'o';
    auto lambda      = [this](
                      frame::mprpc::ConnectionContext&          _rctx,
                      std::shared_ptr<front::ListAppsRequest>&  _rsent_msg_ptr,
                      std::shared_ptr<front::ListAppsResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                    _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (auto& a : _rrecv_msg_ptr->app_id_vec_) {
                const auto it = pimpl_->app_map_.find(a.second);
                if (it != pimpl_->app_map_.end()) {
                    pimpl_->app_dq_[it->second].flag(ApplicationStub::FlagsE::Owned);
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
                    pimpl_->app_dq_[i].hasFlag(ApplicationStub::FlagsE::Owned));
            } else /*if (_rrecv_msg_ptr->error_)*/ {
                {
                    lock_guard<mutex> lock(pimpl_->mutex_);
                    pimpl_->app_dq_[i].status_ = ApplicationStub::StatusE::Errored;
                }
                pimpl_->config_.on_fetch_error_fnc_(i, pimpl_->fetch_count_);
            }
        };
        auto req_ptr     = make_shared<ola::front::FetchBuildConfigurationRequest>();
        req_ptr->app_id_ = pimpl_->app_dq_[i].app_id_;
        req_ptr->lang_   = pimpl_->config_.language_;
        req_ptr->os_id_  = pimpl_->config_.os_;

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

} //namespace store
} //namespace client
} //namespace ola