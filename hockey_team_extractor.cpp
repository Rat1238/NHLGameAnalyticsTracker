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

static int next_team_game()
{ std::ifstream f("team_history.txt"); int mx=0; std::string l;
  while(std::getline(f,l)){ auto p=l.find('|');
    if(p!=std::string::npos){ try{mx=std::max(mx,std::stoi(trim(l.substr(0,p))));}catch(...){} }}
  return mx+1; }

static std::string gemini(const std::string& key,const std::string& img)
{
    const std::string PROMPT=
        "You are a data-extraction assistant. "
        "Look at this hockey team-stats scoreboard. "
        "Return ONLY a pipe-delimited table with exactly 3 columns, no markdown:\n"
        "  LEFT_VALUE|STAT_LABEL|RIGHT_VALUE\n"
        "First row: LEFT_TEAM|SCORE|RIGHT_TEAM\n"
        "  LEFT_TEAM/RIGHT_TEAM = 3-letter abbreviations (e.g. MTL, TOR)\n"
        "  SCORE = L-R format (e.g. 3-5)\n"
        "Each subsequent row = one stat. No spaces around pipes.\n";

    auto raw=readf(img); auto enc=b64(raw); auto mt=mimetype(img);
    json pay={{"contents",json::array({
        {{"parts",json::array({
            {{"text",PROMPT}},
            {{"inline_data",{{"mime_type",mt},{"data",enc}}}}
        })}}
    })}};
    std::string body=pay.dump();
    std::string url="https://generativelanguage.googleapis.com/v1beta/models/"
                    "gemini-2.5-flash:generateContent?key="+key;
    CURL* c=curl_easy_init(); std::string resp;
    curl_slist* h=curl_slist_append(nullptr,"Content-Type: application/json");
    curl_easy_setopt(c,CURLOPT_URL,url.c_str());
    curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);
    curl_easy_setopt(c,CURLOPT_POSTFIELDS,body.c_str());
    curl_easy_setopt(c,CURLOPT_POSTFIELDSIZE,(long)body.size());
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,wcb);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&resp);
    curl_easy_setopt(c,CURLOPT_TIMEOUT,60L);
    curl_easy_setopt(c,CURLOPT_CAINFO,(exe_dir_impl()+"ca-bundle.crt").c_str());
    CURLcode rc=curl_easy_perform(c);
    curl_slist_free_all(h); curl_easy_cleanup(c);
    if(rc!=CURLE_OK) throw std::runtime_error(curl_easy_strerror(rc));
    auto j=json::parse(resp);
    if(j.contains("error")) throw std::runtime_error(j["error"]["message"].get<std::string>());
    return j["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
}

// ── Gemini call with retry on 429 ────────────────────────────────────────────
static std::string gemini_with_retry(const std::string& key, const std::string& img)
{
    int max_retries = 3;
    int wait_ms = 65000;
    for(int attempt = 0; attempt < max_retries; attempt++){
        try{
            return gemini(key, img);
        } catch(const std::exception& ex){
            std::string msg = ex.what();
            if(msg.find("429") != std::string::npos ||
               msg.find("RESOURCE_EXHAUSTED") != std::string::npos ||
               msg.find("TooManyRequests") != std::string::npos){
                if(attempt < max_retries - 1){
                    int wait_s = wait_ms / 1000;
                    std::cout << "  Rate limit hit (429). Waiting " << wait_s
                              << "s before retry " << (attempt+2)
                              << "/" << max_retries << "...\n";
                    std::cout.flush();
                    Sleep(wait_ms);
                    wait_ms += 30000;
                } else {
                    throw std::runtime_error(
                        "Rate limit (429): retried " +
                        std::to_string(max_retries) +
                        " times. Wait a few minutes and try again.");
                }
            } else {
                throw;
            }
        }
    }
    throw std::runtime_error("Max retries exceeded");
}


int main(int argc,char* argv[])
{
    if(argc<2){ std::cerr<<"Usage: hockey_team_extractor.exe scoreboard.png\n"; return 1; }
    std::string key=resolve_api_key();
    if(key.empty()){
        std::cerr<<"Error: GEMINI_API_KEY not set.\n"
                   "  Option 1 - CMD:   set GEMINI_API_KEY=your_key\n"
                   "  Option 2 - MSYS2: export GEMINI_API_KEY=\"your_key\"\n"
                   "  Option 3 - File:  paste your key into api_key.txt next to the exe\n";
        return 1;
    }
    try{
        int gn=next_team_game();
        std::cout<<"Processing as team game "<<gn<<"...\n";
        std::string csv=gemini_with_retry(key,argv[1]);

        // parse
        std::vector<std::string> rows;
        std::istringstream ss(csv); std::string line;
        while(std::getline(ss,line)){ auto t=trim(line); if(!t.empty()) rows.push_back(t); }
        if(rows.empty()) throw std::runtime_error("No data from Gemini");

        // first row = meta
        std::vector<std::string> meta;
        std::istringstream ms(rows[0]); std::string tok;
        while(std::getline(ms,tok,'|')) meta.push_back(trim(tok));
        if(meta.size()<3) throw std::runtime_error("Bad header row");

        std::string la=meta[0], score=meta[1], ra=meta[2];
        std::string ls,rs;
        auto dash=score.find('-');
        if(dash!=std::string::npos){ ls=trim(score.substr(0,dash)); rs=trim(score.substr(dash+1)); }

        // write current game files
        { std::ofstream f("team_meta.txt"); f<<la<<"\n"<<ra<<"\n"<<ls<<"\n"<<rs<<"\n"; }
        std::ofstream lf("team_labels.txt");
        std::ofstream lvf("team_"+la+".txt");
        std::ofstream rvf("team_"+ra+".txt");
        for(size_t i=1;i<rows.size();i++){
            std::vector<std::string> f;
            std::istringstream rs2(rows[i]); std::string t2;
            while(std::getline(rs2,t2,'|')) f.push_back(trim(t2));
            lvf<<(f.size()>0?f[0]:"0")<<"\n";
            lf <<(f.size()>1?f[1]:"")<<"\n";
            rvf<<(f.size()>2?f[2]:"0")<<"\n";
        }
        std::cout<<"  wrote team_meta.txt team_labels.txt team_"<<la<<".txt team_"<<ra<<".txt\n";

        // append team_history.txt
        // format: game|la|ls|ra|rs|LABEL=lval:rval|...
        std::ofstream hf("team_history.txt",std::ios::app);
        hf<<gn<<"|"<<la<<"|"<<ls<<"|"<<ra<<"|"<<rs;
        for(size_t i=1;i<rows.size();i++){
            std::vector<std::string> f;
            std::istringstream rs3(rows[i]); std::string t3;
            while(std::getline(rs3,t3,'|')) f.push_back(trim(t3));
            std::string lb=f.size()>1?f[1]:"";
            std::string lv=f.size()>0?f[0]:"0";
            std::string rv=f.size()>2?f[2]:"0";
            std::replace(lb.begin(),lb.end(),'|','/');
            std::replace(lb.begin(),lb.end(),'=','-');
            hf<<"|"<<lb<<"="<<lv<<":"<<rv;
        }
        hf<<"\n";
        std::cout<<"  Appended to team_history.txt (game "<<gn<<")\n";
        std::cout<<"Done.\n";
    }catch(const std::exception& ex){
        std::cerr<<"Fatal: "<<ex.what()<<"\n"; return 1;
    }
    return 0;
}
