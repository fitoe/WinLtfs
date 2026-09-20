#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <stdexcept>
#include <algorithm>

namespace fs = std::filesystem;
constexpr UINT DONE = WM_APP + 1;
enum { DEVICE=101, LETTER, REFRESH, MOUNT, UNMOUNT, LOCATE, LOGS, ADVANCED, RESET };
HWND window, devices, letters, refreshButton, mountButton, stopButton, locateButton, logsButton, statusLabel;
HFONT font;
HWND advancedButton, verbosityBox, syncBox, intervalBox, minimumBox, maximumBox;
std::vector<HWND> advancedControls; bool expanded=false;
std::vector<std::wstring> mountOptions;
std::vector<std::wstring> lastDevices; DWORD lastDrives=0; unsigned refreshTicks=0;
fs::path engine, logDir;
HANDLE engineProcess = nullptr;
DWORD enginePid = 0;
bool busy=false, closeAfterStop=false;
std::thread worker;
struct Result { std::wstring text; bool clean=false; };

std::wstring error(const wchar_t* prefix) {
    wchar_t* msg=nullptr; DWORD code=GetLastError();
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,code,0,reinterpret_cast<wchar_t*>(&msg),0,nullptr);
    std::wstring result=std::wstring(prefix)+L" ("+std::to_wstring(code)+L"): "+(msg?msg:L"Unknown error");
    LocalFree(msg); return result;
}
std::wstring quote(const std::wstring& arg) {
    std::wstring out=L"\""; size_t slashes=0;
    for(wchar_t c:arg) {
        if(c==L'\\') { ++slashes; continue; }
        if(c==L'\"') out.append(slashes*2+1,L'\\'); else out.append(slashes,L'\\');
        slashes=0; out+=c;
    }
    out.append(slashes*2,L'\\'); return out+L'\"';
}
std::string utf8(const std::wstring& s) {
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,0); WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr); return out;
}
fs::path executable() { wchar_t p[32768]; DWORD n=GetModuleFileNameW(nullptr,p,32768); return std::wstring(p,n); }
fs::path settings() {
    wchar_t p[MAX_PATH]; if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,p))) throw std::runtime_error("LocalAppData unavailable");
    return fs::path(p)/L"WinLtfsManager";
}
bool validEngine(const fs::path& p) {
    if(p.empty()) return false;
    for(auto name:{L"ltfs.exe",L"libltfs.dll",L"libdriver-ltotape-win.dll",L"libiosched-unified.dll",L"winfsp-x64.dll"})
        if(!fs::is_regular_file(p/name)) return false;
    return true;
}
void discover() {
    auto own=executable().parent_path();
    std::ifstream saved(settings()/L"native-engine.txt",std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(saved)),{});
    if(!text.empty()) { engine=fs::u8path(text); if(validEngine(engine)) return; }
    for(auto p:{own/L"engine",own}) if(validEngine(p)) {engine=p;return;}
    for(auto p=own;!p.empty();) {
        if(validEngine(p/L"dist")) {engine=p/L"dist";return;}
        auto parent=p.parent_path(); if(parent==p) break; p=parent;
    }
    engine.clear();
}
std::vector<std::wstring> tapeDevices() {
    for(DWORD size=4096;size<=1048576;size*=2) {
        std::vector<wchar_t> buffer(size);
        if(QueryDosDeviceW(nullptr,buffer.data(),size)) {
            std::vector<std::wstring> found;
            for(auto p=buffer.data();*p;p+=wcslen(p)+1) {
                std::wstring name=p;
                if(name.size()>4 && _wcsnicmp(p,L"Tape",4)==0 &&
                    std::all_of(name.begin()+4,name.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';})) found.push_back(name);
            }
            std::sort(found.begin(),found.end()); return found;
        }
        if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER) throw error(L"Device enumeration failed");
    }
    throw std::wstring(L"Device namespace is too large");
}
std::wstring selected(HWND box) {
    int i=static_cast<int>(SendMessageW(box,CB_GETCURSEL,0,0)); if(i==CB_ERR)return L"";
    int n=static_cast<int>(SendMessageW(box,CB_GETLBTEXTLEN,i,0));
    std::wstring out(n+1,0); SendMessageW(box,CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(out.data())); out.resize(n);return out;
}
void controls() {
    bool idle=!busy&&!engineProcess;
    EnableWindow(devices,idle);EnableWindow(letters,idle);EnableWindow(refreshButton,idle);EnableWindow(locateButton,idle);
    EnableWindow(mountButton,!busy&&(engineProcess||(validEngine(engine)&&!selected(devices).empty()&&!selected(letters).empty())));
    SetWindowTextW(mountButton,engineProcess?L"Safely unmount":L"Mount");
    for(auto c:advancedControls)EnableWindow(c,idle);
    EnableWindow(advancedButton,!busy);
    EnableWindow(stopButton,!busy&&engineProcess);EnableWindow(logsButton,!logDir.empty());
    ShowWindow(locateButton,validEngine(engine)?SW_HIDE:SW_SHOW);
}
void refresh() {
    auto old=selected(devices), letter=selected(letters); if(letter.empty()){std::ifstream saved(settings()/L"drive-letter.txt");char c=0;saved.get(c);letter=(c>='D'&&c<='Z')?std::wstring{static_cast<wchar_t>(c),L':'}:L"T:";}
    SendMessageW(devices,CB_RESETCONTENT,0,0); SendMessageW(letters,CB_RESETCONTENT,0,0);
    lastDevices=tapeDevices();
    for(auto& d:lastDevices) SendMessageW(devices,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(d.c_str()));
    auto i=SendMessageW(devices,CB_FINDSTRINGEXACT,-1,reinterpret_cast<LPARAM>(old.c_str())); SendMessageW(devices,CB_SETCURSEL,i==CB_ERR?0:i,0);
    DWORD used=GetLogicalDrives();lastDrives=used;
    for(wchar_t c=L'D';c<=L'Z';++c)if(!(used&(1u<<(c-L'A')))){std::wstring s{c,L':'};SendMessageW(letters,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s.c_str()));}
    i=SendMessageW(letters,CB_FINDSTRINGEXACT,-1,reinterpret_cast<LPARAM>(letter.c_str()));SendMessageW(letters,CB_SETCURSEL,i==CB_ERR?0:i,0);
    controls();
}
std::string readLog() {
    std::ifstream input(logDir/L"session.log",std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),{});
}
bool exited() {return engineProcess&&WaitForSingleObject(engineProcess,0)==WAIT_OBJECT_0;}
void releaseProcess() {if(engineProcess)CloseHandle(engineProcess);engineProcess=nullptr;enginePid=0;}
bool otherLtfs() {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)throw error(L"Cannot check existing LTFS processes");
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
    if(Process32FirstW(snapshot,&entry))do{if(_wcsicmp(entry.szExeFile,L"ltfs.exe")==0){found=true;break;}}while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return found;
}
void launch(const std::wstring& device,const std::wstring& drive,bool simulation=false) {
    if(drive.size()!=2||drive[1]!=L':'||drive[0]<L'D'||drive[0]>L'Z')throw std::wstring(L"Invalid drive letter");
    if(GetLogicalDrives()&(1u<<(drive[0]-L'A')))throw std::wstring(L"Drive letter is already in use. Refresh first.");
    if(!validEngine(engine))throw std::wstring(L"WinLtfs engine files are missing.");
    if(otherLtfs())throw std::wstring(L"An LTFS process is already running. Unmount it in its original application first.");
    if(!simulation){auto names=tapeDevices();if(std::find(names.begin(),names.end(),device)==names.end())throw std::wstring(L"Tape device is offline.");}
    logDir=settings()/L"sessions"/(std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(GetCurrentProcessId()));fs::create_directories(logDir);
    std::ofstream config(logDir/L"ltfs.conf",std::ios::binary);
    config<<"plugin driver ltotape_win "<<utf8((engine/L"libdriver-ltotape-win.dll").generic_wstring())<<"\nplugin driver file "<<utf8((engine/L"libdriver-file.dll").generic_wstring())
        <<"\nplugin iosched unified "<<utf8((engine/L"libiosched-unified.dll").generic_wstring())<<"\ndefault driver ltotape_win\ndefault iosched unified\ndefault kmi none\n";
    config.close();if(!config)throw std::wstring(L"Cannot write session configuration");
    std::wstring command=quote((engine/L"ltfs.exe").wstring());
    for(auto& a:std::vector<std::wstring>{drive,L"-f",L"-o",L"config_file="+(logDir/L"ltfs.conf").generic_wstring(),L"-o",L"tape_backend="+std::wstring(simulation?L"file":L"ltotape_win"),L"-o",L"devname="+(simulation?fs::path(device).generic_wstring():device),L"-o",L"work_directory="+logDir.generic_wstring(),})command+=L" "+quote(a);
    for(const auto& option:mountOptions)command+=L" -o "+quote(option);
    if(mountOptions.empty())command+=L" -o verbose=2";
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE log=CreateFileW((logDir/L"session.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(log==INVALID_HANDLE_VALUE)throw error(L"Cannot open session log");
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;si.wShowWindow=SW_HIDE;si.hStdOutput=si.hStdError=log;si.hStdInput=input;
    PROCESS_INFORMATION pi{};
    BOOL ok=CreateProcessW((engine/L"ltfs.exe").c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NEW_CONSOLE,nullptr,engine.c_str(),&si,&pi);
    auto failure=ok?L"":error(L"Cannot start WinLtfs");CloseHandle(log);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
    if(!ok)throw std::wstring(failure);
    CloseHandle(pi.hThread);engineProcess=pi.hProcess;enginePid=pi.dwProcessId;
    std::ofstream(logDir/L"pid.txt")<<enginePid;
}
void waitReady(const std::wstring& drive) {
    for(int i=0;i<900;++i){
        if(exited())throw std::wstring(L"WinLtfs exited before mounting. Check Logs.");
        if(readLog().find("Ready to receive file system requests")!=std::string::npos && (GetLogicalDrives()&(1u<<(drive[0]-L'A'))))return;
        Sleep(200);
    }
    throw std::wstring(L"Mount still pending. Use Unmount to request a clean stop; do not power off.");
}
bool unmount() {
    if(!engineProcess)return true;
    if(!exited()) {
        std::wstring cmd=quote(executable().wstring())+L" --signal "+std::to_wstring(enginePid);
        STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
        if(!CreateProcessW(executable().c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))throw error(L"Cannot start unmount helper");
        CloseHandle(pi.hThread);DWORD wait=WaitForSingleObject(pi.hProcess,5000),code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);
        if((wait!=WAIT_OBJECT_0||code!=0)&&!exited())throw std::wstring(L"Could not request unmount. The engine remains running.");
        if(WaitForSingleObject(engineProcess,90000)!=WAIT_OBJECT_0)throw std::wstring(L"Unmount still pending. Wait and retry. Engine has NOT been killed.");
    }
    DWORD code=1;GetExitCodeProcess(engineProcess,&code);
    bool clean=code==0&&readLog().find("Volume unmounted successfully")!=std::string::npos;
    if(!clean)throw std::wstring(L"Engine exited without confirmed clean unmount. Check Logs before powering off.");
    return true;
}
template<class F> void background(F operation) {
    busy=true;controls();
    worker=std::thread([operation]{auto result=new Result;try{*result=operation();}catch(const std::wstring& e){result->text=e;}catch(const std::exception& e){std::string s=e.what();result->text=std::wstring(s.begin(),s.end());}PostMessageW(window,DONE,0,reinterpret_cast<LPARAM>(result));});
}
void requestStop(){SetWindowTextW(statusLabel,L"Synchronizing and unmounting. Keep the tape drive powered on...");background([]{unmount();return Result{L"Unmounted successfully. You can power off the tape drive.",true};});}
HWND control(const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0){HWND c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    try {
    switch(msg){
    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(wp),GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_CREATE:{
        window=hwnd;font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        control(L"STATIC",L"Tape drive",0,24,28,110,25);devices=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,140,24,390,220,DEVICE);refreshButton=control(L"BUTTON",L"Refresh",WS_TABSTOP,546,23,120,30,REFRESH);
        control(L"STATIC",L"Drive letter",0,24,76,110,25);letters=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,140,72,390,350,LETTER);
        control(L"STATIC",L"Mounting may write to tape. Use physical write protection for read-only access.",0,24,124,660,48);
        mountButton=control(L"BUTTON",L"Mount",WS_TABSTOP,140,192,170,36,MOUNT);stopButton=nullptr;logsButton=control(L"BUTTON",L"Logs",WS_TABSTOP,326,192,100,36,LOGS);locateButton=control(L"BUTTON",L"Locate WinLtfs",WS_TABSTOP,442,192,190,36,LOCATE);
        statusLabel=control(L"STATIC",L"Not mounted",0,24,242,650,48);
        advancedButton=control(L"BUTTON",L"Advanced options",WS_TABSTOP,24,294,170,30,ADVANCED);
        auto advanced=[&](const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int h){HWND c=control(type,text,style,x,y,w,h);advancedControls.push_back(c);ShowWindow(c,SW_HIDE);return c;};
        advanced(L"STATIC",L"Logging",0,24,342,110,24);verbosityBox=advanced(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,140,338,240,160);
        for(auto t:{L"Normal (default)",L"Warnings only",L"Debug"})SendMessageW(verbosityBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t));
        SendMessageW(verbosityBox,CB_SETCURSEL,0,0);
        advanced(L"STATIC",L"Index sync",0,24,384,110,24);syncBox=advanced(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,140,380,240,160);
        for(auto t:{L"Periodic (default)",L"On file close",L"On unmount"})SendMessageW(syncBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t));
        SendMessageW(syncBox,CB_SETCURSEL,0,0);
        advanced(L"STATIC",L"Minutes",0,404,384,90,24);intervalBox=advanced(L"EDIT",L"5",WS_BORDER|ES_NUMBER|WS_TABSTOP,504,380,110,27);
        advanced(L"STATIC",L"Write cache",0,24,426,110,24);minimumBox=advanced(L"EDIT",L"25",WS_BORDER|ES_NUMBER|WS_TABSTOP,140,422,90,27);
        advanced(L"STATIC",L"to",0,240,426,30,24);maximumBox=advanced(L"EDIT",L"50",WS_BORDER|ES_NUMBER|WS_TABSTOP,280,422,90,27);advanced(L"STATIC",L"MiB (min / max)",0,390,426,180,24);
        advanced(L"STATIC",L"Defaults are recommended. Larger write cache is not a read-speed fix.",0,24,468,650,28);
        HWND reset=control(L"BUTTON",L"Restore defaults",WS_TABSTOP,24,506,170,30,RESET);advancedControls.push_back(reset);ShowWindow(reset,SW_HIDE);
        discover();refresh();
        if(!validEngine(engine))SetWindowTextW(statusLabel,L"WinLtfs was not found. Use Locate WinLtfs to select its directory.");
        else if(selected(devices).empty())SetWindowTextW(statusLabel,L"No tape drive detected. The list updates automatically.");
        SetTimer(hwnd,1,1000,nullptr);return 0;}
    case WM_COMMAND:
        if(busy)return 0;
        switch(LOWORD(wp)){
        case DEVICE:case LETTER:controls();break;
        case ADVANCED:expanded=!expanded;for(auto c:advancedControls)ShowWindow(c,expanded?SW_SHOW:SW_HIDE);SetWindowPos(hwnd,nullptr,0,0,714,expanded?590:375,SWP_NOMOVE|SWP_NOZORDER);break;
        case RESET:SendMessageW(verbosityBox,CB_SETCURSEL,0,0);SendMessageW(syncBox,CB_SETCURSEL,0,0);SetWindowTextW(intervalBox,L"5");SetWindowTextW(minimumBox,L"25");SetWindowTextW(maximumBox,L"50");break;
        case REFRESH:refresh();break;
        case LOGS:ShellExecuteW(hwnd,L"open",logDir.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
        case LOCATE:{BROWSEINFOW bi{};bi.hwndOwner=hwnd;bi.lpszTitle=L"Select the WinLtfs folder containing ltfs.exe and its DLLs";bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;auto item=SHBrowseForFolderW(&bi);if(item){wchar_t path[MAX_PATH];bool ok=SHGetPathFromIDListW(item,path);CoTaskMemFree(item);if(ok&&validEngine(path)){engine=path;fs::create_directories(settings());std::ofstream(settings()/L"native-engine.txt",std::ios::binary)<<utf8(engine.wstring());SetWindowTextW(statusLabel,L"Not mounted");controls();}else MessageBoxW(hwnd,L"Required WinLtfs engine files are missing.",L"WinLtfs",MB_ICONERROR);}break;}
        case MOUNT:{
            if(engineProcess){requestStop();break;}
                        auto number=[](HWND c,int low,int high){wchar_t text[32];GetWindowTextW(c,text,32);wchar_t* end;long n=wcstol(text,&end,10);if(!*text||*end||n<low||n>high)throw std::wstring(L"Invalid advanced setting. Minutes: 1-1440; cache: 1-4096 MiB.");return n;};
            long minutes=number(intervalBox,1,1440),minimum=number(minimumBox,1,4096),maximum=number(maximumBox,1,4096);
            if(minimum>maximum)throw std::wstring(L"Minimum cache must not exceed maximum cache.");
            int level=static_cast<int>(SendMessageW(verbosityBox,CB_GETCURSEL,0,0));int mode=static_cast<int>(SendMessageW(syncBox,CB_GETCURSEL,0,0));
            mountOptions={L"verbose="+std::to_wstring(level==1?1:level==2?3:2),L"sync_type="+(mode==1?std::wstring(L"close"):mode==2?std::wstring(L"unmount"):L"time@"+std::to_wstring(minutes)),L"min_pool_size="+std::to_wstring(minimum),L"max_pool_size="+std::to_wstring(maximum)};
            auto d=selected(devices),l=selected(letters);SetWindowTextW(statusLabel,L"Loading tape index, please wait...");background([d,l]{launch(d,l);waitReady(l);return Result{L"Mounted "+l+L" - "+d,false};});break;}
        case UNMOUNT:requestStop();break;
        }return 0;
    case DONE:{if(worker.joinable())worker.join();auto r=reinterpret_cast<Result*>(lp);busy=false;SetWindowTextW(statusLabel,r->text.c_str());bool clean=r->clean;
        if(r->text.rfind(L"Mounted ",0)==0){fs::create_directories(settings());std::ofstream saved(settings()/L"drive-letter.txt");saved<<static_cast<char>(selected(letters)[0]);}
        delete r;if(exited())releaseProcess();if(!engineProcess)refresh();controls();if(closeAfterStop&&clean)DestroyWindow(hwnd);else closeAfterStop=false;return 0;}
    case WM_TIMER:
        if(!busy&&!engineProcess&&++refreshTicks%3==0&&!SendMessageW(devices,CB_GETDROPPEDSTATE,0,0)&&!SendMessageW(letters,CB_GETDROPPEDSTATE,0,0)){
            auto current=tapeDevices();if(current!=lastDevices||GetLogicalDrives()!=lastDrives){refresh();if(validEngine(engine))SetWindowTextW(statusLabel,current.empty()?L"No tape drive detected. Waiting for a device...":L"Tape drive detected. Ready to mount.");}
        }
        if(!busy&&exited()){releaseProcess();SetWindowTextW(statusLabel,L"Engine exited unexpectedly. Check Logs before mounting again.");refresh();}return 0;
    case WM_CLOSE:if(busy){MessageBoxW(hwnd,L"Please wait for the current operation before closing.",L"WinLtfs",MB_OK);return 0;}if(engineProcess){closeAfterStop=true;requestStop();return 0;}DestroyWindow(hwnd);return 0;
    case WM_DESTROY:KillTimer(hwnd,1);DeleteObject(font);PostQuitMessage(0);return 0;
    }
    }catch(const std::wstring& e){MessageBoxW(hwnd,e.c_str(),L"WinLtfs",MB_ICONERROR);}catch(const std::exception& e){MessageBoxA(hwnd,e.what(),"WinLtfs",MB_ICONERROR);}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    int argc;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc==3&&std::wstring(argv[1])==L"--signal") {
        DWORD pid=wcstoul(argv[2],nullptr,10);LocalFree(argv);FreeConsole();if(!pid||!AttachConsole(pid))return 1;
        SetConsoleCtrlHandler(nullptr,TRUE);BOOL ok=GenerateConsoleCtrlEvent(CTRL_C_EVENT,0);Sleep(200);FreeConsole();return ok?0:2;
    }
    HANDLE singleton=CreateMutexW(nullptr,FALSE,L"Local\\WinLtfsNativeManager");if(!singleton||GetLastError()==ERROR_ALREADY_EXISTS){LocalFree(argv);MessageBoxW(nullptr,L"WinLtfs Manager is already running.",L"WinLtfs",MB_OK);return 1;}
    if(argc==5&&std::wstring(argv[1])==L"--smoke") {
        int code=0;try{mountOptions={L"verbose=2",L"sync_type=time@5",L"min_pool_size=25",L"max_pool_size=50"};engine=fs::absolute(argv[2]);std::wstring d=argv[3],l=argv[4];launch(d,l,true);waitReady(l);unmount();}catch(...){code=1;}releaseProcess();LocalFree(argv);CloseHandle(singleton);return code;
    }
    LocalFree(argv);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);SetProcessDPIAware();
    WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"WinLtfsNativeManager";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"WinLtfs Manager",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,714,375,nullptr,nullptr,instance,nullptr);
    ShowWindow(hwnd,show);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(worker.joinable())worker.join();
    releaseProcess();CoUninitialize();CloseHandle(singleton);return 0;
}
