#include "store_engine.hpp"
#include "solid/system/log.hpp"
#include "ola/common/ola_front_protocol.hpp"
#include "ola/common/utility/encode.hpp"

using namespace std;
using namespace solid;

namespace ola {
namespace client {
namespace store {
namespace {
const solid::LoggerT logger("ola::client::store::engine");
}

struct Engine::Implementation {
    Configuration config_;
    frame::mprpc::ServiceT& rrpc_service_;
public:
    Implementation(frame::mprpc::ServiceT& _rrpc_service)
        : rrpc_service_(_rrpc_service)
    {
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
    pimpl_->config_ = std::move(_rcfg);

    auto req_ptr = make_shared<front::ListAppsRequest>();

    //o - owned applications
    //a - aquired applications
    //A - all applications
    req_ptr->choice_ = 'A';
    auto lambda = [this](
                      frame::mprpc::ConnectionContext&   _rctx,
                      std::shared_ptr<front::ListAppsRequest>&  _rsent_msg_ptr,
                      std::shared_ptr<front::ListAppsResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&             _rerror) {
        if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
            for (const auto& app_id : _rrecv_msg_ptr->app_id_vec_) {
            }
        } else if (!_rrecv_msg_ptr) {
            solid_log(logger, Info, "no ListAppsResponse: "<<_rerror.message());
        } else {
            solid_log(logger, Info, "ListAppsResponse error: " << _rrecv_msg_ptr->error_);
        }
    };
    pimpl_->rrpc_service_.sendRequest(pimpl_->config_.front_endpoint_.c_str(), req_ptr, lambda);
}

void Engine::stop()
{
}

} //namespace store
} //namespace client
} //namespace ola