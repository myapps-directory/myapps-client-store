#undef UNICODE
#define UNICODE
#undef _WINSOCKAPI_
#define _WINSOCKAPI_
#ifndef NOMINMAX
#define NOMINMAX
#endif #undef UNICODE
#define UNICODE
#undef _WINSOCKAPI_
#define _WINSOCKAPI_
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "store_main_window.hpp"

#include "solid/frame/manager.hpp"
#include "solid/frame/scheduler.hpp"
#include "solid/frame/service.hpp"
#include "solid/system/log.hpp"

#include "solid/frame/aio/aioresolver.hpp"

#include "solid/frame/reactor.hpp"
#include "solid/frame/service.hpp"

#include "solid/frame/mprpc/mprpccompression_snappy.hpp"
#include "solid/frame/mprpc/mprpcconfiguration.hpp"
#include "solid/frame/mprpc/mprpcservice.hpp"
#include "solid/frame/mprpc/mprpcsocketstub_openssl.hpp"

#include "ola/common/utility/encode.hpp"

#include "ola/common/ola_front_protocol.hpp"

#include "boost/program_options.hpp"

#include <QApplication>
#include <QtGui>
#include <QStyleFactory>

#include <signal.h>

#include <wtsapi32.h>
#pragma comment(lib, "wtsapi32.lib")
#include <userenv.h>
#pragma comment(lib, "userenv.lib")

#include <fstream>
#include <future>
#include <iostream>

using namespace ola;
using namespace solid;
using namespace std;

using AioSchedulerT = frame::Scheduler<frame::aio::Reactor>;
using SchedulerT    = frame::Scheduler<frame::Reactor>;

//-----------------------------------------------------------------------------
//      Parameters
//-----------------------------------------------------------------------------
namespace {

const solid::LoggerT logger("ola::client::store");

struct Parameters {
    vector<string> dbg_modules = {"ola::.*:VIEW"};
    string         dbg_addr;
    string         dbg_port;
    bool           dbg_console  = false;
    bool           dbg_buffered = false;
    bool           secure;
    bool           compress;
    string         front_endpoint;

    Parameters() {}

    bool parse(ULONG argc, PWSTR* argv);
};

struct Engine {
    client::store::MainWindow&          main_window_;
    frame::mprpc::ServiceT&             front_rpc_service_;
    Parameters&                         params_;
    frame::mprpc::RecipientId           local_recipient_id_;
    std::shared_ptr<front::AuthRequest> front_auth_req_ptr_;
    frame::mprpc::RecipientId           front_recipient_id_;
    mutex                               mutex_;

    Engine(
        client::store::MainWindow& _main_window,
        frame::mprpc::ServiceT&    _front_rpc_service,
        Parameters&                _params)
        : main_window_(_main_window)
        , front_rpc_service_(_front_rpc_service)
        , params_(_params)
    {
    }

    void onConnectionInit(frame::mprpc::ConnectionContext& _ctx);
    void onConnectionStart(frame::mprpc::ConnectionContext& _ctx);
    void onConnectionStop(frame::mprpc::ConnectionContext& _ctx);

    void onAuthStart(const string& _user, const string& _pass);

    void onAuthResponse(
        frame::mprpc::ConnectionContext&      _rctx,
        std::shared_ptr<front::AuthRequest>&  _rsent_msg_ptr,
        std::shared_ptr<front::AuthResponse>& _rrecv_msg_ptr,
        ErrorConditionT const&                _rerror);
};

void front_configure_service(Engine& _rengine, const Parameters& _params, frame::mprpc::ServiceT& _rsvc, AioSchedulerT& _rsch, frame::aio::Resolver& _rres);

//TODO: find a better name
string envLogPathPrefix()
{
    const char* v = getenv("LOCALAPPDATA");
    if (v == nullptr) {
        v = getenv("APPDATA");
        if (v == nullptr) {
            v = "c:";
        }
    }

    string r = v;
    r += "\\OLA\\client";
    return r;
}

void prepare_application();

} //namespace
//-----------------------------------------------------------------------------
//      main
//-----------------------------------------------------------------------------
#ifdef SOLID_ON_WINDOWS
int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    int     wargc;
    LPWSTR* wargv   = CommandLineToArgvW(GetCommandLineW(), &wargc);
    int     argc    = 1;
    char*   argv[1] = {GetCommandLineA()};
