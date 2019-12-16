#undef UNICODE
#define UNICODE
#undef _WINSOCKAPI_
//#define _WINSOCKAPI_
#ifndef NOMINMAX
#define NOMINMAX
#endif #undef UNICODE
#define UNICODE
#undef _WINSOCKAPI_
//#define _WINSOCKAPI_
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include "boost/filesystem.hpp"
#include "boost/process.hpp"
#include "boost/program_options.hpp"

#include <wtsapi32.h>
#pragma comment(lib, "wtsapi32.lib")
#include <userenv.h>
#pragma comment(lib, "userenv.lib")

#include <vector>

using namespace std;

namespace {
boost::filesystem::path compute_qt_path()
{
    return L"C:\\data\\qt\\5.12.3\\msvc2017_64\\bin";
}
} // namespace

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    int     wargc;
    LPWSTR* wargv   = CommandLineToArgvW(GetCommandLineW(), &wargc);
    int     argc    = 1;
    char*   argv[1] = {GetCommandLineA()};

    auto                    env     = boost::this_process::environment();
    boost::filesystem::path qt_path = compute_qt_path();
    vector<wstring>         args;
    if (!qt_path.empty()) {
        env["PATH"] += qt_path.string();
    }
    for (int i = 1; i < wargc; ++i) {
        args.emplace_back(wargv[i]);
    }

    boost::filesystem::path app_path = wargv[0];

    app_path = app_path.remove_filename();
    app_path /= "ola_client_store.exe";

    boost::process::spawn(app_path , args);
    return 0;
}