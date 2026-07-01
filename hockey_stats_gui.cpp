#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shobjidl.h>
#include <combaseapi.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>
namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════════════════════
//  COLOURS / PALETTE
// ═══════════════════════════════════════════════════════════════════════════════
static const ImVec4 kGold  = {0.85f,0.65f,0.10f,1.f};
static const ImVec4 kDim   = {0.45f,0.45f,0.45f,1.f};
static const ImVec4 kGreen = {0.24f,0.74f,0.32f,1.f};
static const ImVec4 kRed   = {0.87f,0.20f,0.18f,1.f};
static const ImVec4 kBlue  = {0.22f,0.48f,0.87f,1.f};
static const ImVec4 kWhite = {0.90f,0.90f,0.90f,1.f};
static const ImU32 PAL[]={
    IM_COL32(85,165,255,255), IM_COL32(255,200,60,255),
    IM_COL32(60,220,120,255), IM_COL32(255,90,90,255),
    IM_COL32(200,100,255,255),IM_COL32(255,160,40,255)};

// ═══════════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════════
static std::string trim(std::string s)
{ auto l=s.find_first_not_of(" \t\r\n"),r=s.find_last_not_of(" \t\r\n");
  return l==std::string::npos?"":s.substr(l,r-l+1); }

static std::vector<std::string> read_lines(const std::string& path)
{ std::vector<std::string> v; std::ifstream f(path); if(!f) return v;
  std::string l; while(std::getline(f,l)){if(!l.empty()&&l.back()=='\r')l.pop_back();v.push_back(l);}
  return v; }

static std::vector<std::string> pipe_split(const std::string& s)
{ std::vector<std::string> v; std::istringstream ss(s); std::string t;
  while(std::getline(ss,t,'|')) v.push_back(trim(t));
  return v; }

static float to_num(const std::string& s)
{ std::string c; bool hd=false,dot=false,neg=false;
  for(char ch:s){
      if(ch=='-'&&!hd){neg=true;continue;}
      if(ch=='+'||ch=='%') continue;
      if(ch==':'){c+='.';continue;}
      if(std::isdigit(ch)){hd=true;c+=ch;}
      else if(ch=='.'&&!dot){dot=true;c+=ch;}
  }
  if(!hd) return 0.f;
  return (neg?-1.f:1.f)*std::stof(c); }

// ═══════════════════════════════════════════════════════════════════════════════
//  WINDOWS FILE PICKER
// ═══════════════════════════════════════════════════════════════════════════════
static std::vector<std::string> pick_files_win(const wchar_t* title, bool multi)
{
    std::vector<std::string> result;
    IFileOpenDialog* dlg=nullptr;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_ALL,
                               IID_IFileOpenDialog,(void**)&dlg))) return result;
    COMDLG_FILTERSPEC ft[]={{L"Images",L"*.png;*.jpg;*.jpeg"}};
    dlg->SetFileTypes(1,ft);
    dlg->SetTitle(title);
    if(multi){ DWORD o; dlg->GetOptions(&o); dlg->SetOptions(o|FOS_ALLOWMULTISELECT); }
    if(SUCCEEDED(dlg->Show(nullptr))){
        if(multi){
            IShellItemArray* items=nullptr;
            if(SUCCEEDED(dlg->GetResults(&items))){
                DWORD cnt; items->GetCount(&cnt);
                for(DWORD i=0;i<cnt;i++){
                    IShellItem* si=nullptr;
                    if(SUCCEEDED(items->GetItemAt(i,&si))){
                        wchar_t* pw=nullptr;
                        if(SUCCEEDED(si->GetDisplayName(SIGDN_FILESYSPATH,&pw))){
                            char buf[MAX_PATH];
                            WideCharToMultiByte(CP_UTF8,0,pw,-1,buf,MAX_PATH,nullptr,nullptr);
                            result.push_back(buf);
                            CoTaskMemFree(pw);
                        }
                        si->Release();
                    }
                }
                items->Release();
            }
        } else {
            IShellItem* si=nullptr;
            if(SUCCEEDED(dlg->GetResult(&si))){
                wchar_t* pw=nullptr;
                if(SUCCEEDED(si->GetDisplayName(SIGDN_FILESYSPATH,&pw))){
                    char buf[MAX_PATH];
                    WideCharToMultiByte(CP_UTF8,0,pw,-1,buf,MAX_PATH,nullptr,nullptr);
                    result.push_back(buf);
                    CoTaskMemFree(pw);
                }
                si->Release();
            }
        }
    }
    dlg->Release();
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  API KEY RESOLUTION
//  Priority: 1) GEMINI_API_KEY env var
//            2) api_key.txt in same folder as the exe
//            3) api_key.txt in current working directory
// ═══════════════════════════════════════════════════════════════════════════════

// Returns the folder the running exe lives in, with trailing backslash.
static std::string exe_dir()
{
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
    auto slash = path.find_last_of("\\/");
    return (slash != std::string::npos) ? path.substr(0, slash+1) : ".\\";
}

static std::string try_read_key_file(const std::string& path)
{
    std::ifstream f(path);
    if(!f) return "";
    std::string k;
    std::getline(f, k);
    // strip BOM if Notepad added one
    if(k.size()>=3 &&
       (unsigned char)k[0]==0xEF &&
       (unsigned char)k[1]==0xBB &&
       (unsigned char)k[2]==0xBF)
        k = k.substr(3);
    // trim all whitespace
    auto l=k.find_first_not_of(" \t\r\n");
    auto r=k.find_last_not_of(" \t\r\n");
    return (l!=std::string::npos) ? k.substr(l, r-l+1) : "";
}

static std::string resolve_api_key()
{
    // 1. Environment variable
    const char* env = std::getenv("GEMINI_API_KEY");
    if(env && *env) return std::string(env);

    // 2. api_key.txt next to the exe
    std::string key = try_read_key_file(exe_dir() + "api_key.txt");
    if(!key.empty()) return key;

    // 3. api_key.txt in current working directory (fallback)
    key = try_read_key_file("api_key.txt");
    return key;
}