#else
int main(int argc, char* argv[])
{
#endif
    Parameters params;

    if (params.parse(wargc, wargv))
        return 0;
#if !defined(SOLID_ON_WINDOWS)
    signal(SIGPIPE, SIG_IGN);
#endif

    if (params.dbg_addr.size() && params.dbg_port.size()) {
        solid::log_start(
            params.dbg_addr.c_str(),
            params.dbg_port.c_str(),
            params.dbg_modules,
            params.dbg_buffered);

    } else if (params.dbg_console) {
        solid::log_start(std::cerr, params.dbg_modules);
    } else {
        solid::log_start(
            (envLogPathPrefix() + "\\log\\auth").c_str(),
            params.dbg_modules,
            params.dbg_buffered,
            3,
            1024 * 1024 * 64);
    }

    QApplication app(argc, argv);

    AioSchedulerT aioscheduler;

    frame::Manager  manager;
    frame::ServiceT service{manager};

    frame::mprpc::ServiceT front_rpc_service{manager};

    CallPool<void()>     cwp{WorkPoolConfiguration(), 1};
    frame::aio::Resolver resolver(cwp);

    client::store::MainWindow main_window;
    Engine                    engine(main_window, front_rpc_service, params);

    aioscheduler.start(1);

    prepare_application();

    main_window.start();

    front_configure_service(engine, params, front_rpc_service, aioscheduler, resolver);

    const int rv = app.exec();
    front_rpc_service.stop();
    return rv;
}

