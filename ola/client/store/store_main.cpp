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

#include "DarkStyle.h"
#include "framelesswindow.h"
#include "store_engine.hpp"
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

#include "boost/filesystem.hpp"
#include "boost/program_options.hpp"

#include <QApplication>
#include <QStyleFactory>
#include <QTranslator>
#include <QtGui>

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
using namespace ola::client::store;
namespace fs = boost::filesystem;

using AioSchedulerT = frame::Scheduler<frame::aio::Reactor>;
using SchedulerT    = frame::Scheduler<frame::Reactor>;

//-----------------------------------------------------------------------------
//      Parameters
//-----------------------------------------------------------------------------
namespace {

const solid::LoggerT logger("ola::client::store");

struct Parameters {
    vector<string> dbg_modules = {"ola::client::store.*:VIEW"};
    string         dbg_addr;
    string         dbg_port;
    bool           dbg_console  = false;
    bool           dbg_buffered = false;
    bool           secure;
    bool           compress;
    string         front_endpoint;
    string         secure_prefix;

    Parameters() {}

    bool parse(ULONG argc, PWSTR* argv);

    string securePath(const string& _name) const
    {
        return secure_prefix + '\\' + _name;
    }
};

struct Authenticator {
    using RecipientQueueT    = std::queue<frame::mprpc::RecipientId>;
    using OnOnlineFunctionT  = std::function<void()>;
    using OnOfflineFunctionT = std::function<void()>;
    frame::mprpc::ServiceT& front_rpc_service_;
    string                  path_prefix_;
    atomic<bool>            running_      = true;
    atomic<size_t>          active_count_ = 0;
    mutex                   mutex_;
    string                  user_;
    string                  token_;
    thread                  thread_;
    RecipientQueueT         recipient_q_;
    OnOnlineFunctionT       on_online_fnc_;
    OnOfflineFunctionT      on_offline_fnc_;

    Authenticator(
        frame::mprpc::ServiceT& _front_rpc_service,
        const string&           _path_prefix)
        : front_rpc_service_(_front_rpc_service)
        , path_prefix_(_path_prefix)
    {
        on_online_fnc_  = []() {};
        on_offline_fnc_ = []() {};
    }

    ~Authenticator()
    {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    fs::path authDataDirectoryPath() const
    {
        fs::path p = path_prefix_;
        p /= "config";
        return p;
    }

    fs::path authDataFilePath() const
    {
        return authDataDirectoryPath() / "auth.data";
    }
    bool loadAuth(string& _rendpoint, string& _ruser, string& _rtoken)
    {
        const auto path = authDataFilePath();
        ifstream   ifs(path.generic_string());

        if (ifs) {
            getline(ifs, _rendpoint);
            getline(ifs, _ruser);
            getline(ifs, _rtoken);
            try {
                _rtoken = ola::utility::base64_decode(_rtoken);
            } catch (std::exception& e) {
                _ruser.clear();
                _rtoken.clear();
            }
        }
        return !_ruser.empty() && !_rtoken.empty();
    }

    void poll();

    std::shared_ptr<front::AuthRequest> loadAuth(string& _rendpoint)
    {
        string user, token;
        if (loadAuth(_rendpoint, user, token)) {
            return make_shared<front::AuthRequest>(token);
        } else {
            return nullptr;
        }
    }

    void onConnectionInit(frame::mprpc::ConnectionContext& _ctx);
    void onConnectionStart(frame::mprpc::ConnectionContext& _ctx);
    void onConnectionStop(frame::mprpc::ConnectionContext& _ctx);

    void onAuthResponse(
        frame::mprpc::ConnectionContext&      _rctx,
        std::shared_ptr<front::AuthRequest>&  _rsent_msg_ptr,
        std::shared_ptr<front::AuthResponse>& _rrecv_msg_ptr,
        ErrorConditionT const&                _rerror);
};

void front_configure_service(Authenticator& _rauth, const Parameters& _params, frame::mprpc::ServiceT& _rsvc, AioSchedulerT& _rsch, frame::aio::Resolver& _rres);

//TODO: find a better name

string env_log_path_prefix()
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

string env_config_path_prefix()
{
#ifdef SOLID_ON_WINDOWS
    const char* v = getenv("APPDATA");
    if (v == nullptr) {
        v = getenv("LOCALAPPDATA");
        if (v == nullptr) {
            v = "c:";
        }
    }

    string r = v;
    r += "\\OLA";
    return r;
#else
    return get_home_env() + "/.ola";
#endif
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
            (env_log_path_prefix() + "\\log\\store").c_str(),
            params.dbg_modules,
            params.dbg_buffered,
            3,
            1024 * 1024 * 64);
    }

