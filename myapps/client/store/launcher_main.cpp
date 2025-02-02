// myapps/client/store/launcher_main.cpp

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
// #define _WINSOCKAPI_
#ifndef NOMINMAX
#define NOMINMAX
#endif #undef UNICODE
#define UNICODE
#undef _WINSOCKAPI_
// #define _WINSOCKAPI_
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on
#include "boost/filesystem.hpp"
#include "boost/process.hpp"
#include "boost/program_options.hpp"

#include <wtsapi32.h>
#pragma comment(lib, "wtsapi32.lib")
#include <userenv.h>
#pragma comment(lib, "userenv.lib")

#include <codecvt>
#include <fstream>
#include <iostream>
#include <locale>
#include <string>
#include <vector>

using namespace std;

namespace {
boost::filesystem::path compute_qt_path()
{
    const char* qt_env = getenv("MYAPPS_QT_PATH");

    if (qt_env != nullptr && *qt_env) {
        boost::filesystem::path qt_path(qt_env);
        if (boost::filesystem::exists(qt_path)) {
            return qt_path;
        }
    }
    return boost::filesystem::path{};
}

bool matches(const string& _path_str, const string& _pattern)
{
    const auto off = _path_str.rfind(_pattern);
    if (off != string::npos) {
        if (_path_str.back() == '\\') {
            return off == (_path_str.size() - 1 - _pattern.size());
        } else {
            return off == (_path_str.size() - _pattern.size());
        }
    }
    return false;
}

inline std::wstring widen(const std::string& str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
    return convert.from_bytes(str);
}

} // namespace

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    int     wargc;
    LPWSTR* wargv   = CommandLineToArgvW(GetCommandLineW(), &wargc);
    int     argc    = 1;
    char*   argv[1] = {GetCommandLineA()};

    auto env = boost::this_process::native_environment();
    // add a variable to the current environment
    boost::filesystem::path qt_path = compute_qt_path();
    vector<wstring>         args;
    auto                    env_path = env["Path"];
    auto                    path_vec = env_path.to_vector();
    string                  bin_path;

    {
        string new_paths;
        // we need to put the new qt_path before the myapps/bin path
        for (const auto& p : path_vec) {
            if (matches(p, "MyApps.dir\\bin")) {
                if (!new_paths.empty()) {
                    new_paths += ';';
                }

                if (!qt_path.empty()) {
                    new_paths += qt_path.string();
                    new_paths += ';';
                }

                new_paths += p;
                bin_path = p;
            } else if (!p.empty()) {
                if (!new_paths.empty()) {
                    new_paths += ';';
                }
                new_paths += p;
            }
        }

        if (bin_path.empty()) {
            // myapps path not found,
            env_path.append(qt_path.string());
        } else if (!qt_path.empty()) {
            env_path.assign(new_paths);
        }
    }
    {
        string qt_plugin_path = qt_path.string();
        if (!qt_plugin_path.empty()) {
            // if (qt_plugin_path.back() != '\\') {
            //     qt_plugin_path += '\\';
            // }
            // qt_plugin_path += "plugins";
            env["QT_PLUGIN_PATH"].assign(qt_plugin_path);
        } else if (!bin_path.empty()) {
            qt_plugin_path = bin_path;
            // if (qt_plugin_path.back() != '\\') {
            //     qt_plugin_path += '\\';
            // }
            // qt_plugin_path += "plugins";
            env["QT_PLUGIN_PATH"].assign(qt_plugin_path);
        }
    }

    if (!bin_path.empty()) {
        args.emplace_back(L"--secure-prefix");
        if (bin_path.back() == '\\') {
            bin_path.pop_back();
        }
        args.emplace_back(widen(bin_path + "\\certs"));
    }

    for (int i = 1; i < wargc; ++i) {
        args.emplace_back(wargv[i]);
    }

    boost::filesystem::path app_path = wargv[0];

    app_path = app_path.remove_filename();
    app_path /= "myapps_store.exe";

    boost::process::spawn(app_path, args);
    return 0;
}