// Debug helper — call from the UI to show what the app sees
static std::string debug_api_key_info()
{
    std::string out;
    char cwd[MAX_PATH]={};
    GetCurrentDirectoryA(MAX_PATH, cwd);

    out += "Exe folder:  " + exe_dir() + "\n";
    out += "Working dir: " + std::string(cwd) + "\n\n";

    // Check env var
    const char* env = std::getenv("GEMINI_API_KEY");
    if(env && *env)
        out += "[OK] Env var GEMINI_API_KEY found (" + std::to_string(strlen(env)) + " chars)\n";
    else
        out += "[--] Env var GEMINI_API_KEY not set\n";

    // Check file in exe dir
    std::string fp1 = exe_dir() + "api_key.txt";
    std::string k1  = try_read_key_file(fp1);
    if(!k1.empty())
        out += "[OK] " + fp1 + " found (" + std::to_string(k1.size()) + " chars)\n";
    else
        out += "[--] " + fp1 + " not found or empty\n";

    // Check file in cwd
    std::string fp2 = std::string(cwd) + "\\api_key.txt";
    std::string k2  = try_read_key_file(fp2);
    if(!k2.empty())
        out += "[OK] " + fp2 + " found (" + std::to_string(k2.size()) + " chars)\n";
    else
        out += "[--] " + fp2 + " not found or empty\n";

    out += "\nResolved key: ";
    std::string final_key = resolve_api_key();
    if(final_key.empty())
        out += "(empty — this is why you see the error)\n";
    else
        out += final_key.substr(0,8) + "... (" + std::to_string(final_key.size()) + " chars total)\n";

    return out;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  EXTRACTOR RUNNER
// ═══════════════════════════════════════════════════════════════════════════════
static bool run_extractor(const std::string& exe,
                           const std::vector<std::string>& imgs,
                           std::string& err)
{
    std::string key = resolve_api_key();
    if(key.empty()){
        err="GEMINI_API_KEY not set. Paste your key into api_key.txt next to the exe.";
        return false;
    }

    std::string dir     = exe_dir();
    std::string exepath = dir + exe;
    std::string logpath = dir + "extractor_log.txt";

    // Check the extractor exe actually exists before trying to launch it
    if(GetFileAttributesA(exepath.c_str())==INVALID_FILE_ATTRIBUTES){
        err = "Cannot find: " + exepath;
        return false;
    }

    // Build command line:  hockey_stats_extractor.exe "img1.png" "img2.png"
    std::string cmdline = "\"" + exepath + "\"";
    for(auto& p : imgs) cmdline += " \"" + p + "\"";

    // Build environment block with GEMINI_API_KEY injected
    // Environment block is double-null-terminated KEY=VALUE\0KEY=VALUE\0\0
    std::string env_block;
    env_block += "GEMINI_API_KEY=" + key + '\0';
    // Copy existing environment variables
    LPCH existing = GetEnvironmentStringsA();
    if(existing){
        LPCH p = existing;
        while(*p){
            std::string var(p);
            // Skip any existing GEMINI_API_KEY so we don't duplicate
            if(var.rfind("GEMINI_API_KEY=",0) != 0)
                env_block += var + '\0';
            p += var.size() + 1;
        }
        FreeEnvironmentStringsA(existing);
    }
    env_block += '\0'; // final double null terminator

    // Open log file for stdout/stderr redirection
    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle       = TRUE;

    HANDLE hLog = CreateFileA(
        logpath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if(hLog == INVALID_HANDLE_VALUE){
        err = "Could not create log file: " + logpath;
        return false;
    }

    STARTUPINFOA si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput  = hLog;
    si.hStdError   = hLog;

    PROCESS_INFORMATION pi = {};

    // Make cmdline a mutable buffer (CreateProcessA needs non-const)
    std::vector<char> cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back('\0');

    BOOL ok = CreateProcessA(
        exepath.c_str(),       // exe path
        cmd_buf.data(),        // command line
        nullptr,               // process security
        nullptr,               // thread security
        TRUE,                  // inherit handles (so log file works)
        CREATE_NO_WINDOW,      // don't flash a console window
        (LPVOID)env_block.data(), // our custom environment
        dir.c_str(),           // working directory = exe folder
        &si,
        &pi);

    CloseHandle(hLog);

    if(!ok){
        DWORD e = GetLastError();
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "CreateProcess failed for %s (error %lu)", exepath.c_str(), e);
        err = msg;
        return false;
    }

    // Wait for extractor to finish (timeout 60 seconds)
    DWORD wait = WaitForSingleObject(pi.hProcess, 60000);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if(wait == WAIT_TIMEOUT){
        err = "Extractor timed out after 60 seconds.";
        return false;
    }

    if(exit_code != 0){
        // Read full log so user sees the real error
        auto lines = read_lines(logpath);
        std::string full;
        for(auto& l : lines) full += l + " | ";
        err = full.empty() ? "Extractor failed (exit code " + std::to_string(exit_code) + ")"
                           : full.substr(0, 300);
        return false;
    }

    // Success — make sure GUI reads data from exe dir
    SetCurrentDirectoryA(dir.c_str());
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  UPLOAD JOB
// ═══════════════════════════════════════════════════════════════════════════════
enum class UpState { Idle, Running, Done, Error };
struct UploadJob {
    UpState     state = UpState::Idle;
    std::string status_msg;
    std::string exe;
    std::vector<std::string> images;

    // Call from UI thread — opens picker then immediately runs extractor
    void trigger(const std::string& binary, bool multi, const wchar_t* title)
    {
        exe = binary;
        images = pick_files_win(title, multi);
        if(images.empty()||images[0].empty()){ state=UpState::Idle; return; }
        state = UpState::Running;
        status_msg = "Analysing with Gemini...";
    }
};

static void tick_upload(UploadJob& job, std::function<void()> on_ok)
{
    if(job.state != UpState::Running) return;
    std::string err;
    if(run_extractor(job.exe, job.images, err)){
        job.state = UpState::Done;
        job.status_msg = "Loaded!";
        on_ok();
    } else {
        job.state = UpState::Error;
        job.status_msg = "Error: " + err.substr(0, 120);
    }
}

static void upload_button(const char* label, UploadJob& job,
                           const std::string& exe, bool multi,
                           const wchar_t* title)
{
    bool busy = (job.state == UpState::Running);
    if(busy) ImGui::BeginDisabled();
    if(ImGui::Button(label)) job.trigger(exe, multi, title);
    if(busy) ImGui::EndDisabled();
    if(!job.status_msg.empty()){
        ImGui::SameLine(0,10);
        ImGui::TextColored(job.state==UpState::Error?kRed:kGreen,
                           "%s", job.status_msg.c_str());
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  DATA MODELS
// ═══════════════════════════════════════════════════════════════════════════════

// ── player column files ───────────────────────────────────────────────────────
struct Column { std::string header; std::vector<std::string> values; };
struct PlayerTable {
    std::vector<std::string> players;
    std::vector<Column>      columns;
    bool empty() const { return columns.empty(); }
};

static PlayerTable load_player_table(const std::string& prefix="col_")
{
    std::vector<fs::path> paths;
    for(auto& e : fs::directory_iterator(".")){
        auto fn = e.path().filename().string();
        if(fn.rfind(prefix,0)==0 && fn.find("goalie")==std::string::npos
           && e.path().extension()==".txt")
            paths.push_back(e.path());
    }
    std::sort(paths.begin(),paths.end());
    PlayerTable tbl;
    for(auto& p : paths){
        std::ifstream f(p); if(!f) continue;
        Column col; std::string line; bool first=true;
        while(std::getline(f,line)){
            if(!line.empty()&&line.back()=='\r') line.pop_back();
            if(first){ col.header=line; first=false; }
            else col.values.push_back(line);
        }
        tbl.columns.push_back(col);
    }
    if(!tbl.columns.empty()) tbl.players = tbl.columns[0].values;
    return tbl;
}

static PlayerTable load_goalie_table()
{
    std::vector<fs::path> paths;
    for(auto& e : fs::directory_iterator(".")){
        auto fn = e.path().filename().string();
        if(fn.rfind("col_goalie_",0)==0 && e.path().extension()==".txt")
            paths.push_back(e.path());
    }
    std::sort(paths.begin(),paths.end());
    PlayerTable tbl;
    for(auto& p : paths){
        std::ifstream f(p); if(!f) continue;
        Column col; std::string line; bool first=true;
        while(std::getline(f,line)){
            if(!line.empty()&&line.back()=='\r') line.pop_back();
            if(first){ col.header=line; first=false; }
            else col.values.push_back(line);
        }
        tbl.columns.push_back(col);
    }
    if(!tbl.columns.empty()) tbl.players = tbl.columns[0].values;
    return tbl;
}




// ── season entries ────────────────────────────────────────────────────────────
struct SeasonEntry {
    int game; std::string player;
    float goals,assists,points,pm,shots,spct,pptime,pentime,hits,ppg,shg;
};

static std::vector<SeasonEntry> load_season(const std::string& file="season_scores.txt")
{
    std::vector<SeasonEntry> v;
    for(auto& l : read_lines(file)){
        auto f=pipe_split(l); if(f.size()<13) continue;
        SeasonEntry e;
        e.game=std::stoi(f[0]); e.player=f[1];
        e.goals=to_num(f[2]);  e.assists=to_num(f[3]); e.points=to_num(f[4]);
        e.pm=to_num(f[5]);     e.shots=to_num(f[6]);   e.spct=to_num(f[7]);
        e.pptime=to_num(f[8]); e.pentime=to_num(f[9]);
        e.hits=to_num(f[10]);  e.ppg=to_num(f[11]);    e.shg=to_num(f[12]);
        v.push_back(e);
    }
    return v;
}

// Off = (G*0.75) + (SH*0.09) + (PPG*0.1) - (-PlusMinus*0.1)
static float off_score(const SeasonEntry& e)
{ return e.goals*.75f + e.shots*.09f + e.ppg*.10f - (-e.pm*.10f); }

// Def = (PlusMinus*0.15) + (Hits*0.1) - (PIM*0.09)
static float def_score(const SeasonEntry& e)
{ return e.pm*.15f + e.hits*.10f - e.pentime*.09f; }

struct PlayerTotals {
    std::string name; int gp=0;
    float goals=0,assists=0,points=0,shots=0,hits=0,ppg=0,shg=0,pentime=0,pm=0;
    float off_sum=0, def_sum=0;
    float avg_off() const { return gp?off_sum/gp:0.f; }
    float avg_def() const { return gp?def_sum/gp:0.f; }
};

static std::map<std::string,PlayerTotals> build_totals(const std::vector<SeasonEntry>& s)
{
    std::map<std::string,PlayerTotals> m;
    for(auto& e : s){
        auto& t=m[e.player]; t.name=e.player; t.gp++;
        t.goals+=e.goals; t.assists+=e.assists; t.points+=e.points;
        t.shots+=e.shots; t.hits+=e.hits;
        t.ppg+=e.ppg; t.shg+=e.shg;
        t.pentime+=e.pentime; t.pm+=e.pm;
        t.off_sum+=off_score(e); t.def_sum+=def_score(e);
    }
    return m;
}

// ── goalie entries ────────────────────────────────────────────────────────────
struct GoalieEntry { int game; std::string player; float sa,svs,ga,svp,gaa,pentime; };

static std::vector<GoalieEntry> load_goalies(const std::string& file="goalie_scores.txt")
{
    std::vector<GoalieEntry> v;
    for(auto& l : read_lines(file)){
        auto f=pipe_split(l); if(f.size()<8) continue;
        GoalieEntry e;
        e.game=std::stoi(f[0]); e.player=f[1];
        e.sa=to_num(f[2]); e.svs=to_num(f[3]); e.ga=to_num(f[4]);
        e.svp=to_num(f[5]); e.gaa=to_num(f[6]); e.pentime=to_num(f[7]);
        v.push_back(e);
    }
    return v;
}

// ── team stats ────────────────────────────────────────────────────────────────
struct TeamStats {
    std::string la,ra,ls,rs;
    std::vector<std::string> labels,lv,rv;
    bool loaded=false;
};

static TeamStats load_team_stats()
{
    TeamStats ts;
    auto meta=read_lines("team_meta.txt"); if(meta.size()<4) return ts;
    ts.la=meta[0]; ts.ra=meta[1]; ts.ls=meta[2]; ts.rs=meta[3];
    ts.labels=read_lines("team_labels.txt");
    ts.lv=read_lines("team_"+ts.la+".txt");
    ts.rv=read_lines("team_"+ts.ra+".txt");
    ts.loaded=true; return ts;
}

struct TeamGame {
    int game; std::string la,ls,ra,rs;
    std::vector<std::string> labels,lvals,rvals;
};

static std::vector<TeamGame> load_team_history(const std::string& file="team_history.txt")
{
    std::vector<TeamGame> v;
    for(auto& line : read_lines(file)){
        auto f=pipe_split(line); if(f.size()<5) continue;
        TeamGame g;
        g.game=std::stoi(f[0]); g.la=f[1]; g.ls=f[2]; g.ra=f[3]; g.rs=f[4];
        for(size_t i=5;i<f.size();i++){
            auto eq=f[i].find('='); if(eq==std::string::npos) continue;
            g.labels.push_back(f[i].substr(0,eq));
            auto col=f[i].find(':',eq);
            if(col!=std::string::npos){
                g.lvals.push_back(f[i].substr(eq+1,col-eq-1));
                g.rvals.push_back(f[i].substr(col+1));
            }
        }
        v.push_back(g);
    }
    return v;
}

// ── season archive ────────────────────────────────────────────────────────────
struct ArchiveSeason {
    int number;
    std::string player_file, goalie_file, team_file;
    // label to show
    std::string label;
};

static std::vector<ArchiveSeason> load_archive_index()
{
    std::vector<ArchiveSeason> v;
    for(auto& l : read_lines("archive_index.txt")){
        auto f=pipe_split(l); if(f.size()<4) continue;
        ArchiveSeason a;
        a.number=std::stoi(f[0]);
        a.player_file=f[1]; a.goalie_file=f[2]; a.team_file=f[3];
        a.label="Season "+f[0];
        if(f.size()>=5) a.label=f[4]; // optional label
        v.push_back(a);
    }
    return v;
}

static int next_season_number()
{
    int mx=0;
    for(auto& a : load_archive_index()) mx=std::max(mx,a.number);
    return mx+1;
}

static void archive_season()
{
    int n=next_season_number();
    auto rename_if=[&](const std::string& src,const std::string& dst){
        if(fs::exists(src)){ fs::rename(src,dst); return dst; }
        return std::string("");
    };
    std::string pf = rename_if("season_scores.txt",
                                "archive_season_"+std::to_string(n)+"_scores.txt");
    std::string gf = rename_if("goalie_scores.txt",
                                "archive_goalie_season_"+std::to_string(n)+".txt");
    std::string tf = rename_if("team_history.txt",
                                "archive_team_season_"+std::to_string(n)+".txt");

    // Also clear col_ and team_ snapshot files
    for(auto& e : fs::directory_iterator(".")){
        auto fn=e.path().filename().string();
        if(fn.rfind("col_",0)==0||fn.rfind("team_",0)==0)
            fs::remove(e.path());
    }

    // Append to archive_index.txt
    std::ofstream ai("archive_index.txt",std::ios::app);
    ai<<n<<"|"<<pf<<"|"<<gf<<"|"<<tf<<"|Season "<<n<<"\n";
}

// ═══════════════════════════════════════════════════════════════════════════════
//  LINE GRAPH
// ═══════════════════════════════════════════════════════════════════════════════
struct Series { std::string name; std::vector<float> games,vals; ImU32 color; };

// ═══════════════════════════════════════════════════════════════════════════════
//  LINE GRAPH  —  supports up to 82 games, filled area under line,
//                 styled dark with green/red series like the screenshot
// ═══════════════════════════════════════════════════════════════════════════════
static void draw_graph(const std::vector<Series>& series, const char* ylabel, ImVec2 sz)
{
    if(series.empty()){ ImGui::TextColored(kDim,"No season data yet."); return; }
    ImVec2 orig=ImGui::GetCursorScreenPos();
    ImDrawList* dl=ImGui::GetWindowDrawList();
    const float PL=52,PR=24,PT=20,PB=32;
    float pw=sz.x-PL-PR, ph=sz.y-PT-PB;
    ImVec2 tl={orig.x+PL,orig.y+PT}, br={tl.x+pw,tl.y+ph};

    // Dark background matching screenshot style
    dl->AddRectFilled(orig,{orig.x+sz.x,orig.y+sz.y},IM_COL32(10,12,10,255),4);
    dl->AddRectFilled(tl,br,IM_COL32(15,18,15,255));

    float vmin=1e9f,vmax=-1e9f,gmin=1e9f,gmax=-1e9f;
    for(auto& s : series){
        for(float v : s.vals){ vmin=std::min(vmin,v); vmax=std::max(vmax,v); }
        for(float g : s.games){ gmin=std::min(gmin,g); gmax=std::max(gmax,g); }
    }
    if(vmin==vmax){ vmin-=1; vmax+=1; }
    // Cap to 82 games max (full NHL season)
    gmax=std::min(gmax,gmin+81.f);

    // Horizontal grid lines
    for(int i=0;i<=4;i++){
        float t=(float)i/4, y=tl.y+ph*t;
        dl->AddLine({tl.x,y},{br.x,y},IM_COL32(35,45,35,200));
        char buf[16]; snprintf(buf,16,"%.1f",vmax-(vmax-vmin)*t);
        dl->AddText({orig.x+2,y-7},IM_COL32(120,140,120,255),buf);
    }

    // X axis labels — smart spacing so they don't overlap for 82 games
    int ng=(int)(gmax-gmin)+1;
    int step = ng<=10?1:ng<=20?2:ng<=42?5:10;
    for(int i=0;i<ng;i++){
        float g=gmin+i;
        if(i%step!=0) continue;
        float x=tl.x+pw*(g-gmin)/(gmax-gmin);
        dl->AddLine({x,tl.y},{x,br.y},IM_COL32(30,40,30,120));
        char buf[16]; snprintf(buf,16,"G%d",(int)g);
        dl->AddText({x-8,br.y+5},IM_COL32(120,140,120,255),buf);
    }

    dl->AddText({orig.x+2,tl.y},IM_COL32(150,180,150,255),ylabel);
    dl->AddRect(tl,br,IM_COL32(40,55,40,255));

    // Hover detection
    ImVec2 mouse=ImGui::GetMousePos();
    bool hov=(mouse.x>=tl.x&&mouse.x<=br.x&&mouse.y>=tl.y&&mouse.y<=br.y);
    float hg=hov?gmin+(mouse.x-tl.x)/pw*(gmax-gmin):-1;

    // Draw each series with filled area under the line
    for(auto& s : series){
        if(s.vals.empty()) continue;

        // Filled area under the line (semi-transparent)
        ImU32 fill_col = (s.color & 0x00FFFFFF) | 0x40000000; // 25% alpha

        // Build filled polygon points
        std::vector<ImVec2> poly;
        // Start at bottom-left of first point
        if(!s.vals.empty()){
            float x0=tl.x+pw*(s.games[0]-gmin)/(gmax-gmin);
            poly.push_back({x0, br.y});
            for(size_t i=0;i<s.vals.size();i++){
                float x=tl.x+pw*(s.games[i]-gmin)/(gmax-gmin);
                float y=tl.y+ph*(1-(s.vals[i]-vmin)/(vmax-vmin));
                poly.push_back({x,y});
            }
            float xlast=tl.x+pw*(s.games.back()-gmin)/(gmax-gmin);
            poly.push_back({xlast, br.y});
            dl->AddConvexPolyFilled(poly.data(),(int)poly.size(),fill_col);
        }

        // Draw the line on top
        for(size_t i=1;i<s.vals.size();i++){
            float x0=tl.x+pw*(s.games[i-1]-gmin)/(gmax-gmin);
            float y0=tl.y+ph*(1-(s.vals[i-1]-vmin)/(vmax-vmin));
            float x1=tl.x+pw*(s.games[i]-gmin)/(gmax-gmin);
            float y1=tl.y+ph*(1-(s.vals[i]-vmin)/(vmax-vmin));
            dl->AddLine({x0,y0},{x1,y1},s.color,2.f);
        }

        // Dots — smaller when many games to avoid clutter
        float dot_r = ng>20 ? 2.5f : 4.f;
        for(size_t i=0;i<s.vals.size();i++){
            float x=tl.x+pw*(s.games[i]-gmin)/(gmax-gmin);
            float y=tl.y+ph*(1-(s.vals[i]-vmin)/(vmax-vmin));
            bool is_hovered=hov&&std::fabs(s.games[i]-hg)<.7f;
            dl->AddCircleFilled({x,y},is_hovered?7.f:dot_r,s.color);
            if(is_hovered)
                ImGui::SetTooltip("%s  G%d: %.2f",s.name.c_str(),(int)s.games[i],s.vals[i]);
        }

        // Legend top-right
        float lx=br.x-140, ly=tl.y+6+(&s-series.data())*15.f;
        if(ly<br.y-10){
            dl->AddLine({lx,ly+5},{lx+16,ly+5},s.color,2.f);
            dl->AddText({lx+20,ly},IM_COL32(200,210,200,255),s.name.c_str());
        }
    }
    ImGui::Dummy(sz);
}

static std::vector<Series> build_series(const std::vector<SeasonEntry>& season,
                                         bool use_off, int top_n=6)
{
    std::map<std::string,float> sum; std::map<std::string,int> cnt;
    for(auto& e : season){ float v=use_off?off_score(e):def_score(e);
        sum[e.player]+=v; cnt[e.player]++; }
    std::vector<std::pair<float,std::string>> ranked;
    for(auto& [p,s] : sum) ranked.push_back({s/cnt[p],p});
    std::sort(ranked.begin(),ranked.end(),[](auto& a,auto& b){return a.first>b.first;});
    if((int)ranked.size()>top_n) ranked.resize(top_n);
    std::vector<Series> out;
    for(int i=0;i<(int)ranked.size();i++){
        Series s; s.name=ranked[i].second; s.color=PAL[i%6];
        for(auto& e : season) if(e.player==s.name)
            { s.games.push_back(e.game); s.vals.push_back(use_off?off_score(e):def_score(e)); }
        out.push_back(s);
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PLAYER POPUP  (click player name)
// ═══════════════════════════════════════════════════════════════════════════════
static std::string g_popup_player;
static bool        g_popup_open=false;

static void draw_player_popup(const std::vector<SeasonEntry>& season)
{
    if(!g_popup_open) return;
    ImGui::SetNextWindowSize({520,390},ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always,{.5f,.5f});
    if(ImGui::Begin(g_popup_player.c_str(),&g_popup_open,
                    ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse)){
        std::vector<SeasonEntry> mine;
        for(auto& e : season) if(e.player==g_popup_player) mine.push_back(e);
        if(mine.empty()){
            ImGui::TextColored(kDim,"No season data for this player yet.");
        } else {
            float lo=off_score(mine.back()), ld=def_score(mine.back());
            float ao=0,ad=0;
            for(auto& e : mine){ ao+=off_score(e); ad+=def_score(e); }
            ao/=mine.size(); ad/=mine.size();

            ImGui::TextColored(kGold,"Games Played:"); ImGui::SameLine();
            ImGui::Text("%d",(int)mine.size());
            ImGui::Spacing();
            ImGui::Columns(2,"##pc",false);
            ImGui::TextColored(kGold,"Latest Off Score:"); ImGui::SameLine();
            ImGui::TextColored(kGreen,"%.2f",lo);
            ImGui::TextColored(kGold,"Latest Def Score:"); ImGui::SameLine();
            ImGui::TextColored(kBlue,"%.2f",ld);
            ImGui::NextColumn();
            ImGui::TextColored(kGold,"Season Avg Off:"); ImGui::SameLine();
            ImGui::TextColored(kGreen,"%.2f",ao);
            ImGui::TextColored(kGold,"Season Avg Def:"); ImGui::SameLine();
            ImGui::TextColored(kBlue,"%.2f",ad);
            ImGui::Columns(1);
            ImGui::Separator(); ImGui::Spacing();

            Series so,sd;
            so.name="Offensive"; so.color=PAL[1];
            sd.name="Defensive"; sd.color=PAL[0];
            for(auto& e : mine){
                so.games.push_back(e.game); so.vals.push_back(off_score(e));
                sd.games.push_back(e.game); sd.vals.push_back(def_score(e));
            }
            draw_graph({so,sd},"Score",
                       {ImGui::GetContentRegionAvail().x-4,160});
        }
    }
    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  TEAM SCOREBOARD  (shared renderer)
// ═══════════════════════════════════════════════════════════════════════════════
static void draw_scoreboard(const std::string& la,const std::string& ls,
                              const std::string& ra,const std::string& rs,
                              const std::vector<std::string>& labels,
                              const std::vector<std::string>& lv,
                              const std::vector<std::string>& rv)
{
    float ww=ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((ww-340)*.5f);
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextColored(kRed,"%s",la.c_str()); ImGui::SameLine(0,14);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,IM_COL32(225,225,225,255));
    ImGui::BeginChild("##scb",{120,42},true);
    ImGui::SetWindowFontScale(1.6f);
    float sw=ImGui::CalcTextSize((ls+" - "+rs).c_str()).x;
    ImGui::SetCursorPosX((120-sw)*.5f);
    ImGui::TextColored({.05f,.05f,.05f,1.f},"%s - %s",ls.c_str(),rs.c_str());
    ImGui::SetWindowFontScale(1.f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine(0,14); ImGui::TextColored(kBlue,"%s",ra.c_str());
    ImGui::SetWindowFontScale(1.f);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    ImGuiTableFlags tf=ImGuiTableFlags_SizingFixedFit|ImGuiTableFlags_BordersInnerH;
    if(ImGui::BeginTable("scbtbl",3,tf,{ww-14,0})){
        ImGui::TableSetupColumn("l",ImGuiTableColumnFlags_WidthFixed,130);
        ImGui::TableSetupColumn("m",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("r",ImGuiTableColumnFlags_WidthFixed,130);
        for(size_t i=0;i<labels.size();i++){
            const std::string& lvs=i<lv.size()?lv[i]:"-";
            const std::string& rvs=i<rv.size()?rv[i]:"-";
            float fl=to_num(lvs), fr=to_num(rvs);
            bool lw=fl>fr, rw=fr>fl;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetWindowFontScale(1.1f);
            ImGui::TextColored(lw?kGold:kWhite,"%s",lvs.c_str());
            ImGui::SetWindowFontScale(1.f);
            ImGui::TableSetColumnIndex(1);
            float cw=ImGui::GetColumnWidth();
            float tw=ImGui::CalcTextSize(labels[i].c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(cw-tw)*.5f);
            ImGui::TextColored(kDim,"%s",labels[i].c_str());
            float tot=fl+fr; float lf=tot>0?fl/tot:.5f;
            float bw=cw-20, bx=ImGui::GetCursorScreenPos().x+10;
            float by=ImGui::GetCursorScreenPos().y+2;
            ImDrawList* d=ImGui::GetWindowDrawList();
            d->AddRectFilled({bx,by},{bx+bw,by+4},IM_COL32(50,50,60,255),2);
            d->AddRectFilled({bx,by},{bx+bw*lf,by+4},IM_COL32(175,30,45,255),2);
            d->AddRectFilled({bx+bw*lf,by},{bx+bw,by+4},IM_COL32(0,62,126,255),2);
            ImGui::Dummy({cw,8});
            ImGui::TableSetColumnIndex(2);
            ImGui::SetWindowFontScale(1.1f);
            float rw2=ImGui::CalcTextSize(rvs.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX()+130-rw2-8);
            ImGui::TextColored(rw?kGold:kWhite,"%s",rvs.c_str());
            ImGui::SetWindowFontScale(1.f);
        }
        ImGui::EndTable();
    }
    ImGui::TextColored(kDim,"  gold = winning stat | red bar = %s | blue bar = %s",
                       la.c_str(),ra.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════════
//  TAB: PLAYER STATS
// ═══════════════════════════════════════════════════════════════════════════════
static void tab_players(PlayerTable& tbl,
                         std::vector<SeasonEntry>& season,
                         std::vector<size_t>& row_order,
                         int& sort_col, bool& sort_asc,
                         char* fbuf,
                         UploadJob& upload)
{
    upload_button("  Upload Player Screenshot(s)  ", upload,
                  "hockey_stats_extractor.exe", true,
                  L"Select Player Stats Screenshots");
    ImGui::Separator();

    if(ImGui::BeginTabBar("##ptabs")){
        // ── Current Game ──────────────────────────────────────────────────────
        if(ImGui::BeginTabItem("  Current Game  ")){
            ImGui::TextColored(kGold,"Player Stats — Current Game");
            ImGui::SameLine(0,18);
            ImGui::TextUnformatted("Filter:"); ImGui::SameLine();
            ImGui::SetNextItemWidth(155);
            ImGui::InputText("##pf",fbuf,128);
            ImGui::SameLine(0,5);
            if(ImGui::Button("Clear##pfc")) fbuf[0]='\0';
            ImGui::Separator();

            if(tbl.empty()){
                ImGui::TextColored(kDim,"No data — upload a player screenshot.");
                ImGui::EndTabItem(); ImGui::EndTabBar(); return;
            }
            size_t nc=tbl.columns.size();
            ImGuiTableFlags tf=
                ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|
                ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY|
                ImGuiTableFlags_Sortable|ImGuiTableFlags_Resizable|
                ImGuiTableFlags_Reorderable|ImGuiTableFlags_Hideable|
                ImGuiTableFlags_SizingFixedFit;
            float th=ImGui::GetContentRegionAvail().y-28.f;
            if(ImGui::BeginTable("ptbl",(int)nc,tf,{0,th})){
                for(size_t c=0;c<nc;c++){
                    float w=c==0?160.f:80.f;
                    ImGui::TableSetupColumn(tbl.columns[c].header.c_str(),
                        ImGuiTableColumnFlags_DefaultSort,w,(ImGuiID)c);
                }
                ImGui::TableSetupScrollFreeze(1,1);
                ImGui::TableHeadersRow();
                if(ImGuiTableSortSpecs* sp=ImGui::TableGetSortSpecs()){
                    if(sp->SpecsDirty&&sp->SpecsCount>0){
                        sort_col=(int)sp->Specs[0].ColumnIndex;
                        sort_asc=sp->Specs[0].SortDirection==ImGuiSortDirection_Ascending;
                        std::sort(row_order.begin(),row_order.end(),[&](size_t a,size_t b){
                            auto& col=tbl.columns[sort_col].values;
                            const auto& va=a<col.size()?col[a]:std::string();
                            const auto& vb=b<col.size()?col[b]:std::string();
                            float fa=to_num(va),fb=to_num(vb);
                            bool num=fa||fb||va=="0"||vb=="0";
                            return sort_asc?(num?fa<fb:va<vb):(num?fa>fb:va>vb);
                        });
                        sp->SpecsDirty=false;
                    }
                }
                std::string fl(fbuf);
                std::transform(fl.begin(),fl.end(),fl.begin(),::tolower);
                int rn=0;
                for(size_t ri : row_order){
                    if(!fl.empty()){
                        std::string pn=ri<tbl.players.size()?tbl.players[ri]:"";
                        std::transform(pn.begin(),pn.end(),pn.begin(),::tolower);
                        if(pn.find(fl)==std::string::npos) continue;
                    }
                    ImGui::TableNextRow();
                    if(rn++%2==0)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,IM_COL32(28,38,58,255));
                    for(size_t c=0;c<nc;c++){
                        ImGui::TableSetColumnIndex((int)c);
                        const auto& vals=tbl.columns[c].values;
                        const char* txt=ri<vals.size()?vals[ri].c_str():"";
                        if(c==0){
                            ImGui::PushStyleColor(ImGuiCol_Text,      {.55f,.80f,1.f,1.f});
                            ImGui::PushStyleColor(ImGuiCol_Button,     {0,0,0,0});
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.10f,.20f,.40f,1.f});
                            if(ImGui::SmallButton(txt)){
                                g_popup_player=txt; g_popup_open=true;
                            }
                            ImGui::PopStyleColor(3);
                        } else {
                            if((int)c==sort_col) ImGui::TextColored(kGold,"%s",txt);
                            else ImGui::TextUnformatted(txt);
                        }
                    }
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("  Click a player name to view their score breakdown & graph");
            ImGui::EndTabItem();
        }

        // ── Season Totals ─────────────────────────────────────────────────────
        if(ImGui::BeginTabItem("  Season Totals  ")){
            auto totals=build_totals(season);
            if(totals.empty()){
                ImGui::TextColored(kDim,"No season data yet.");
                ImGui::EndTabItem(); ImGui::EndTabBar(); return;
            }
            std::vector<PlayerTotals> rows;
            for(auto& [n,t]:totals) rows.push_back(t);
            std::sort(rows.begin(),rows.end(),[](auto& a,auto& b){return a.points>b.points;});

            ImGuiTableFlags tf=
                ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|
                ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY|
                ImGuiTableFlags_SizingFixedFit;
            float th=ImGui::GetContentRegionAvail().y-12.f;
            if(ImGui::BeginTable("stot",13,tf,{0,th})){
                const char* hdrs[]={"Player","GP","G","A","PTS","SH","HIT","PPG","SHG","PIM","+/-","Avg Off","Avg Def"};
                for(auto* h:hdrs)
                    ImGui::TableSetupColumn(h,0,(h[0]=='P'&&h[1]=='l')?150.f:72.f);
                ImGui::TableSetupScrollFreeze(1,1);
                ImGui::TableHeadersRow();
                int rn=0;
                for(auto& t:rows){
                    ImGui::TableNextRow();
                    if(rn++%2==0)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,IM_COL32(28,38,58,255));
                    int c=0;
                    ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kWhite,"%s",t.name.c_str());
                    ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kGold,"%d",t.gp);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.goals);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.assists);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.points);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.shots);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.hits);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.ppg);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.shg);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.pentime);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.pm);
                    ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kGreen,"%.2f",t.avg_off());
                    ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kBlue,"%.2f",t.avg_def());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  TAB: TEAM STATS
