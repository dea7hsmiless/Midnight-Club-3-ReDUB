/* Windows media metadata and playback bridge, isolated from game/render threads.
 * Windows PowerShell supplies WinRT without adding an SDK/build dependency. */
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
static SRWLOCK media_lock=SRWLOCK_INIT;
static wchar_t media_text[512]=L"PC MUSIC: CONNECTING...";
static wchar_t media_track[512];
static ULONGLONG media_until;
static int media_route_ready;
static atomic_int media_enabled;
static atomic_int media_gameplay,media_known_menu,media_native_hud;
static HANDLE media_map;
static volatile LONG *media_control;
static atomic_uint media_skip_sequence;
static void media_control_update(void){if(media_control)InterlockedExchange(media_control,(media_enabled?1:0)|(media_gameplay?2:0)|(media_known_menu?4:0));}
int windows_media_gameplay_get(void){return atomic_load(&media_gameplay);}
void windows_media_skip(int next){
    if(!media_control||!atomic_load(&media_enabled)||!atomic_load(&media_gameplay))return;
    unsigned sequence=atomic_fetch_add(&media_skip_sequence,1)+1;
    InterlockedExchange(media_control+1,(LONG)(((sequence&0x3FFFFFFFu)<<1)|(next?1u:0u)));
}
void windows_media_context(int context){
    int gameplay=context==1;
    atomic_store(&media_known_menu,context==2);
    int old=atomic_exchange(&media_gameplay,gameplay);
    if(gameplay&&!old){AcquireSRWLockExclusive(&media_lock);media_until=media_track[0]?GetTickCount64()+6000:0;ReleaseSRWLockExclusive(&media_lock);}
    media_control_update();
}
void windows_media_native_hud(int active){atomic_store(&media_native_hud,active);}
void windows_media_route_commit(const wchar_t *text,int native){
    AcquireSRWLockExclusive(&media_lock);
    /* Metadata can change while the guest prepares a native announcement.
     * Never publish that older route for the worker's newer track. */
    if(!wcscmp(text,media_text)){
        atomic_store(&media_native_hud,native);media_route_ready=1;
    }
    ReleaseSRWLockExclusive(&media_lock);
}
#include "windows_media_volume.h"
static void media_publish(const char *s) {
    wchar_t text[512];int n=MultiByteToWideChar(CP_UTF8,0,s,-1,text,512);
    if(!n)wcscpy(text,L"PC MUSIC: TITLE UNAVAILABLE");
    AcquireSRWLockExclusive(&media_lock);
    if(!wcsncmp(text,L"NOW PLAYING: ",13)){
        if(wcscmp(media_track,text+13)){wcscpy(media_track,text+13);media_until=GetTickCount64()+6000;media_route_ready=0;}
        wcscpy(media_text,text);
    }
    /* Pause/resume and repeated metadata polls do not restart the banner. */
    ReleaseSRWLockExclusive(&media_lock);
}
static DWORD WINAPI media_worker(void *unused) {
    HRESULT co=CoInitializeEx(NULL,COINIT_MULTITHREADED);
    (void)unused;wchar_t exe[2048],script[2048],command[4600],ps[2048];
    GetModuleFileNameW(NULL,exe,2048);wchar_t *slash=wcsrchr(exe,L'\\');if(!slash)return 0;*slash=0;
    swprintf(script,2048,L"%ls\\now_playing.ps1",exe);
    GetSystemDirectoryW(ps,2048);wcscat(ps,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    swprintf(command,4600,L"\"%ls\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%ls\" -OwnerPid %lu -EnableMenuPause",ps,script,GetCurrentProcessId());
    SECURITY_ATTRIBUTES sa={sizeof sa,NULL,TRUE};HANDLE read=NULL,write=NULL;
    if(!CreatePipe(&read,&write,&sa,0))return 0;SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof si;si.dwFlags=STARTF_USESTDHANDLES;
    si.hStdOutput=write;si.hStdError=write;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    if(!CreateProcessW(ps,command,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,exe,&si,&pi)) {
        media_publish("PC MUSIC: MEDIA HELPER UNAVAILABLE");CloseHandle(read);CloseHandle(write);return 0;
    }
    CloseHandle(write);CloseHandle(pi.hThread);
    /* The helper watches our process handle. Let its finally block restore a
     * player it paused, even if the game crashes; do not kill it with a job. */
    char line[4096];DWORD got;int n=0;
    while(ReadFile(read,line+n,1,&got,NULL)&&got){
        if(line[n]=='\n'){line[n]=0;if(n&&line[n-1]=='\r')line[n-1]=0;if(!strncmp(line,"SESSION:",8))MultiByteToWideChar(CP_UTF8,0,line+8,-1,media_source,512);else if(!strncmp(line,"POLICY:",7))fprintf(stderr,"[MEDIA] %s\n",line+7);else if(strncmp(line,"NOW PLAYING:",12)==0||strncmp(line,"PAUSED:",7)==0||strncmp(line,"PC MUSIC:",9)==0){media_publish(line);media_volume_tick();}n=0;}
        else if(n<(int)sizeof(line)-2)n++;
    }
    CloseHandle(read);CloseHandle(pi.hProcess);
    media_publish("PC MUSIC: MEDIA HELPER STOPPED");if(SUCCEEDED(co))CoUninitialize();return 0;
}
void windows_media_set(int enabled) {
    static int started;int before=atomic_exchange(&media_enabled,enabled);
    if(enabled&&!media_map){wchar_t name[80];swprintf(name,80,L"Local\\MC3MediaMode_%lu",GetCurrentProcessId());media_map=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,8,name);if(media_map)media_control=MapViewOfFile(media_map,FILE_MAP_ALL_ACCESS,0,0,8);}
    media_control_update();
    if(enabled&&!before){AcquireSRWLockExclusive(&media_lock);media_until=media_track[0]?GetTickCount64()+6000:0;ReleaseSRWLockExclusive(&media_lock);}
    if(enabled&&!started){HANDLE t=CreateThread(NULL,0,media_worker,NULL,0,NULL);if(t){CloseHandle(t);started=1;}else media_publish("PC MUSIC: CANNOT START MEDIA HELPER");}
}
int windows_media_banner_get(wchar_t *out,int count) {
    if(!atomic_load(&media_enabled)||!atomic_load(&media_gameplay)||count<1)return 0;
    AcquireSRWLockShared(&media_lock);int visible=GetTickCount64()<media_until;
    if(visible){wcsncpy(out,media_text,count-1);out[count-1]=0;}ReleaseSRWLockShared(&media_lock);return visible;
}
int windows_media_get(wchar_t *out,int count){
    if(count<1)return 0;
    AcquireSRWLockShared(&media_lock);
    int visible=atomic_load(&media_enabled)&&atomic_load(&media_gameplay)&&
        media_route_ready&&!atomic_load(&media_native_hud)&&GetTickCount64()<media_until;
    if(visible){wcsncpy(out,media_text,count-1);out[count-1]=0;}
    ReleaseSRWLockShared(&media_lock);return visible;
}
#endif
