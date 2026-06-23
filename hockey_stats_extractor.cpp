cat > C:\Users\your name \Desktop\NHL 26 Analytics sim\hockey_stats_extractor.cpp << 'CPPEOF'

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
static size_t wcb(char* p, size_t s, size_t n, void* u) {
  static_cast<std::string*>(u)->append(p, s*n);
    return s*n;
}

// base64 encode
static std::string b64(const std::vector<uint8_t>& d) 
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

// read files
static std::vector<uint8_t> readf(const std::string& path) 
{
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("Failed to open file: " + p);
    return {std::istreambuf_iterator<char>(f), {}};
}

// get pic type from file extension
static std::string mimetype(const std::string& p)
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

//split string by delimiter
static std::vector<std::string> splitpipe(const std::string& s, char d)
{
    std::vector<std::string> v;
    std::istringstream ss(s);
    std::string t;
    while (std::getline(ss, t, d)) {
        v.push_back(trim(t));
    }
    return v;
}

//see previous seasons
static int next_season()
{
    std::ifstream f("season_scores.txt");
    int mx = 0;
    std::string l;
    while (std::getline(f, l)) {
        auto p = l.find('|');
        if (p != std::string::npos) {
            try {
                mx = std::max(mx, std::stoi(trim(l.substr(0, p))));
            } catch (...) {}
    }
    return mx + 1;
}

// code for picture extraction using AI
static std::string gemini(const std::string& key, const std::string& img)
{
    const std::string PROMPT = 
    "You are a data extraction assistant."    
    "Your job is to only look at the Skaters and not the Goalies"
    "Look at the Player's statistics in the screenshot"
    "Return ONLY a pipe-delimited tabble, no markdown, no code fences,\n"
    "First row MUST be exactly:\n"

    "Player|TOI|Goals|Assists|Points|PlusMinus|Shots|ShotPct|PPTime|PenTime|Hits|Blocks|Takeaways|Giveaways\n"
    "every subsequent row = one skater, exactly 14 pipe-separated fields.\n"
    "Missing column -> output 0 for every player in that column.\n"
    "PenTime in MM:SS. No spaces around pipes.\n";"
    
    auto raw=readf(img); auto enc=b64(raw); auto mt=mimetype(img);
    json pay={{"contents",json::array({
        {{"parts",json::array({
            {{"text",PROMPT}},
            {{"inline_data",{{"mime_type",mt},{"data",enc}}}}
        })}}
    })}};
    std::string body=pay.dump();
    std::string url="https://generativelanguage.googleapis.com/v1beta/models/"
                    "gemini-2.0-flash:generateContent?key="+key;
    CURL* c=curl_easy_init(); std::string resp;
    curl_slist* h=curl_slist_append(nullptr,"Content-Type: application/json");
    curl_easy_setopt(c,CURLOPT_URL,url.c_str());
    curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);
    curl_easy_setopt(c,CURLOPT_POSTFIELDS,body.c_str());
    curl_easy_setopt(c,CURLOPT_POSTFIELDSIZE,(long)body.size());
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,wcb);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&resp);
    curl_easy_setopt(c,CURLOPT_TIMEOUT,60L);
    curl_easy_setopt(c,CURLOPT_CAINFO,"C:/msys64/mingw64/ssl/certs/ca-bundle.crt");
    CURLcode rc=curl_easy_perform(c);
    curl_slist_free_all(h); curl_easy_cleanup(c);
    if(rc!=CURLE_OK) throw std::runtime_error(curl_easy_strerror(rc));
    auto j=json::parse(resp);
    if(j.contains("error")) throw std::runtime_error(j["error"]["message"].get<std::string>());
    return j["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
}

//process a single image
static void process(const std::string& key, const std::string& img, int gn)
{
    std::cout<<"  Game "<<gn<<": "<<img<<"\n";
    std::string csv=gemini(key,img);

    // parse rows
    std::vector<std::string> rows;
    std::istringstream ss(csv);
    std::string line;
    while(std::getline(ss,line)){ auto t=trim(line); if(!t.empty()) rows.push_back(t); }
    if(rows.empty()) throw std::runtime_error("Gemini returned no rows");

    auto headers=splitpipe(rows[0]);
    size_t nc=headers.size();

    // Write col_XX_*.txt (current game snapshot)
    std::vector<std::string> cols(nc);
    for(size_t c=0;c<nc;c++) cols[c]=headers[c]+"\n";
    for(size_t r=1;r<rows.size();r++){
        auto f=splitpipe(rows[r]);
        for(size_t c=0;c<nc;c++)
            cols[c]+=(c<f.size()?f[c]:"0")+"\n";
    }
    for(size_t c=0;c<nc;c++){
        std::string n=headers[c];
        std::transform(n.begin(),n.end(),n.begin(),::tolower);
        std::replace(n.begin(),n.end(),' ','_');
        char fn[256]; snprintf(fn,sizeof(fn),"col_%02zu_%s.txt",c,n.c_str());
        std::ofstream out(fn); out<<cols[c];
        std::cout<<"    wrote "<<fn<<"\n";
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

    // Append season_scores.txt
    // Format: game|player|goals|assists|points|plusminus|shots|shotpct|pptime|pentime|hits|blocks|takeaways|giveaways
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
    std::cout<<"  Appended game "<<gn<<" to season_scores.txt\n";
}
// set GEMINI_API_KEY
int main(int argc,char* argv[])
{
    if(argc<2){
        std::cerr<<"Usage: hockey_stats_extractor.exe img1.png [img2.png ...]\n";
        return 1;
    }
    const char* key=std::getenv("API KEY");
    if(!key||!*key){
        std::cerr<<"Error: GEMINI_API_KEY not set.\n"
                   "  CMD: set GEMINI_API_KEY=your_key\n"
                   "  PS:  $env:GEMINI_API_KEY=\"your_key\"\n";
        return 1;
    }
    try{
        for(int i=1;i<argc;i++){
            int gn=next_game();
            process(key,argv[i],gn);
        }
        std::cout<<"\nDone.\n";
    }catch(const std::exception& ex){
        std::cerr<<"Fatal: "<<ex.what()<<"\n"; return 1;
    }
    return 0;
}
CPPEOF
echo "FINI"
