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

// ── tiny helpers ──────────────────────────────────────────────────────────────
static size_t wcb(char* p,size_t s,size_t n,void* u)
{ static_cast<std::string*>(u)->append(p,s*n); return s*n; }

static std::string b64(const std::vector<uint8_t>& d)
{
    static const char* T="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; o.reserve(((d.size()+2)/3)*4);
    for(size_t i=0;i<d.size();i+=3){
        uint32_t v=(uint32_t)d[i]<<16;
        if(i+1<d.size())v|=(uint32_t)d[i+1]<<8;
        if(i+2<d.size())v|=(uint32_t)d[i+2];
        o+=T[(v>>18)&63]; o+=T[(v>>12)&63];
        o+=(i+1<d.size())?T[(v>>6)&63]:'=';
        o+=(i+2<d.size())?T[v&63]:'=';
    }
    return o;
}

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

static std::vector<std::string> splitpipe(const std::string& s)
{ std::vector<std::string> v; std::istringstream ss(s); std::string t;
  while(std::getline(ss,t,'|')) v.push_back(trim(t));
  return v; }

// ── API key resolution (env var first, then api_key.txt) ─────────────────────
static std::string resolve_api_key()
{
    const char* env=std::getenv("GEMINI_API_KEY");
    if(env&&*env) return std::string(env);
    std::ifstream f("api_key.txt");
    if(f){ std::string k; std::getline(f,k); return trim(k); }
    return "";
}

// ── next game number (reads season_scores.txt) ────────────────────────────────
static int next_game()
{ std::ifstream f("season_scores.txt"); int mx=0; std::string l;
  while(std::getline(f,l)){ auto p=l.find('|');
    if(p!=std::string::npos){ try{mx=std::max(mx,std::stoi(trim(l.substr(0,p))));}catch(...){} }}
  return mx+1; }

// ── Gemini call ───────────────────────────────────────────────────────────────
static std::string gemini(const std::string& key, const std::string& img)
{
    const std::string PROMPT=
        "You are a data-extraction assistant. "
        "Look at the hockey SKATER (not goalie) statistics table in this screenshot. "
        "Return ONLY a pipe-delimited table, no markdown, no code fences.\n"
        "First row MUST be exactly:\n"
        "Player|TOI|Goals|Assists|Points|PlusMinus|Shots|ShotPct|PPTime|PenTime|Hits|Blocks|Takeaways|Giveaways\n"
        "Every subsequent row = one skater, exactly 14 pipe-separated fields.\n"
        "Missing column -> output 0 for every player in that column.\n"
        "PenTime in MM:SS. No spaces around pipes.\n";

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


// ── process one image ─────────────────────────────────────────────────────────
// append_col: if true, appends to existing col_*.txt instead of overwriting
//             used when multiple screenshots share the same game number
static void process(const std::string& key, const std::string& img,
                    int gn, bool append_col=false)
{
    std::cout<<"  ["<<img<<"]\n";
    std::string csv=gemini_with_retry(key,img);

    // parse rows
    std::vector<std::string> rows;
    std::istringstream ss(csv);
    std::string line;
    while(std::getline(ss,line)){ auto t=trim(line); if(!t.empty()) rows.push_back(t); }
    if(rows.empty()) throw std::runtime_error("Gemini returned no rows");

    auto headers=splitpipe(rows[0]);
    size_t nc=headers.size();

    // Write or append col_XX_*.txt (current game snapshot)
    for(size_t c=0;c<nc;c++){
        std::string n=headers[c];
        std::transform(n.begin(),n.end(),n.begin(),::tolower);
        std::replace(n.begin(),n.end(),' ','_');
        char fn[256]; snprintf(fn,sizeof(fn),"col_%02zu_%s.txt",c,n.c_str());

        // First image: write header + data. Subsequent images: append data only.
        std::ofstream out(fn, append_col ? std::ios::app : std::ios::out);
        if(!append_col) out<<headers[c]<<"\n"; // write header only on first image
        for(size_t r=1;r<rows.size();r++){
            auto f=splitpipe(rows[r]);
            out<<(c<f.size()?f[c]:"0")<<"\n";
        }
        std::cout<<"    "<<(append_col?"appended":"wrote")<<" "<<fn<<"\n";
    }

    // Find column indices
    auto ci=[&](const std::string& nm)->int{
        for(int i=0;i<(int)nc;i++){
            std::string h=headers[i];
            std::transform(h.begin(),h.end(),h.begin(),::tolower);
            if(h==nm) return i;
        } return -1;
    };
    int cip=ci("player"),cig=ci("goals"),cia=ci("assists"),cip2=ci("points"),
        cipm=ci("plusminus"),cis=ci("shots"),cisp=ci("shotpct"),
        cippt=ci("pptime"),cipent=ci("pentime"),cih=ci("hits"),
        cib=ci("blocks"),citk=ci("takeaways"),cigv=ci("giveaways");

    // Append season_scores.txt — all rows use same game number
    std::ofstream sf("season_scores.txt",std::ios::app);
    for(size_t r=1;r<rows.size();r++){
        auto f=splitpipe(rows[r]);
        auto g=[&](int idx)->std::string{
            return (idx>=0&&(size_t)idx<f.size())?f[idx]:"0";
        };
        sf<<gn<<"|"<<g(cip)<<"|"<<g(cig)<<"|"<<g(cia)<<"|"<<g(cip2)<<"|"
          <<g(cipm)<<"|"<<g(cis)<<"|"<<g(cisp)<<"|"<<g(cippt)<<"|"
          <<g(cipent)<<"|"<<g(cih)<<"|"<<g(cib)<<"|"<<g(citk)<<"|"<<g(cigv)<<"\n";
    }
    std::cout<<"  Appended "<<(rows.size()-1)<<" players as Game "<<gn
             <<" to season_scores.txt\n";
}

int main(int argc,char* argv[])
{
    if(argc<2){
        std::cerr<<"Usage: hockey_stats_extractor.exe img1.png [img2.png ...]\n";
        return 1;
    }
    std::string key=resolve_api_key();
    if(key.empty()){
        std::cerr<<"Error: GEMINI_API_KEY not set.\n"
                   "  Option 1 - CMD:        set GEMINI_API_KEY=your_key\n"
                   "  Option 2 - MSYS2:      export GEMINI_API_KEY=\"your_key\"\n"
                   "  Option 3 - File:       paste your key into api_key.txt next to the exe\n";
        return 1;
    }
    try{
        // All images passed together share the SAME game number.
        // This lets you upload multiple screenshots (e.g. two halves of the
        // player bench) and have them all count as one game.
        int gn = next_game();
        std::cout<<"Processing "<<(argc-1)<<" screenshot(s) as Game "<<gn<<"...\n";
        for(int i=1;i<argc;i++){
            std::cout<<"  ["<<i<<"/"<<(argc-1)<<"] "<<argv[i]<<"\n";
            // First image writes col files fresh, subsequent images append
            bool append = (i > 1);
            process(key, argv[i], gn, append);
            // Small delay between requests to avoid rate limiting
            if(i < argc-1){
                std::cout<<"  Waiting 5s before next request...\n";
                Sleep(5000);
            }
        }
        std::cout<<"\nDone. All "<<(argc-1)<<" screenshot(s) saved as Game "<<gn<<".\n";
    }catch(const std::exception& ex){
        std::cerr<<"Fatal: "<<ex.what()<<"\n"; return 1;
    }
    return 0;
}
