#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>

// ── exe directory helper ──────────────────────────────────────────────────────
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
static std::string exe_dir_impl()
{
    char buf[MAX_PATH]={};
    GetModuleFileNameA(nullptr,buf,MAX_PATH);
    std::string p(buf);
    auto s=p.find_last_of("\\/");
    return s!=std::string::npos?p.substr(0,s+1):".\\";
}
