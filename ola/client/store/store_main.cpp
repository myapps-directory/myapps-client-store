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

#include "ola/client/utility/auth_file.hpp"
#include "ola/client/utility/file_monitor.hpp"

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
    using StopFunctionT      = std::function<void()>;

    frame::mprpc::ServiceT&       front_rpc_service_;
    client::utility::FileMonitor& rfile_monitor_;
    string                        path_prefix_;
    atomic<bool>                  running_      = true;
    atomic<size_t>                active_count_ = 0;
    mutex                         mutex_;
    string                        endpoint_;
    string                        user_;
    string                        token_;
    RecipientQueueT               recipient_q_;
    OnOnlineFunctionT             on_online_fnc_;
    OnOfflineFunctionT            on_offline_fnc_;
    StopFunctionT                 stop_fnc_;

    template <class StopFnc>
    Authenticator(
        frame::mprpc::ServiceT&       _front_rpc_service,
        client::utility::FileMonitor& _rfile_monitor,
        const string&                 _path_prefix,
        StopFnc                       _stop_fnc)
        : front_rpc_service_(_front_rpc_service)
        , rfile_monitor_(_rfile_monitor)
        , path_prefix_(_path_prefix)
        , stop_fnc_(_stop_fnc)
    {
        on_online_fnc_  = []() {};
        on_offline_fnc_ = []() {};

        rfile_monitor_.add(
            authDataFilePath(),
            [this](const fs::path& _dir, const fs::path& _name, const chrono::system_clock::time_point& _time_point) mutable {
                onAuthFileChange();
            });
    }

    ~Authenticator()
    {
        running_ = false;
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
        lock_guard<mutex> lock(mutex_);
        if (!token_.empty()) {
            _rendpoint = endpoint_;
            _ruser     = user_;
            _rtoken    = token_;
            return true;
        } else {
            _rendpoint.clear();
            _ruser.clear();
            _rtoken.clear();
            return false;
        }
    }

    void onAuthFileChange();

    std::shared_ptr<front::AuthRequest> loadAuth(string& _rendpoint)
    {
        string user, token;
        if (loadAuth(_rendpoint, user, token)) {
            auto ptr   = make_shared<front::AuthRequest>();
            ptr->pass_ = token;
            return ptr;
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

    {
        const auto m_singleInstanceMutex = CreateMutex(NULL, TRUE, L"OLA_STORE_SHARED_MUTEX");
        if (m_singleInstanceMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND existingApp = FindWindow(0, L"MyApps.space Store");
            if (existingApp) {
                SetForegroundWindow(existingApp);
            }
            return -1; // Exit the app. For MFC, return false from InitInstance.
        }
    }
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

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    //QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QApplication                 app(argc, argv);
    AioSchedulerT                aioscheduler;
    frame::Manager               manager;
    frame::ServiceT              service{manager};
    frame::mprpc::ServiceT       front_rpc_service{manager};
    CallPool<void()>             cwp{WorkPoolConfiguration(), 1};
    frame::aio::Resolver         resolver(cwp);
    client::utility::FileMonitor file_monitor_;
    Authenticator                authenticator(front_rpc_service, file_monitor_, env_config_path_prefix(), []() { QApplication::exit(); });
    Engine                       engine(front_rpc_service);
    MainWindow                   main_window(engine);

    authenticator.on_offline_fnc_ = [&main_window]() {
        //main_window.setWindowTitle(QApplication::tr("MyApps.space Store - Offline"));
    };

    authenticator.on_online_fnc_ = [&main_window]() {
        //main_window.setWindowTitle(QApplication::tr("MyApps.space Store"));
    };

    file_monitor_.start();
    aioscheduler.start(1);

    main_window.setWindowIcon(app.style()->standardIcon(QStyle::SP_DesktopIcon));
    main_window.setWindowTitle(QApplication::tr("MyApps.space Store"));

    front_configure_service(authenticator, params, front_rpc_service, aioscheduler, resolver);
    {
        Configuration config;
        config.language_       = "en-US";
        config.os_             = "Windows10x86_64";
        config.front_endpoint_ = params.front_endpoint;
        if (config.front_endpoint_.empty()) {
            string user;
            string token;
            if (!authenticator.loadAuth(config.front_endpoint_, user, token)) {
                return 0;
            }
        }

        front_rpc_service.createConnectionPool(config.front_endpoint_.c_str(), 1);

        config.on_fetch_fnc_ = [&cwp, &main_window](
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
                [_index, _count, &main_window, name = std::move(_uname), company = std::move(_ucompany), brief = std::move(_ubrief), image = std::move(_uimage), _aquired, _owned, _default]() {
                    main_window.model().prepareAndPushItem(_index, _count, name, company, brief, image, _aquired, _owned, _default);
                });
        };
        config.on_fetch_error_fnc_ = [&cwp, &main_window](
                                         const size_t _index,
                                         const size_t _count) {
            cwp.push(
                [_index, _count, &main_window]() {
                    main_window.model().prepareAndPushItem(_index, _count);
                });
        };

        engine.start(std::move(config));
    }

    main_window.setWindowIcon(QIcon(":/images/ola_store_bag.ico"));
    main_window.show();

    SetWindowText(GetActiveWindow(), L"MyApps.space Store");

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
                //_rctx.loadCertificateFile(_params.securePath("ola-client-front-cert.pem").c_str());
                //_rctx.loadPrivateKeyFile(_params.securePath("ola-client-front-key.pem").c_str());
                return ErrorCodeT();
            },
            frame::mprpc::openssl::NameCheckSecureStart{"front.myapps.space"});
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

void Authenticator::onAuthFileChange()
{
    unique_lock<mutex> lock(mutex_);
    string             endpoint;
    string             user;
    string             token;
    ola::client::utility::auth_read(authDataFilePath(), endpoint, user, token);

    if ((endpoint_.empty() || endpoint == endpoint_) && (user_.empty() || user == user_)) {
        endpoint_ = endpoint;
        user_     = user;

        if (token.empty()) { //logged out
            token_.clear();
            stop_fnc_();
            return;
        } else {
            token_ = token;
        }
    } else {
        token_.clear();
        stop_fnc_();
        return;
    }
}

void Authenticator::onConnectionInit(frame::mprpc::ConnectionContext& _rctx)
{
    {
        {

            lock_guard<mutex> lock(mutex_);
            if (!recipient_q_.empty()) {
                recipient_q_.emplace(_rctx.recipientId());
                return;
            }
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
