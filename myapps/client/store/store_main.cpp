// myapps/client/store/store_main.cpp

// This file is part of MyApps.directory project
// Copyright (C) 2020, 2021, 2022, 2023, 2024, 2025 Valentin Palade (vipalade @ gmail . com)

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

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
#include "solid/frame/mprpc/mprpcprotocol_serialization_v3.hpp"
#include "solid/frame/mprpc/mprpcservice.hpp"
#include "solid/frame/mprpc/mprpcsocketstub_openssl.hpp"

#include "solid/utility/threadpool.hpp"

#include "myapps/common/utility/encode.hpp"
#include "myapps/common/utility/version.hpp"

#include "myapps/client/utility/auth_file.hpp"
#include "myapps/client/utility/file_monitor.hpp"
#include "myapps/client/utility/locale.hpp"

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

using namespace myapps;
using namespace solid;
using namespace std;
using namespace myapps::front;
using namespace myapps::client::store;
namespace fs = boost::filesystem;

//-----------------------------------------------------------------------------
//      Parameters
//-----------------------------------------------------------------------------
namespace {

constexpr string_view service_name("myapps_client_store");
const solid::LoggerT  logger("myapps::client::store");

using AioSchedulerT = frame::Scheduler<frame::aio::Reactor<frame::mprpc::EventT>>;
using SchedulerT    = frame::Scheduler<frame::Reactor<Event<32>>>;
using CallPoolT     = ThreadPool<Function<void()>, Function<void()>>;

struct Parameters {
    vector<string> debug_modules;
    string         debug_addr;
    string         debug_port;
    bool           debug_console;
    bool           debug_buffered;
    bool           secure;
    bool           compress;
    string         front_endpoint;
    string         secure_prefix;
    string         path_prefix;

    string securePath(const string& _name) const
    {
        return secure_prefix + '\\' + _name;
    }

    string configPath(const string& _path_prefix) const;

    bool                                  parse(ULONG argc, PWSTR* argv);
    boost::program_options::variables_map bootstrapCommandLine(ULONG argc, PWSTR* argv);
    void                                  writeConfigurationFile(string _path, const boost::program_options::options_description& _od, const boost::program_options::variables_map& _vm) const;
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

    fs::path configDirectoryPath() const
    {
        fs::path p = path_prefix_;
        p /= "config";
        return p;
    }

    fs::path authDataFilePath() const
    {
        return configDirectoryPath() / "auth.data";
    }

    fs::path appListFilePath() const
    {
        return configDirectoryPath() / "app_list.data";
    }