    QApplication           app(argc, argv);
    AioSchedulerT          aioscheduler;
    frame::Manager         manager;
    frame::ServiceT        service{manager};
    frame::mprpc::ServiceT front_rpc_service{manager};
    CallPool<void()>       cwp{WorkPoolConfiguration(), 1};
    frame::aio::Resolver   resolver(cwp);
    Authenticator          authenticator(front_rpc_service, env_config_path_prefix());
    Engine                 engine(front_rpc_service);
    FramelessWindow        frameless_window;
    MainWindow&            rmain_window = *(new MainWindow(engine, &frameless_window));

    authenticator.on_offline_fnc_ = [&frameless_window]() {
        frameless_window.setWindowTitle(QApplication::tr("Store - Offline"));
    };

    authenticator.on_online_fnc_ = [&frameless_window]() {
        frameless_window.setWindowTitle(QApplication::tr("Store"));
    };

    aioscheduler.start(1);

    //framelessWindow.setWindowState(Qt::WindowFullScreen);
    app.setStyle(new DarkStyle);

    frameless_window.setWindowIcon(app.style()->standardIcon(QStyle::SP_DesktopIcon));
    frameless_window.setContent(&rmain_window);
    frameless_window.setWindowTitle(QApplication::tr("Store"));

    front_configure_service(authenticator, params, front_rpc_service, aioscheduler, resolver);
    {
        Configuration config;
        config.language_       = "en-US";
        config.os_             = "Windows10x86_64";
        config.front_endpoint_ = params.front_endpoint;
        if (config.front_endpoint_.empty()) {
            string user;
            string token;
            solid_check(authenticator.loadAuth(config.front_endpoint_, user, token), "Failed to load authentication endpoint");
        }

        front_rpc_service.createConnectionPool(config.front_endpoint_.c_str(), 1);

        config.on_fetch_fnc_ = [&cwp, &rmain_window](
                                   const size_t   _index,
                                   const size_t   _count,
                                   string&&       _uname,
                                   string&&       _ucompany,
                                   string&&       _ubrief,
                                   vector<char>&& _uimage,
                                   const bool     _aquired,
                                   const bool     _owned,
                                   const bool     _default) {
            cwp.push(
                [_index, _count, &rmain_window, name = std::move(_uname), company = std::move(_ucompany), brief = std::move(_ubrief), image = std::move(_uimage), _aquired, _owned, _default]() {
                    rmain_window.model().prepareAndPushItem(_index, _count, name, company, brief, image, _aquired, _owned, _default);
                });
        };
        config.on_fetch_error_fnc_ = [&cwp, &rmain_window](
                                         const size_t _index,
                                         const size_t _count) {
            cwp.push(
                [_index, _count, &rmain_window]() {
                    rmain_window.model().prepareAndPushItem(_index, _count);
                });
        };

        engine.start(std::move(config));
    }

