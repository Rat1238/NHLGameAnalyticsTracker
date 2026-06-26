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

static size_t wcb(char* p,size_t s,size_t n,void* u)
{ static_cast<std::string*>(u)->append(p,s*n); return s*n; }

static std::string b64(const std::vector<uint8_t>& d)
{ static const char* T="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o; o.reserve(((d.size()+2)/3)*4);
  for(size_t i=0;i<d.size();i+=3){
      uint32_t v=(uint32_t)d[i]<<16;
      if(i+1<d.size()) v|=(uint32_t)d[i+1]<<8;
        if(i+2<d.size()) v|=(uint32_t)d[i+2];
      o+=T[(v>>18)&63]; o+=T[(v>>12)&63];
      o+=(i+1<d.size())?T[(v>>6)&63]:'='; o+=(i+2<d.size())?T[v&63]:'=';
  } return o; }

static std::vector<uint8_t> readf(const std::string& p)
{ std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("Cannot open: "+p);
  return {std::istreambuf_iterator<char>(f),{}}; }

static std::string mimetype(const std::string& p)
{ auto e=p.substr(p.rfind('.')+1); std::transform(e.begin(),e.end(),e.begin(),::tolower);
  if(e=="jpg"||e=="jpeg") return "image/jpeg";
  if(e=="png") return "image/png";
  throw std::runtime_error("Unsupported: "+e); }

static std::string trim(std::string s)
{ auto l=s.find_first_not_of(" \t\r\n"),r=s.find_last_not_of(" \t\r\n");
  return l==std::string::npos?"":s.substr(l,r-l+1); }

// ── API key resolution (env var first, then api_key.txt) ─────────────────────
static std::string resolve_api_key()
{
    const char* env=std::getenv("GEMINI_API_KEY");
    if(env&&*env) return std::string(env);
    std::ifstream f("api_key.txt");
    if(f){ std::string k; std::getline(f,k); return trim(k); }
    return "";
}