    bool loadAuth(string& _rendpoint, string& _ruser, string& _rtoken)
    {
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

    solid::frame::mprpc::MessagePointerT<core::AuthRequest> loadAuth(string& _rendpoint)
    {
        string user, token;
        if (loadAuth(_rendpoint, user, token)) {
            auto ptr   = frame::mprpc::make_message<core::AuthRequest>();
            ptr->pass_ = token;
            return ptr;
        } else {
            return {};
        }
    }

    void onConnectionInit(frame::mprpc::ConnectionContext& _ctx);
    void onConnectionStart(frame::mprpc::ConnectionContext& _ctx);
    void onConnectionStop(frame::mprpc::ConnectionContext& _ctx);

    void onAuthResponse(
        frame::mprpc::ConnectionContext&                          _rctx,
        solid::frame::mprpc::MessagePointerT<core::AuthRequest>&  _rsent_msg_ptr,
        solid::frame::mprpc::MessagePointerT<core::AuthResponse>& _rrecv_msg_ptr,
        ErrorConditionT const&                                    _rerror);
};

void front_configure_service(Authenticator& _rauth, const Parameters& _params, frame::mprpc::ServiceT& _rsvc, AioSchedulerT& _rsch, frame::aio::Resolver& _rres);

// TODO: find a better name

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
    r += "\\MyApps.dir\\client";
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
    r += "\\MyApps.dir";
    return r;
#else
    return get_home_env() + "/.myapps.dir";
#endif
}

void   prepare_application();
string get_myapps_filesystem_path();

} // namespace

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
        const auto m_singleInstanceMutex = CreateMutex(NULL, TRUE, L"MYAPPS_STORE_SHARED_MUTEX");
        if (m_singleInstanceMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND existingApp = FindWindow(0, L"MyApps.directory Store");
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

    if (!params.parse(wargc, wargv))
        return 0;
#if !defined(SOLID_ON_WINDOWS)
    signal(SIGPIPE, SIG_IGN);
#endif

    if (params.debug_addr.size() && params.debug_port.size()) {
        solid::log_start(
            params.debug_addr.c_str(),
            params.debug_port.c_str(),
            params.debug_modules,
            params.debug_buffered);

    } else if (params.debug_console) {
        solid::log_start(std::cerr, params.debug_modules);
    } else {
        solid::log_start(
            (env_log_path_prefix() + "\\log\\store").c_str(),
            params.debug_modules,
            params.debug_buffered,
            3,
            1024 * 1024 * 64);
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QApplication app(argc, argv);

    app.setStyle("fusion");

    AioSchedulerT                aioscheduler;
    frame::Manager               manager;
    frame::ServiceT              service{manager};
    frame::mprpc::ServiceT       front_rpc_service{manager};
    CallPoolT                    cwp{{1, 1000, 0}, [](const size_t) {}, [](const size_t) {}};
    frame::aio::Resolver         resolver([&cwp](std::function<void()>&& _fnc) { cwp.pushOne(std::move(_fnc)); });
    client::utility::FileMonitor file_monitor_;
    Authenticator                authenticator(front_rpc_service, file_monitor_, env_config_path_prefix(), []() { QApplication::exit(); });
    Engine                       engine(front_rpc_service);
    MainWindow                   main_window(engine);

    authenticator.on_offline_fnc_ = [&main_window]() {
        // main_window.setWindowTitle(QApplication::tr("MyApps.directory Store - Offline"));
        main_window.onlineSignal(false);
    };

    authenticator.on_online_fnc_ = [&main_window]() {
        // main_window.setWindowTitle(QApplication::tr("MyApps.directory Store"));
        main_window.onlineSignal(true);
    };

    file_monitor_.start();
    aioscheduler.start(1);

    main_window.setWindowIcon(app.style()->standardIcon(QStyle::SP_DesktopIcon));
    main_window.setWindowTitle(QApplication::tr("MyApps.directory Store"));

    front_configure_service(authenticator, params, front_rpc_service, aioscheduler, resolver);
    {
        Configuration config;
        config.language_           = "en-US";
        config.os_                 = "Windows10x86_64";
        config.front_endpoint_     = params.front_endpoint;
        config.app_list_file_path_ = authenticator.appListFilePath();
        config.myapps_fs_path_     = get_myapps_filesystem_path();
        if (config.front_endpoint_.empty()) {
            lock_guard<mutex> lock(authenticator.mutex_);
            string            user;
            string            token;
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
                                   string&&       _ubuild_id,
                                   vector<char>&& _uimage,
                                   const uint32_t _flags) {
            cwp.pushOne(
                [_index, _count, &main_window, name = std::move(_uname), company = std::move(_ucompany), brief = std::move(_ubrief), build_id = std::move(_ubuild_id), image = std::move(_uimage), _flags]() {
                    main_window.model().prepareAndPushItem(_index, _count, name, company, brief, build_id, image, _flags);
                });
        };
        config.on_fetch_error_fnc_ = [&cwp, &main_window](
                                         const size_t _index,
                                         const size_t _count) {
            cwp.pushOne(
                [_index, _count, &main_window]() {
                    main_window.model().prepareAndPushItem(_index, _count);
                });
        };

        engine.start(std::move(config));
    }

    main_window.setWindowIcon(QIcon(":/images/store_bag.ico"));
    main_window.show();

    SetWindowText(GetActiveWindow(), L"MyApps.directory Store");

    const int rv = app.exec();
    front_rpc_service.stop();
    return rv;
}
//-----------------------------------------------------------------------------

namespace std {
std::ostream& operator<<(std::ostream& os, const std::vector<string>& vec)
{
    for (auto item : vec) {
        os << item << ",";
    }
    return os;
}
} // namespace std

//-----------------------------------------------------------------------------

namespace {
//-----------------------------------------------------------------------------
// Parameters
//-----------------------------------------------------------------------------
string Parameters::configPath(const std::string& _path_prefix) const
{
    return _path_prefix + "\\config\\" + string(service_name) + ".config";
}
//-----------------------------------------------------------------------------
string get_myapps_filesystem_path()
{
    static const string home_path = getenv("USERPROFILE");
    return home_path + "\\MyApps.dir";
}
//-----------------------------------------------------------------------------
boost::program_options::variables_map Parameters::bootstrapCommandLine(ULONG argc, PWSTR* argv)
{
    using namespace boost::program_options;
    boost::program_options::options_description desc{"Bootstrap Options"};
    // clang-format off
    desc.add_options()
        ("version,v", "Version string")
        ("help,h", "Help Message")
        ("config,c", value<string>(), "Configuration File Path")
        ("generate-config", value<bool>()->implicit_value(true)->default_value(false), "Write configuration file and exit")
        ;
    // clang-format off
    variables_map vm;
    boost::program_options::store(basic_command_line_parser(argc, argv).options(desc).allow_unregistered().run(), vm);
    notify(vm);
    return vm;
}

bool Parameters::parse(ULONG argc, PWSTR* argv)
{
    using namespace boost::program_options;
    try {
        string              config_file_path;
        bool                generate_config_file;
        options_description generic(string(service_name) + " generic options");
        // clang-format off
        generic.add_options()
            ("version,v", "Version string")
            ("help,h", "Help Message")
            ("config,c", value<string>(&config_file_path), "Configuration File Path")
            ("generate-config", value<bool>(&generate_config_file)->implicit_value(true)->default_value(false), "Write configuration file and exit")
            ;
        // clang-format on
        options_description config(string(service_name) + " configuration options");
        // clang-format off
        config.add_options()
            ("debug-modules,M", value<std::vector<std::string>>(&this->debug_modules)->default_value(std::vector<std::string>{"myapps::.*:VIEW", ".*:EWX"}), "Debug logging modules")
            ("debug-address,A", value<string>(&debug_addr)->default_value(""), "Debug server address (e.g. on linux use: nc -l 9999)")
            ("debug-port,P", value<string>(&debug_port)->default_value("9999"), "Debug server port (e.g. on linux use: nc -l 9999)")
            ("debug-console,C", value<bool>(&debug_console)->implicit_value(true)->default_value(false), "Debug console")
            ("debug-buffered,S", value<bool>(&this->debug_buffered)->implicit_value(true)->default_value(true), "Debug unbuffered")
            ("secure,s", value<bool>(&secure)->implicit_value(true)->default_value(true), "Use SSL to secure communication")
            ("compress", value<bool>(&compress)->implicit_value(true)->default_value(false), "Use Snappy to compress communication")
            ("secure-prefix", value<std::string>(&secure_prefix)->default_value("certs"), "Secure Path prefix")
            ("path-prefix", value<std::string>(&path_prefix)->default_value(env_config_path_prefix()), "Path prefix")
            ("front,f", value<std::string>(&front_endpoint)->default_value(""), "MyApps.directory Front Endpoint")
            ;
        // clang-format off

        options_description cmdline_options;
        cmdline_options.add(generic).add(config);

        options_description config_file_options;
        config_file_options.add(config);

        options_description visible("Allowed options");
        visible.add(generic).add(config);

        variables_map vm;
        boost::program_options::store(basic_command_line_parser(argc, argv).options(cmdline_options).run(), vm);

        auto bootstrap = bootstrapCommandLine(argc, argv);

        if (bootstrap.count("help") != 0u) {
            cout << visible << endl;
            return false;
        }

        if (bootstrap.count("version") != 0u) {
            cout << myapps::utility::version_full() << endl;
            cout << "SolidFrame: " << solid::version_full() << endl;
            return false;
        }

        string cfg_path;

        if (bootstrap.count("config")) {
            cfg_path = bootstrap["config"].as<std::string>();
        }

        if (cfg_path.empty()) {
            string prefix;
            if (bootstrap.count("path-prefix")) {
                prefix = bootstrap["path-prefix"].as<std::string>();
            }
            else {
                prefix = env_config_path_prefix();
            }
            cfg_path = configPath(prefix);
        }

        generate_config_file = bootstrap["generate-config"].as<bool>();

        if (generate_config_file) {
            writeConfigurationFile(cfg_path, config_file_options, vm);
            return false;
        }

        if (!cfg_path.empty()) {
            ifstream ifs(cfg_path);
            if (!ifs) {
                cout << "cannot open config file: " << cfg_path << endl;
                if (bootstrap.count("config")) {
                    //exit only if the config path was explicitly given
                    return false;
                }
            }
            else {
                boost::program_options::store(parse_config_file(ifs, config_file_options), vm);
            }
        }

        notify(vm);
    }
    catch (exception& e) {
        cout << e.what() << "\n";
        return false;
    }
    return true;
}
//-----------------------------------------------------------------------------
void write_value(std::ostream& _ros, const string& _name, const boost::any& _rav)
{
    if (_rav.type() == typeid(bool)) {
        _ros << _name << '=' << boost::any_cast<bool>(_rav) << endl;
    }
    else if (_rav.type() == typeid(uint16_t)) {
        _ros << _name << '=' << boost::any_cast<uint16_t>(_rav) << endl;
    }
    else if (_rav.type() == typeid(uint32_t)) {
        _ros << _name << '=' << boost::any_cast<uint32_t>(_rav) << endl;
    }
    else if (_rav.type() == typeid(uint64_t)) {
        _ros << _name << '=' << boost::any_cast<uint64_t>(_rav) << endl;
    }
    else if (_rav.type() == typeid(std::string)) {
        _ros << _name << '=' << boost::any_cast<std::string>(_rav) << endl;
    }
    else if (_rav.type() == typeid(std::wstring)) {
        _ros << _name << '=' << myapps::client::utility::narrow(boost::any_cast<std::wstring>(_rav)) << endl;
    }
    else if (_rav.type() == typeid(std::vector<std::string>)) {
        const auto& v = boost::any_cast<const std::vector<std::string>&>(_rav);
        for (const auto& val : v) {
            _ros << _name << '=' << val << endl;
        }
        if (v.empty()) {
            _ros << '#' << _name << '=' << endl;
        }
    }
    else {
        _ros << _name << '=' << "<UNKNOWN-TYPE>" << endl;
    }
}
//-----------------------------------------------------------------------------
void Parameters::writeConfigurationFile(string _path, const boost::program_options::options_description& _od, const boost::program_options::variables_map& _vm)const
{
    if (boost::filesystem::exists(_path)) {

        cout << "File \"" << _path << "\" already exists - renamed to: " << _path << ".old" << endl;

        boost::filesystem::rename(_path, _path + ".old");
    }

    ofstream ofs(_path);

    if (!ofs) {
        cout << "Could not open file \"" << _path << "\" for writing" << endl;
        return;
    }
    if (!_od.options().empty()) {
        ofs << '#' << " " << service_name << " configuration file" << endl;
        ofs << endl;

        for (auto& opt : _od.options()) {
            ofs << '#' << ' ' << opt->description() << endl;
            const auto& val = _vm[opt->long_name()];
            write_value(ofs, opt->long_name(), val.value());
            ofs << endl;
        }
    }
    ofs.flush();
    ofs.close();
    cout << service_name << " configuration file writen: " << _path << endl;
}

//-----------------------------------------------------------------------------

template <class M>
void complete_message(
    frame::mprpc::ConnectionContext& _rctx,
    solid::frame::mprpc::MessagePointerT<M>&              _rsent_msg_ptr,
    solid::frame::mprpc::MessagePointerT<M>&              _rrecv_msg_ptr,
    ErrorConditionT const&           _rerror)
{
    //solid_check(false); //this method should not be called
}

//-----------------------------------------------------------------------------
// Front
//-----------------------------------------------------------------------------

void front_configure_service(Authenticator& _rauth, const Parameters& _params, frame::mprpc::ServiceT& _rsvc, AioSchedulerT& _rsch, frame::aio::Resolver& _rres)
{
    auto                        proto = frame::mprpc::serialization_v3::create_protocol<reflection::v1::metadata::Variant, myapps::front::ProtocolTypeIdT>(
        myapps::utility::metadata_factory,
        [&](auto& _rmap) {
            auto lambda = [&](const myapps::front::ProtocolTypeIdT _id, const std::string_view _name, auto const& _rtype) {
                using TypeT = typename std::decay_t<decltype(_rtype)>::TypeT;
                _rmap.template registerMessage<TypeT>(_id, _name, complete_message<TypeT>);
            };
            myapps::front::core::configure_protocol(lambda);
            myapps::front::main::configure_protocol(lambda);
            myapps::front::store::configure_protocol(lambda);
        }
    );
    frame::mprpc::Configuration cfg(_rsch, proto);

    cfg.client.name_resolve_fnc = frame::mprpc::InternetResolverF(_rres, myapps::front::default_port());

    cfg.client.connection_start_state     = frame::mprpc::ConnectionState::Passive;
    cfg.client.connection_timeout_keepalive = std::chrono::seconds(30);
    cfg.pool_max_active_connection_count  = 2;
    cfg.pool_max_pending_connection_count = 2;
    cfg.connection_recv_buffer_start_capacity_kb = myapps::utility::client_connection_recv_buffer_start_capacity_kb;
    cfg.connection_send_buffer_start_capacity_kb = myapps::utility::client_connection_send_buffer_start_capacity_kb;


    cfg.connection_stop_fnc = [&_rauth](frame::mprpc::ConnectionContext& _rctx) {
        _rauth.onConnectionStop(_rctx);
    };
    cfg.client.connection_start_fnc = [&_rauth](frame::mprpc::ConnectionContext& _rctx) {
        _rctx.any() = std::make_tuple(core::version, main::version, store::version, myapps::utility::version);
        _rauth.onConnectionStart(_rctx);
    };

    if (_params.secure) {
        frame::mprpc::openssl::setup_client(
            cfg,
            [_params](frame::aio::openssl::Context& _rctx) -> ErrorCodeT {
                solid_log(logger, Error, "Secure path: " << _params.securePath("ola-ca-cert.pem"));
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
    auto req_ptr = frame::mprpc::make_message<store::InitRequest>();
    auto lambda  = [this](
                      frame::mprpc::ConnectionContext&      _rctx,
                      solid::frame::mprpc::MessagePointerT<store::InitRequest>&  _rsent_msg_ptr,
                      solid::frame::mprpc::MessagePointerT<core::InitResponse>& _rrecv_msg_ptr,
                      ErrorConditionT const&                _rerror) {
        if (_rrecv_msg_ptr) {
            if (_rrecv_msg_ptr->error_ == 0) {
                onConnectionInit(_rctx);
            } else {
                solid_log(logger, Error, "Initiating connection: version error " << _rrecv_msg_ptr->error_ << ':' << _rrecv_msg_ptr->message_);
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
    
    myapps::client::utility::auth_read(authDataFilePath(), endpoint, user, token);

    solid_log(logger, Info, "new auth data = " << endpoint << " " << user << " old auth data = " << endpoint_ << " " << user_);

    if ((endpoint_.empty() || endpoint == endpoint_) && (user_.empty() || user == user_)) {
        endpoint_ = endpoint;
        user_     = user;

        if (token.empty()) { //logged out
            token_.clear();
            stop_fnc_();
            return;
        } else {
            token_ = token;
            while (recipient_q_.size()) {
                string endpoint;
                auto   auth_ptr = loadAuth(endpoint);

                if (auth_ptr) {
                    auto lambda = [this](
                        frame::mprpc::ConnectionContext& _rctx,
                        solid::frame::mprpc::MessagePointerT<core::AuthRequest>& _rsent_msg_ptr,
                        solid::frame::mprpc::MessagePointerT<core::AuthResponse>& _rrecv_msg_ptr,
                        ErrorConditionT const& _rerror) {
                            onAuthResponse(_rctx, _rsent_msg_ptr, _rrecv_msg_ptr, _rerror);
                    };
                    front_rpc_service_.sendRequest(recipient_q_.front(), auth_ptr, lambda);
                    recipient_q_.pop();
                }
                else {
                    break;
                }
            }
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
        lock_guard<mutex> lock(mutex_);
        string endpoint;
        auto   auth_ptr = loadAuth(endpoint);

        if (auth_ptr) {
            auto lambda = [this](
                              frame::mprpc::ConnectionContext&      _rctx,
                              solid::frame::mprpc::MessagePointerT<core::AuthRequest>&  _rsent_msg_ptr,
                              solid::frame::mprpc::MessagePointerT<core::AuthResponse>& _rrecv_msg_ptr,
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
    solid::frame::mprpc::MessagePointerT<core::AuthRequest>&  _rsent_msg_ptr,
    solid::frame::mprpc::MessagePointerT<core::AuthResponse>& _rrecv_msg_ptr,
    ErrorConditionT const&                _rerror)
{
    if (!_rrecv_msg_ptr) {
        return;//do nothing - connection will close
    }

    if (_rrecv_msg_ptr->error_ == 0) {
        solid_log(logger, Info, "AuthResponse OK");
        _rctx.service().connectionNotifyEnterActiveState(_rctx.recipientId());
        if (active_count_.fetch_add(1) == 0) {
            on_online_fnc_();
        }
    } else {
        solid_log(logger, Info, "AuthResponse Error: " << _rrecv_msg_ptr->error_);
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
