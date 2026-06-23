#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
// helpers
static size_t WriteCallback(char* ptr, size_t size, size_t nmeb, void* userdata) {
    auto* buf =static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmeb);
    return size * nmeb;
}

static std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() +2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
       uint32_t v = (uint32_t)data[i] << 16;
       if (i + 1 < data.size()) v |= (uint32_t)data[i+1] << 8;
       if (i + 2 < data.size()) v |= (uint32_t)data[i + 2]; 
       out += B64[(v >> 18) & 63];
       out += B64[(v >> 12) & 63];
       out += (i + 1 < data.size()) ? B64[(v >> 6) & 63] : '=';
       out += (i + 2 < data.size()) ? B64[v & 63] : '=';
    }
    return out;
}