    frameless_window.setWindowIcon(QIcon(":/images/ola_store_bag.ico"));
    frameless_window.show();

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
            ("front,f", value<std::string>(&front_endpoint)->default_value(""), "Front Server endpoint: address:port")
			("compress", value<bool>(&compress)->implicit_value(true)->default_value(false), "Use Snappy to compress communication")
            ("unsecure", value<bool>(&secure)->implicit_value(false)->default_value(true), "Use SSL to secure communication")
            ("secure-prefix", value<std::string>(&secure_prefix)->default_value("certs"), "Secure Path prefix")
        ;
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

void front_configure_service(Authenticator& _rauth, const Parameters& _params, frame::mprpc::ServiceT& _rsvc, AioSchedulerT& _rsch, frame::aio::Resolver& _rres)
{
    auto                        proto = front::ProtocolT::create();
    frame::mprpc::Configuration cfg(_rsch, proto);

    front::protocol_setup(FrontSetup(), *proto);

    cfg.client.name_resolve_fnc = frame::mprpc::InternetResolverF(_rres, ola::front::default_port());

    cfg.client.connection_start_state     = frame::mprpc::ConnectionState::Passive;
    cfg.pool_max_active_connection_count  = 2;
    cfg.pool_max_pending_connection_count = 2;

    cfg.connection_stop_fnc = [&_rauth](frame::mprpc::ConnectionContext& _rctx) {
        _rauth.onConnectionStop(_rctx);
    };
    cfg.client.connection_start_fnc = [&_rauth](frame::mprpc::ConnectionContext& _rctx) {
        _rauth.onConnectionStart(_rctx);
    };

    if (_params.secure) {
        frame::mprpc::openssl::setup_client(
            cfg,
            [_params](frame::aio::openssl::Context& _rctx) -> ErrorCodeT {
                solid_log(logger, Info, "Secure path: " << _params.securePath("ola-ca-cert.pem"));
                _rctx.loadVerifyFile(_params.securePath("ola-ca-cert.pem").c_str());
                _rctx.loadCertificateFile(_params.securePath("ola-client-front-cert.pem").c_str());
                _rctx.loadPrivateKeyFile(_params.securePath("ola-client-front-key.pem").c_str());
                return ErrorCodeT();
            },
            frame::mprpc::openssl::NameCheckSecureStart{"ola-server"});
    }

    if (_params.compress) {
        frame::mprpc::snappy::setup(cfg);
    }

    _rsvc.start(std::move(cfg));
    //_rsvc.createConnectionPool(_params.front_endpoint.c_str(), 1);
}

void Authenticator::onConnectionStart(frame::mprpc::ConnectionContext& _ctx)
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
                solid_log(logger, Error, "Initiating connection: version " << _rctx.peerVersionMajor() << '.' << _rctx.peerVersionMinor() << " error " << _rrecv_msg_ptr->error_ << ':' << _rrecv_msg_ptr->message_);
            }
        }
    };

    _ctx.service().sendRequest(_ctx.recipientId(), req_ptr, lambda);
}
void Authenticator::poll()
{
    string endpoint, user, token;
    while (running_) {
        this_thread::sleep_for(chrono::seconds(1));
        if (loadAuth(endpoint, user, token)) {
            auto auth_ptr = make_shared<front::AuthRequest>(user, token);
            auto lambda   = [this](
                              frame::mprpc::ConnectionContext&      _rctx,
                              std::shared_ptr<front::AuthRequest>&  _rsent_msg_ptr,
                              std::shared_ptr<front::AuthResponse>& _rrecv_msg_ptr,
                              ErrorConditionT const&                _rerror) {
                if (!_rerror && _rrecv_msg_ptr->error_) {
                    recipient_q_.pop();
                    auto lambda = [this](
                                      frame::mprpc::ConnectionContext&      _rctx,
                                      std::shared_ptr<front::AuthRequest>&  _rsent_msg_ptr,
                                      std::shared_ptr<front::AuthResponse>& _rrecv_msg_ptr,
                                      ErrorConditionT const&                _rerror) {
                        onAuthResponse(_rctx, _rsent_msg_ptr, _rrecv_msg_ptr, _rerror);
                    };

                    while (!recipient_q_.empty()) {
                        front_rpc_service_.sendRequest(recipient_q_.front(), _rsent_msg_ptr, lambda);
                    }
                }

                onAuthResponse(_rctx, _rsent_msg_ptr, _rrecv_msg_ptr, _rerror);
            };
            while (!recipient_q_.empty()) {
                const auto err = front_rpc_service_.sendRequest(recipient_q_.front(), auth_ptr, lambda);
                if (err) {
                    recipient_q_.pop();
                } else {
                    return;
                }
            }
        }
    }
}
void Authenticator::onConnectionInit(frame::mprpc::ConnectionContext& _rctx)
{
    {
        lock_guard<mutex> lock(mutex_);
        if (!recipient_q_.empty()) {
            recipient_q_.emplace(_rctx.recipientId());
            return;
        }
        string endpoint;
        auto   auth_ptr = loadAuth(endpoint);

        if (auth_ptr) {
            auto lambda = [this](
                              frame::mprpc::ConnectionContext&      _rctx,
                              std::shared_ptr<front::AuthRequest>&  _rsent_msg_ptr,
                              std::shared_ptr<front::AuthResponse>& _rrecv_msg_ptr,
                              ErrorConditionT const&                _rerror) {
                onAuthResponse(_rctx, _rsent_msg_ptr, _rrecv_msg_ptr, _rerror);
            };
            front_rpc_service_.sendRequest(_rctx.recipientId(), auth_ptr, lambda);
        } else {
            recipient_q_.emplace(_rctx.recipientId());
            if (thread_.joinable()) {
                thread_.join();
            }
            thread_ = std::thread(&Authenticator::poll, this);
        }
    }
}
void Authenticator::onConnectionStop(frame::mprpc::ConnectionContext& _rctx)
{
    if (_rctx.isConnectionActive()) {
        if (active_count_.fetch_sub(1) == 1) {
            on_offline_fnc_();
        }
    }
}

void Authenticator::onAuthResponse(
    frame::mprpc::ConnectionContext&      _rctx,
    std::shared_ptr<front::AuthRequest>&  _rsent_msg_ptr,
    std::shared_ptr<front::AuthResponse>& _rrecv_msg_ptr,
    ErrorConditionT const&                _rerror)
{
    if (_rrecv_msg_ptr && _rrecv_msg_ptr->error_ == 0) {
        solid_log(logger, Info, "AuthResponse: " << _rrecv_msg_ptr->error_);
        _rctx.service().connectionNotifyEnterActiveState(_rctx.recipientId());
        if (active_count_.fetch_add(1) == 0) {
            on_online_fnc_();
        }
    } else {
        solid_log(logger, Info, "No AuthResponse");
        lock_guard<mutex> lock(mutex_);

        recipient_q_.emplace(_rctx.recipientId());

        if (recipient_q_.size() == 1) {
            if (thread_.joinable()) {
                thread_.join();
            }
            thread_ = std::thread(&Authenticator::poll, this);
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
