#include "store_engine.hpp"
#include "ola/common/ola_front_protocol.hpp"
#include "ola/common/utility/encode.hpp"
#include "solid/system/log.hpp"

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
    string app_id_;

    ApplicationStub() {}
    ApplicationStub(const string& _app_id)
        : app_id_(_app_id)
    {
    }
};

using ApplicationDequeT = std::deque<ApplicationStub>;

struct Engine::Implementation {
    Configuration           config_;
    frame::mprpc::ServiceT& rrpc_service_;
    ApplicationDequeT       app_dq_;
    size_t                  fetch_count_;
    size_t                  fetch_offset_ = 0;

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
            for (auto&& app_id : _rrecv_msg_ptr->app_id_vec_) {
                pimpl_->app_dq_.emplace_back(std::move(app_id));
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

void Engine::stop()
{
}
void Engine::requestMore(const size_t _index, const size_t _count_hint)
{
    const size_t last_offset = _index + _count_hint;
    for (size_t i = _index; i < last_offset; ++i) {
        auto lambda = [this, i, fetch_count = pimpl_->fetch_count_](
                          frame::mprpc::ConnectionContext&                              _rctx,
                          std::shared_ptr<ola::front::FetchBuildConfigurationRequest>&  _rsent_msg_ptr,
                          std::shared_ptr<ola::front::FetchBuildConfigurationResponse>& _rrecv_msg_ptr,
                          ErrorConditionT const&                                        _rerror) {
            if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
                pimpl_->config_.on_fetch_fnc_(i, fetch_count,
                    std::move(_rrecv_msg_ptr->configuration_.property_vec_[0].second), //name
                    std::move(_rrecv_msg_ptr->configuration_.property_vec_[1].second), //company
                    std::move(_rrecv_msg_ptr->configuration_.property_vec_[2].second), //brief
                    std::move(_rrecv_msg_ptr->image_blob_));
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
}

} //namespace store
} //namespace client
} //namespace ola