//-----------------------------------------------------------------------------
namespace {
bool Parameters::parse(ULONG argc, PWSTR* argv)
{
    using namespace boost::program_options;
    try {
        options_description desc("ola_client_store application");
        // clang-format off
		desc.add_options()
			("help,h", "List program options")
			("debug-modules,M", value<vector<string>>(&dbg_modules), "Debug logging modules (e.g. \".*:EW\", \"\\*:VIEW\")")
			("debug-address,A", value<string>(&dbg_addr), "Debug server address (e.g. on linux use: nc -l 9999)")
			("debug-port,P", value<string>(&dbg_port)->default_value("9999"), "Debug server port (e.g. on linux use: nc -l 9999)")
			("debug-console,C", value<bool>(&dbg_console)->implicit_value(true)->default_value(false), "Debug console")
			("debug-buffered,S", value<bool>(&dbg_buffered)->implicit_value(true)->default_value(false), "Debug unbuffered")
            ("front,f", value<std::string>(&front_endpoint), "Front Server endpoint: address:port")
			("secure,s", value<bool>(&secure)->implicit_value(true)->default_value(false), "Use SSL to secure communication")
			("compress", value<bool>(&compress)->implicit_value(true)->default_value(false), "Use Snappy to compress communication");
        // clang-format on
        variables_map vm;
        store(parse_command_line(argc, argv, desc), vm);
        notify(vm);
        if (vm.count("help")) {
            cout << desc << "\n";
            return true;
        }
        return false;
    } catch (exception& e) {
        cout << e.what() << "\n";
        return true;
    }
}

//-----------------------------------------------------------------------------

template <class M>
void complete_message(
    frame::mprpc::ConnectionContext& _rctx,
    std::shared_ptr<M>&              _rsent_msg_ptr,
    std::shared_ptr<M>&              _rrecv_msg_ptr,
    ErrorConditionT const&           _rerror)
{
    //solid_check(false); //this method should not be called
}

//-----------------------------------------------------------------------------
// Front
//-----------------------------------------------------------------------------
struct FrontSetup {
    template <class T>
    void operator()(front::ProtocolT& _rprotocol, TypeToType<T> _t2t, const front::ProtocolT::TypeIdT& _rtid)
    {
        _rprotocol.registerMessage<T>(complete_message<T>, _rtid);
    }
};

void front_configure_service(Engine& _rengine, const Parameters& _params, frame::mprpc::ServiceT& _rsvc, AioSchedulerT& _rsch, frame::aio::Resolver& _rres)
{
    auto                        proto = front::ProtocolT::create();
    frame::mprpc::Configuration cfg(_rsch, proto);

    front::protocol_setup(FrontSetup(), *proto);

    cfg.client.name_resolve_fnc = frame::mprpc::InternetResolverF(_rres, ola::front::default_port());

    cfg.client.connection_start_state     = frame::mprpc::ConnectionState::Passive;
    cfg.pool_max_active_connection_count  = 1;
    cfg.pool_max_pending_connection_count = 1;

    {
        auto connection_stop_lambda = [&_rengine](frame::mprpc::ConnectionContext& _ctx) {
            _rengine.onConnectionStop(_ctx);
        };
        auto connection_start_lambda = [&_rengine](frame::mprpc::ConnectionContext& _ctx) {
            _rengine.onConnectionStart(_ctx);
        };
        cfg.connection_stop_fnc         = std::move(connection_stop_lambda);
        cfg.client.connection_start_fnc = std::move(connection_start_lambda);
    }

    if (_params.secure) {
        frame::mprpc::openssl::setup_client(
            cfg,
            [](frame::aio::openssl::Context& _rctx) -> ErrorCodeT {
                _rctx.loadVerifyFile("ola-ca-cert.pem");
                _rctx.loadCertificateFile("ola-front-client-cert.pem");
                _rctx.loadPrivateKeyFile("ola-front-client-key.pem");
                return ErrorCodeT();
            },
            frame::mprpc::openssl::NameCheckSecureStart{"ola-front-server"});
    }

    if (_params.compress) {
        frame::mprpc::snappy::setup(cfg);
    }

    _rsvc.start(std::move(cfg));
    _rsvc.createConnectionPool(_params.front_endpoint.c_str(), 1);
}

void Engine::onAuthStart(const string& _user, const string& _pass)
{
    //on gui thread
    {
        lock_guard<mutex> lock(mutex_);
        front_auth_req_ptr_ = std::make_shared<front::AuthRequest>(_user, _pass);

        if (!front_recipient_id_.empty()) {
            auto lambda = [this](
                              frame::mprpc::ConnectionContext&      _rctx,
                              std::shared_ptr<front::AuthRequest>&  _rsent_msg_ptr,
                              std::shared_ptr<front::AuthResponse>& _rrecv_msg_ptr,
                              ErrorConditionT const&                _rerror) {
                onAuthResponse(_rctx, _rsent_msg_ptr, _rrecv_msg_ptr, _rerror);
            };
            if (!front_rpc_service_.sendRequest(front_recipient_id_, front_auth_req_ptr_, lambda)) {
                front_auth_req_ptr_.reset();
            }
        }
    }
}

void Engine::onConnectionStart(frame::mprpc::ConnectionContext& _ctx)
{
    auto req_ptr = std::make_shared<front::InitRequest>();
    auto lambda  = [this](
                      frame::mprpc::ConnectionContext&      _rctx,
                      std::shared_ptr<front::InitRequest>&  _rsent_msg_ptr,
                      std::shared_ptr<front::InitResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                _rerror) {
        if (_rrecv_msg_ptr) {
            if (_rrecv_msg_ptr->error_ == 0) {
                onConnectionInit(_rctx);
            } else {
                cout << "ERROR initiating connection: version " << _rctx.peerVersionMajor() << '.' << _rctx.peerVersionMinor() << " error " << _rrecv_msg_ptr->error_ << ':' << _rrecv_msg_ptr->message_ << endl;
            }
        }
    };

    _ctx.service().sendRequest(_ctx.recipientId(), req_ptr, lambda);
}

void Engine::onConnectionInit(frame::mprpc::ConnectionContext& _ctx)
{
    main_window_.offlineSignal(false);
    {
        lock_guard<mutex> lock(mutex_);
        front_recipient_id_ = _ctx.recipientId();
        if (front_auth_req_ptr_) {
            auto lambda = [this](
                              frame::mprpc::ConnectionContext&      _rctx,
                              std::shared_ptr<front::AuthRequest>&  _rsent_msg_ptr,
                              std::shared_ptr<front::AuthResponse>& _rrecv_msg_ptr,
                              ErrorConditionT const&                _rerror) {
                onAuthResponse(_rctx, _rsent_msg_ptr, _rrecv_msg_ptr, _rerror);
            };
            if (!front_rpc_service_.sendRequest(front_recipient_id_, front_auth_req_ptr_, lambda)) {
                front_auth_req_ptr_.reset();
            }
        }
    }
}
void Engine::onConnectionStop(frame::mprpc::ConnectionContext& _ctx)
{
    main_window_.offlineSignal(true);
    {
        lock_guard<mutex> lock(mutex_);
        if (front_recipient_id_ == _ctx.recipientId()) {
            front_recipient_id_.clear();
        }
    }
}

void Engine::onAuthResponse(
    frame::mprpc::ConnectionContext&      _rctx,
    std::shared_ptr<front::AuthRequest>&  _rsent_msg_ptr,
    std::shared_ptr<front::AuthResponse>& _rrecv_msg_ptr,
    ErrorConditionT const&                _rerror)
{
    if (_rrecv_msg_ptr) {
        solid_log(logger, Info, "AuthResponse: " << _rrecv_msg_ptr->error_ << " " << _rrecv_msg_ptr->message_);
        if (_rrecv_msg_ptr->error_) {
        }
    } else {
        solid_log(logger, Info, "No AuthResponse");
        //offline

        {
            lock_guard<mutex> lock(mutex_);
            front_auth_req_ptr_ = std::move(_rsent_msg_ptr);
        }
    }
}

void prepare_application()
{
 #ifdef Q_OS_WIN
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", QSettings::NativeFormat);
    if (settings.value("AppsUseLightTheme") == 0) {
        qApp->setStyle(QStyleFactory::create("Fusion"));
        QPalette darkPalette;
        QColor   darkColor     = QColor(45, 45, 45);
        QColor   disabledColor = QColor(127, 127, 127);
        darkPalette.setColor(QPalette::Window, darkColor);
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(18, 18, 18));
        darkPalette.setColor(QPalette::AlternateBase, darkColor);
        darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, disabledColor);
        darkPalette.setColor(QPalette::Button, darkColor);
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledColor);
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));

        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);
        darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledColor);

        qApp->setPalette(darkPalette);

        qApp->setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }");
    }
#endif
}

} //namespace