// ═══════════════════════════════════════════════════════════════════════════════
static int g_sel_team_game=-1; // -1 = current

static void tab_team(const TeamStats& ts,
                      const std::vector<TeamGame>& history,
                      UploadJob& upload)
{
    upload_button("  Upload Team Screenshot  ", upload,
                  "hockey_team_extractor.exe", false,
                  L"Select Team Stats Screenshot");
    ImGui::Separator();

    if(!ts.loaded && history.empty()){
        ImGui::TextColored(kDim,"No team data — upload a team screenshot.");
        return;
    }

    const float LIST_W=190.f;
    ImGui::BeginChild("##tlist",{LIST_W,0},true);
    ImGui::TextColored(kGold,"Games");
    ImGui::Separator();
    if(ts.loaded){
        bool sel=(g_sel_team_game==-1);
        if(ImGui::Selectable("  Current Game",sel)) g_sel_team_game=-1;
    }
    for(auto& g : history){
        char lb[72]; snprintf(lb,sizeof(lb),"  G%d  %s %s-%s %s",
                               g.game,g.la.c_str(),g.ls.c_str(),g.rs.c_str(),g.ra.c_str());
        bool sel=(g_sel_team_game==g.game);
        if(ImGui::Selectable(lb,sel)) g_sel_team_game=g.game;
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##tdetail",{0,0},false);
    if(g_sel_team_game==-1 && ts.loaded){
        draw_scoreboard(ts.la,ts.ls,ts.ra,ts.rs,ts.labels,ts.lv,ts.rv);
    } else {
        bool found=false;
        for(auto& g : history){
            if(g.game==g_sel_team_game){
                draw_scoreboard(g.la,g.ls,g.ra,g.rs,g.labels,g.lvals,g.rvals);
                found=true; break;
            }
        }
        if(!found) ImGui::TextColored(kDim,"Select a game from the list.");
    }
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  TAB: GOALTENDERS
// ═══════════════════════════════════════════════════════════════════════════════
static void tab_goalies(PlayerTable& gtbl,
                         const std::vector<GoalieEntry>& gs,
                         UploadJob& upload)
{
    upload_button("  Upload Goalie Screenshot(s)  ", upload,
                  "hockey_goalie_extractor.exe", true,
                  L"Select Goalie Stats Screenshots");
    ImGui::Separator();

    if(ImGui::BeginTabBar("##gtabs")){
        // Current Game
        if(ImGui::BeginTabItem("  Current Game  ")){
            if(gtbl.empty()){
                ImGui::TextColored(kDim,"No goalie data — upload a screenshot.");
                ImGui::EndTabItem(); ImGui::EndTabBar(); return;
            }
            size_t nc=gtbl.columns.size();
            ImGuiTableFlags tf=
                ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|
                ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY|
                ImGuiTableFlags_Resizable|ImGuiTableFlags_SizingFixedFit;
            float th=ImGui::GetContentRegionAvail().y-12.f;
            if(ImGui::BeginTable("gtbl",(int)nc,tf,{0,th})){
                for(size_t c=0;c<nc;c++)
                    ImGui::TableSetupColumn(gtbl.columns[c].header.c_str(),0,c==0?150.f:80.f);
                ImGui::TableSetupScrollFreeze(1,1);
                ImGui::TableHeadersRow();
                for(size_t r=0;r<gtbl.players.size();r++){
                    ImGui::TableNextRow();
                    for(size_t c=0;c<nc;c++){
                        ImGui::TableSetColumnIndex((int)c);
                        const auto& v=gtbl.columns[c].values;
                        ImGui::TextUnformatted(r<v.size()?v[r].c_str():"");
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        // Season Stats
        if(ImGui::BeginTabItem("  Season Stats  ")){
            if(gs.empty()){
                ImGui::TextColored(kDim,"No goalie season data yet.");
                ImGui::EndTabItem(); ImGui::EndTabBar(); return;
            }
            struct GTot{ std::string name; int gp=0;
                float sa=0,svs=0,ga=0,svp_sum=0,gaa_sum=0;
                float avg_svp()const{return gp?svp_sum/gp:0;}
                float avg_gaa()const{return gp?gaa_sum/gp:0;} };
            std::map<std::string,GTot> gm;
            for(auto& e:gs){
                auto& t=gm[e.player]; t.name=e.player; t.gp++;
                t.sa+=e.sa; t.svs+=e.svs; t.ga+=e.ga;
                t.svp_sum+=e.svp; t.gaa_sum+=e.gaa;
            }
            std::vector<GTot> grows;
            for(auto& [n,t]:gm) grows.push_back(t);
            std::sort(grows.begin(),grows.end(),[](auto& a,auto& b){return a.avg_svp()>b.avg_svp();});

            ImGuiTableFlags tf=
                ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|
                ImGuiTableFlags_SizingFixedFit|ImGuiTableFlags_ScrollY;
            float th=ImGui::GetContentRegionAvail().y-12.f;
            if(ImGui::BeginTable("gstot",7,tf,{0,th})){
                const char* hdrs[]={"Player","GP","SA","SVS","GA","Avg SV%","Avg GAA"};
                for(auto* h:hdrs)
                    ImGui::TableSetupColumn(h,0,(h[0]=='P')?150.f:80.f);
                ImGui::TableSetupScrollFreeze(1,1);
                ImGui::TableHeadersRow();
                int rn=0;
                for(auto& t:grows){
                    ImGui::TableNextRow();
                    if(rn++%2==0)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,IM_COL32(28,38,58,255));
                    int c=0;
                    ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kWhite,"%s",t.name.c_str());
                    ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kGold,"%d",t.gp);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.sa);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.svs);
                    ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.ga);
                    ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kGreen,"%.3f",t.avg_svp());
                    ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kRed,"%.2f",t.avg_gaa());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  TAB: DATA
// ═══════════════════════════════════════════════════════════════════════════════
static void draw_notable(const std::vector<SeasonEntry>& season, bool use_off)
{
    auto totals=build_totals(season);
    std::vector<PlayerTotals> rows;
    for(auto& [n,t]:totals) rows.push_back(t);
    std::sort(rows.begin(),rows.end(),[&](auto& a,auto& b){
        return use_off?a.avg_off()>b.avg_off():a.avg_def()>b.avg_def();});
    if(rows.empty()){ ImGui::TextColored(kDim,"No data."); return; }
    int show=std::min(3,(int)rows.size());
    ImGui::Columns(2,"nc",true);
    ImGui::TextColored(kGreen,"Notable Players"); ImGui::Separator();
    for(int i=0;i<show;i++){
        ImGui::TextColored(kGold,"%-22s",rows[i].name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(kGreen,"%.2f",use_off?rows[i].avg_off():rows[i].avg_def());
    }
    ImGui::NextColumn();
    ImGui::TextColored(kRed,"Concerning Players"); ImGui::Separator();
    int n=(int)rows.size();
    for(int i=n-1;i>=std::max(0,n-show);i--){
        ImGui::TextColored(kGold,"%-22s",rows[i].name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(kRed,"%.2f",use_off?rows[i].avg_off():rows[i].avg_def());
    }
    ImGui::Columns(1);
}

static void tab_data(const std::vector<SeasonEntry>& season,
                      const std::vector<GoalieEntry>& gseason)
{
    if(ImGui::BeginTabBar("##dtabs")){
        // Offensive
        if(ImGui::BeginTabItem("  Offensive  ")){
            ImGui::TextColored(kGold,"Offensive Score");
            ImGui::SameLine(0,8);
            ImGui::TextColored(kDim,"= (G*0.75)+(SH*0.09)+(PPG*0.10)-(-PlusMinus*0.10)");
            ImGui::Separator();
            auto s=build_series(season,true,6);
            float gh=std::min(210.f,ImGui::GetContentRegionAvail().y*.44f);
            draw_graph(s,"Off",{ImGui::GetContentRegionAvail().x-6,gh});
            ImGui::Separator();
            draw_notable(season,true);
            ImGui::EndTabItem();
        }
        // Defensive
        if(ImGui::BeginTabItem("  Defensive  ")){
            ImGui::TextColored(kBlue,"Defensive Score");
            ImGui::SameLine(0,8);
            ImGui::TextColored(kDim,"= (PlusMinus*0.15)+(Hits*0.10)-(PIM*0.09)");
            ImGui::Separator();
            auto s=build_series(season,false,6);
            float gh=std::min(210.f,ImGui::GetContentRegionAvail().y*.44f);
            draw_graph(s,"Def",{ImGui::GetContentRegionAvail().x-6,gh});
            ImGui::Separator();
            draw_notable(season,false);
            ImGui::EndTabItem();
        }
        // Goaltending
        if(ImGui::BeginTabItem("  Goaltending  ")){
            ImGui::TextColored(kGold,"Goaltender Trends - Save%%");
            ImGui::Separator();
            if(gseason.empty()){
                ImGui::TextColored(kDim,"No goalie season data yet.");
                ImGui::EndTabItem(); ImGui::EndTabBar(); return;
            }
            std::map<std::string,Series> sm; int idx=0;
            for(auto& e:gseason){
                if(!sm.count(e.player))
                    { sm[e.player].name=e.player; sm[e.player].color=PAL[idx++%6]; }
                sm[e.player].games.push_back(e.game);
                sm[e.player].vals.push_back(e.svp);
            }
            std::vector<Series> sv;
            for(auto& [n,s]:sm) sv.push_back(s);
            float gh=std::min(210.f,ImGui::GetContentRegionAvail().y*.44f);
            draw_graph(sv,"SV%",{ImGui::GetContentRegionAvail().x-6,gh});
            ImGui::Separator();
            // GP summary table
            std::map<std::string,int> gp; std::map<std::string,float> svp_sum;
            for(auto& e:gseason){ gp[e.player]++; svp_sum[e.player]+=e.svp; }
            ImGui::TextColored(kGold,"Goalie GP Summary");
            if(ImGui::BeginTable("ggp",3,ImGuiTableFlags_Borders|ImGuiTableFlags_SizingFixedFit)){
                ImGui::TableSetupColumn("Player",0,150);
                ImGui::TableSetupColumn("GP",0,60);
                ImGui::TableSetupColumn("Avg SV%",0,90);
                ImGui::TableHeadersRow();
                for(auto& [n,g]:gp){
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextColored(kWhite,"%s",n.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::TextColored(kGold,"%d",g);
                    ImGui::TableSetColumnIndex(2); ImGui::TextColored(kGreen,"%.3f",svp_sum[n]/g);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        // Suggestions
        if(ImGui::BeginTabItem("  Suggestions  ")){
            if(season.empty()){
                ImGui::TextColored(kDim,"Upload player screenshots first.");
                ImGui::EndTabItem(); ImGui::EndTabBar(); return;
            }
            auto totals=build_totals(season);
            std::vector<PlayerTotals> rows;
            for(auto& [n,t]:totals) rows.push_back(t);
            std::sort(rows.begin(),rows.end(),[](auto& a,auto& b){
                return (a.avg_off()+a.avg_def())>(b.avg_off()+b.avg_def());});
            int n=(int)rows.size(), show=std::min(3,n);
            ImGui::TextColored(kGold,"Roster Suggestions  (Avg Off + Avg Def combined)");
            ImGui::Separator(); ImGui::Spacing();
            ImGui::TextColored(kGreen,"  Bump Up — top performers:");
            for(int i=0;i<show;i++)
                ImGui::BulletText("%-22s  GP:%d  Avg Off:%+.2f  Avg Def:%+.2f  Combined:%+.2f",
                    rows[i].name.c_str(),rows[i].gp,rows[i].avg_off(),rows[i].avg_def(),
                    rows[i].avg_off()+rows[i].avg_def());
            ImGui::Spacing();
            ImGui::TextColored(kRed,"  Consider Scratching — bottom performers:");
            for(int i=n-1;i>=n-show&&i>=0;i--)
                ImGui::BulletText("%-22s  GP:%d  Avg Off:%+.2f  Avg Def:%+.2f  Combined:%+.2f",
                    rows[i].name.c_str(),rows[i].gp,rows[i].avg_off(),rows[i].avg_def(),
                    rows[i].avg_off()+rows[i].avg_def());
            ImGui::Spacing(); ImGui::Separator();
            ImGui::TextColored(kDim,"  Off = (G*0.75)+(SH*0.09)+(PPG*0.10)-(-PlusMinus*0.10)");
            ImGui::TextColored(kDim,"  Def = (PlusMinus*0.15)+(Hits*0.10)-(PIM*0.09)");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PAST SEASONS MODAL
// ═══════════════════════════════════════════════════════════════════════════════
// State for viewing a past season inside the modal
static int  g_view_season=-1;       // which archive season we're previewing
static std::vector<SeasonEntry>  g_arch_season;
static std::vector<GoalieEntry>  g_arch_goalies;
static std::vector<TeamGame>     g_arch_team;

static void draw_past_seasons_modal(std::vector<ArchiveSeason>& archives, bool& show)
{
    if(!show) return;
    ImGui::SetNextWindowSize({900,580},ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always,{.5f,.5f});
    ImGui::OpenPopup("##psmodal");
    if(ImGui::BeginPopupModal("##psmodal",nullptr,
        ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoTitleBar)){

        ImGui::TextColored(kGold,"  Past Seasons");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x-60);
        if(ImGui::Button("  Close  ")){ show=false; ImGui::CloseCurrentPopup(); }
        ImGui::Separator();

        if(archives.empty()){
            ImGui::TextColored(kDim,"  No past seasons found. Use 'New Season' to archive the current season.");
            ImGui::EndPopup(); return;
        }

        // Left sidebar: season list with delete buttons
        static int  g_delete_confirm=-1;
        static bool g_delete_confirmed=false;
        ImGui::BeginChild("##pslist",{180,0},true);
        ImGui::TextColored(kGold,"Seasons"); ImGui::Separator();
        for(auto& a : archives){
            bool sel=(g_view_season==a.number);
            if(ImGui::Selectable(a.label.c_str(),sel)){
                g_view_season=a.number;
                g_delete_confirm=-1; // cancel any pending delete when switching
                g_arch_season = a.player_file.empty()?
                    std::vector<SeasonEntry>{}: load_season(a.player_file);
                g_arch_goalies= a.goalie_file.empty()?
                    std::vector<GoalieEntry>{}: load_goalies(a.goalie_file);
                g_arch_team   = a.team_file.empty()?
                    std::vector<TeamGame>{}: load_team_history(a.team_file);
            }
            // Red X on right side of each season row
            ImGui::SameLine(150);
            char del_id[32]; snprintf(del_id,sizeof(del_id),"X##d%d",a.number);
            ImGui::PushStyleColor(ImGuiCol_Button,       IM_COL32(120,15,15,255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,IM_COL32(200,35,35,255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(80,8,8,255));
            if(ImGui::SmallButton(del_id)){
                g_delete_confirm=a.number;
                g_delete_confirmed=false;
            }
            ImGui::PopStyleColor(3);
        }

        // Inline confirm — shown at bottom of sidebar when X is clicked
        if(g_delete_confirm>=0){
            ImGui::Separator();
            ImGui::TextColored(kRed,"Delete Season %d?",g_delete_confirm);
            ImGui::TextColored(kDim,"Cannot be undone.");
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button,       IM_COL32(120,15,15,255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,IM_COL32(200,35,35,255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(80,8,8,255));
            if(ImGui::Button("Yes, delete",{130,0})){
                g_delete_confirmed=true;
            }
            ImGui::PopStyleColor(3);
            if(ImGui::Button("Cancel",{130,0})){
                g_delete_confirm=-1;
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // Execute delete AFTER EndChild so we are outside the child scope
        if(g_delete_confirmed && g_delete_confirm>=0){
            for(auto& a : archives){
                if(a.number==g_delete_confirm){
                    if(!a.player_file.empty() && fs::exists(a.player_file))
                        fs::remove(a.player_file);
                    if(!a.goalie_file.empty() && fs::exists(a.goalie_file))
                        fs::remove(a.goalie_file);
                    if(!a.team_file.empty() && fs::exists(a.team_file))
                        fs::remove(a.team_file);
                    break;
                }
            }
            // Rewrite index without deleted entry
            {
                std::ofstream ai("archive_index.txt");
                for(auto& a : archives)
                    if(a.number!=g_delete_confirm)
                        ai<<a.number<<"|"<<a.player_file<<"|"
                          <<a.goalie_file<<"|"<<a.team_file<<"|"<<a.label<<"\n";
            }
            if(g_view_season==g_delete_confirm){
                g_view_season=-1;
                g_arch_season.clear();
                g_arch_goalies.clear();
                g_arch_team.clear();
            }
            archives=load_archive_index();
            g_delete_confirm=-1;
            g_delete_confirmed=false;
        }

        // Right detail panel
        ImGui::BeginChild("##psdetail",{0,0},false);
        if(g_view_season<0){
            ImGui::TextColored(kDim,"Select a season from the list.");
        } else {
            if(ImGui::BeginTabBar("##pstabs")){
                // Player totals
                if(ImGui::BeginTabItem("  Player Totals  ")){
                    if(g_arch_season.empty()){
                        ImGui::TextColored(kDim,"No player data for this season.");
                    } else {
                        auto totals=build_totals(g_arch_season);
                        std::vector<PlayerTotals> rows;
                        for(auto& [n,t]:totals) rows.push_back(t);
                        std::sort(rows.begin(),rows.end(),[](auto& a,auto& b){return a.points>b.points;});
                        ImGuiTableFlags tf=ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|
                            ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY|ImGuiTableFlags_SizingFixedFit;
                        if(ImGui::BeginTable("aptot",13,tf,{0,ImGui::GetContentRegionAvail().y-12})){
                            const char* hdrs[]={"Player","GP","G","A","PTS","SH","HIT","PPG","SHG","PIM","+/-","Avg Off","Avg Def"};
                            for(auto* h:hdrs) ImGui::TableSetupColumn(h,0,(h[0]=='P'&&h[1]=='l')?150.f:72.f);
                            ImGui::TableSetupScrollFreeze(1,1);
                            ImGui::TableHeadersRow();
                            int rn=0;
                            for(auto& t:rows){
                                ImGui::TableNextRow();
                                if(rn++%2==0) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,IM_COL32(28,38,58,255));
                                int c=0;
                                ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kWhite,"%s",t.name.c_str());
                                ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kGold,"%d",t.gp);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.goals);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.assists);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.points);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.shots);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.hits);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.ppg);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.shg);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.pentime);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.pm);
                                ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kGreen,"%.2f",t.avg_off());
                                ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kBlue,"%.2f",t.avg_def());
                            }
                            ImGui::EndTable();
                        }
                    }
                    ImGui::EndTabItem();
                }
                // Team Results
                if(ImGui::BeginTabItem("  Team Results  ")){
                    if(g_arch_team.empty()){
                        ImGui::TextColored(kDim,"No team data for this season.");
                    } else {
                        ImGuiTableFlags tf=ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingFixedFit;
                        if(ImGui::BeginTable("atm",5,tf,{0,ImGui::GetContentRegionAvail().y-12})){
                            ImGui::TableSetupColumn("Game",0,60);
                            ImGui::TableSetupColumn("Home",0,80);
                            ImGui::TableSetupColumn("Score",0,80);
                            ImGui::TableSetupColumn("Away",0,80);
                            ImGui::TableSetupColumn("Result",0,80);
                            ImGui::TableHeadersRow();
                            for(auto& g:g_arch_team){
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0); ImGui::Text("G%d",g.game);
                                ImGui::TableSetColumnIndex(1); ImGui::TextColored(kWhite,"%s",g.la.c_str());
                                ImGui::TableSetColumnIndex(2);
                                ImGui::TextColored(kGold,"%s - %s",g.ls.c_str(),g.rs.c_str());
                                ImGui::TableSetColumnIndex(3); ImGui::TextColored(kWhite,"%s",g.ra.c_str());
                                ImGui::TableSetColumnIndex(4);
                                int ls=std::stoi(g.ls), rs=std::stoi(g.rs);
                                ImGui::TextColored(ls>rs?kGreen:kRed, ls>rs?"W":"L");
                            }
                            ImGui::EndTable();
                        }
                    }
                    ImGui::EndTabItem();
                }
                // Goalie Stats
                if(ImGui::BeginTabItem("  Goalie Stats  ")){
                    if(g_arch_goalies.empty()){
                        ImGui::TextColored(kDim,"No goalie data for this season.");
                    } else {
                        struct GTot{ std::string name; int gp=0;
                            float sa=0,svs=0,ga=0,svp_sum=0,gaa_sum=0;
                            float avg_svp()const{return gp?svp_sum/gp:0;}
                            float avg_gaa()const{return gp?gaa_sum/gp:0;} };
                        std::map<std::string,GTot> gm;
                        for(auto& e:g_arch_goalies){
                            auto& t=gm[e.player]; t.name=e.player; t.gp++;
                            t.sa+=e.sa; t.svs+=e.svs; t.ga+=e.ga;
                            t.svp_sum+=e.svp; t.gaa_sum+=e.gaa;
                        }
                        std::vector<GTot> grows;
                        for(auto& [n,t]:gm) grows.push_back(t);
                        std::sort(grows.begin(),grows.end(),[](auto& a,auto& b){return a.avg_svp()>b.avg_svp();});
                        ImGuiTableFlags tf=ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingFixedFit;
                        if(ImGui::BeginTable("agl",7,tf)){
                            const char* hdrs[]={"Player","GP","SA","SVS","GA","Avg SV%","Avg GAA"};
                            for(auto* h:hdrs) ImGui::TableSetupColumn(h,0,(h[0]=='P')?150.f:80.f);
                            ImGui::TableHeadersRow();
                            for(auto& t:grows){
                                ImGui::TableNextRow();
                                int c=0;
                                ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kWhite,"%s",t.name.c_str());
                                ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kGold,"%d",t.gp);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.sa);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.svs);
                                ImGui::TableSetColumnIndex(c++); ImGui::Text("%.0f",t.ga);
                                ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kGreen,"%.3f",t.avg_svp());
                                ImGui::TableSetColumnIndex(c++); ImGui::TextColored(kRed,"%.2f",t.avg_gaa());
                            }
                            ImGui::EndTable();
                        }
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════════════════════
int main(int,char**)
{
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);

    // Set working directory to the folder the exe lives in.
    // This ensures all txt files (col_*.txt, season_scores.txt etc.)
    // are always read from and written to the same place regardless of
    // how the exe was launched (double-click, VS Code, MSYS2, shortcut).
    SetCurrentDirectoryA(exe_dir().c_str());

    // Initial data load
    PlayerTable  player_tbl  = load_player_table();
    PlayerTable  goalie_tbl  = load_goalie_table();
    TeamStats    team_stats   = load_team_stats();
    auto season              = load_season();
    auto gseason             = load_goalies();
    auto team_history        = load_team_history();
    auto archives            = load_archive_index();

    size_t nrows=player_tbl.players.size();
    std::vector<size_t> row_order(nrows);
    std::iota(row_order.begin(),row_order.end(),0);
    int sort_col=-1; bool sort_asc=true;
    char filter_buf[128]={};

    UploadJob player_upload, team_upload, goalie_upload;

    bool show_new_season_confirm = false;
    bool show_past_seasons       = false;

    // SDL
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0){
        MessageBoxA(nullptr,SDL_GetError(),"SDL Error",MB_OK|MB_ICONERROR);
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window* win=SDL_CreateWindow(
        "Hockey Stats Viewer",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        1500,840,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl=SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win,gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO();
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    auto& st=ImGui::GetStyle();
    ImVec4* co=st.Colors;
    co[ImGuiCol_TitleBg]          = {.07f,.15f,.30f,1.f};
    co[ImGuiCol_TitleBgActive]    = {.10f,.20f,.45f,1.f};
    co[ImGuiCol_Header]           = {.10f,.20f,.45f,.60f};
    co[ImGuiCol_HeaderHovered]    = {.85f,.65f,.10f,.80f};
    co[ImGuiCol_HeaderActive]     = {.85f,.65f,.10f,1.f};
    co[ImGuiCol_Tab]              = {.07f,.15f,.30f,.86f};
    co[ImGuiCol_TabHovered]       = {.85f,.65f,.10f,.80f};
    co[ImGuiCol_TabActive]        = {.10f,.20f,.45f,1.f};
    co[ImGuiCol_Button]           = {.15f,.25f,.45f,1.f};
    co[ImGuiCol_ButtonHovered]    = {.85f,.65f,.10f,.85f};
    co[ImGuiCol_ButtonActive]     = {.75f,.55f,.05f,1.f};
    co[ImGuiCol_FrameBg]          = {.12f,.18f,.30f,1.f};
    co[ImGuiCol_PopupBg]          = {.09f,.12f,.20f,.98f};
    ImGui_ImplSDL2_InitForOpenGL(win,gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    bool running=true;
    while(running){
        SDL_Event ev;
        while(SDL_PollEvent(&ev)){
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if(ev.type==SDL_QUIT) running=false;
            if(ev.type==SDL_KEYDOWN&&ev.key.keysym.sym==SDLK_ESCAPE) running=false;
        }

        // Tick uploads
        tick_upload(player_upload,[&](){
            player_tbl=load_player_table();
            season=load_season();
            nrows=player_tbl.players.size();
            row_order.resize(nrows);
            std::iota(row_order.begin(),row_order.end(),0);
            sort_col=-1;
        });
        tick_upload(team_upload,[&](){
            team_stats=load_team_stats();
            team_history=load_team_history();
            g_sel_team_game=-1;
        });
        tick_upload(goalie_upload,[&](){
            goalie_tbl=load_goalie_table();
            gseason=load_goalies();
        });

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        int W,H; SDL_GetWindowSize(win,&W,&H);
        ImGui::SetNextWindowPos({0,0});
        ImGui::SetNextWindowSize({(float)W,(float)H});
        ImGui::Begin("##root",nullptr,
            ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|
            ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoTitleBar);

        // ── 4 main tabs ───────────────────────────────────────────────────────
        if(ImGui::BeginTabBar("##main")){
            if(ImGui::BeginTabItem("  Player Stats  ")){
                tab_players(player_tbl,season,row_order,sort_col,sort_asc,
                            filter_buf,player_upload);
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("  Team Stats  ")){
                tab_team(team_stats,team_history,team_upload);
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("  Goaltenders  ")){
                tab_goalies(goalie_tbl,gseason,goalie_upload);
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("  Data  ")){
                tab_data(season,gseason);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // ── Player popup ──────────────────────────────────────────────────────
        draw_player_popup(season);

        // ── Past Seasons button (bottom-left) ─────────────────────────────────
        ImGui::SetCursorPos({10.f,(float)H-42.f});
        ImGui::PushStyleColor(ImGuiCol_Button,      IM_COL32(20,50,100,255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,IM_COL32(40,90,180,255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(10,35,75,255));
        if(ImGui::Button("  Past Seasons  ",{150,30})){
            archives=load_archive_index();
            g_view_season=-1;
            show_past_seasons=true;
        }
        ImGui::PopStyleColor(3);

        // ── Debug API Key button (bottom-centre-left) ─────────────────────────
        static bool show_debug_key=false;
        ImGui::SetCursorPos({170.f,(float)H-42.f});
        ImGui::PushStyleColor(ImGuiCol_Button,      IM_COL32(40,40,40,255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,IM_COL32(70,70,70,255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(20,20,20,255));
        if(ImGui::Button("  Debug API Key  ",{145,30}))
            show_debug_key=true;
        ImGui::PopStyleColor(3);

        if(show_debug_key){
            ImGui::SetNextWindowSize({600,300},ImGuiCond_Always);
            ImGui::SetNextWindowPos({(float)W*.5f,(float)H*.5f},ImGuiCond_Always,{.5f,.5f});
            if(ImGui::Begin("API Key Debug",&show_debug_key,
                ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse)){
                ImGui::TextColored(kGold,"Where the app looks for your API key:");
                ImGui::Separator(); ImGui::Spacing();
                std::string info=debug_api_key_info();
                ImGui::TextUnformatted(info.c_str());
                ImGui::Spacing(); ImGui::Separator();
                ImGui::TextColored(kDim,"Put api_key.txt in the exe folder shown above.");
            }
            ImGui::End();
        }

        // ── New Season button (bottom-right) ──────────────────────────────────
        ImGui::SetCursorPos({(float)W-185.f,(float)H-42.f});
        ImGui::PushStyleColor(ImGuiCol_Button,      IM_COL32(130,15,15,255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,IM_COL32(190,35,35,255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(90,8,8,255));
        if(ImGui::Button("  New Season (Archive)  ",{178,30}))
            show_new_season_confirm=true;
        ImGui::PopStyleColor(3);

        // ── New Season confirm modal ──────────────────────────────────────────
        if(show_new_season_confirm){
            ImGui::OpenPopup("##nsconfirm");
            show_new_season_confirm=false;
        }
        ImGui::SetNextWindowPos({(float)W*.5f,(float)H*.5f},ImGuiCond_Always,{.5f,.5f});
        if(ImGui::BeginPopupModal("##nsconfirm",nullptr,
            ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoTitleBar)){
            ImGui::TextColored(kGold,"  Archive current season and start a new one?");
            ImGui::Spacing();
            ImGui::TextColored(kDim,"  Current season data will be saved to archive files.");
            ImGui::TextColored(kDim,"  All live score files will be cleared.");
            ImGui::TextColored(kDim,"  Screenshot snapshot files (col_*, team_*) will be removed.");
            ImGui::Spacing(); ImGui::Separator();
            if(ImGui::Button("  Yes, archive it  ",{160,0})){
                archive_season();
                player_tbl=PlayerTable(); goalie_tbl=PlayerTable();
                team_stats=TeamStats(); season.clear(); gseason.clear();
                team_history.clear(); row_order.clear(); sort_col=-1;
                player_upload={}; team_upload={}; goalie_upload={};
                g_popup_open=false; g_sel_team_game=-1;
                archives=load_archive_index();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0,20);
            if(ImGui::Button("  Cancel  ",{90,0})) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ── Past Seasons modal ────────────────────────────────────────────────
        draw_past_seasons_modal(archives, show_past_seasons);

        ImGui::End();

        ImGui::Render();
        glViewport(0,0,W,H);
        glClearColor(.08f,.10f,.18f,1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    CoUninitialize();
    return 0;
}

