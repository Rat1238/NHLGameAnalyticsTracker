cat > /mnt/c/Users/Administrator/Desktop/hockey_stats_extractor.cpp << 'CPPEOF'

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
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <filesystem>
namespace fs = std ::filesystem;


// helpers
static size_t write_cb(char* p, size_t s, size_t n, void* u) {
    auto* b = static_cast<std::string*>(u); 
    b->append(p, s*n);
    return s*n;
}

// base64 encode
static std::string base64_encode(const std::vector<uint8_t>& d) 
   {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve(((d.size() + 2) / 3) * 4);
    for (size_t i = 0; i < d.size(); i += 3) 
    {
        uint32_t v = (uint32_t)d[i] << 16;
        if (i + 1 < d.size()) {
            v |= (uint32_t)d[i + 1] << 8;
        }
        if (i + 2 < d.size()) {
            v |= (uint32_t)d[i + 2];
        }
        o+= T[(v >> 18) & 63];
        o+= T[(v >> 12) & 63];
        o+= (i + 1 < d.size() ? T[(v >> 6) & 63] : '=');
        o+= (i + 2 < d.size() ? T[v & 63] : '=');
    }
    return o;
}

// read bytes from file
static std::vector<uint8_t> read_bytes(const std::string& path) 
{
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("Failed to open file: " + p);
    return {std::istreambuf_iterator<char>(f), {}};
}

// get pic type from file extension
static std::string mime(const std::string& p)
{
    auto e= p.substr(p.rfind('.') + 1);
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    if (e == "jpg" || e == "jpeg") {
        return "image/jpeg";
    }
    if (e == "png") {
        return "image/png";
    }
    if (e == "webp") {
        return "image/webp";
    }
    throw std::runtime_error("Unsupported image type: " + e);
}

// trim whitespace 
static std::string trim(std::string s)
{
    auto l = s.find_first_not_of(" \t\r\n");
    auto r = s.find_last_not_of(" \t\r\n");
    return (l == std::string::npos) ? "" : s.substr(l, r - l + 1);
}

//split string 
static std::vector<std::string> split(const std::string& s, char d)
{
    std::vector<std::string> v;
    std::istringstream ss(s);
    std::string t;
    while (std::getline(ss, t, d)) {
        v.push_back(trim(t));
    }
    return v;
}

// code for picture extraction using AI
