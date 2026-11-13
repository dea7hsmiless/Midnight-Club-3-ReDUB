/* Runs only on the media worker's COM apartment. Adjust the selected player's
 * Windows mixer session, never the endpoint/master or the game's own session. */
#define COBJMACROS
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <appmodel.h>
static atomic_int media_volume_request=-1,media_volume_actual=-1;
static wchar_t media_source[512];
static int media_process_matches(DWORD pid)
{
    if(!pid||pid==GetCurrentProcessId()||!media_source[0])return 0;
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!p)return 0;
    wchar_t app[512],path[2048];UINT32 len=512;DWORD size=2048;int match=0;
    if(GetApplicationUserModelId(p,&len,app)==ERROR_SUCCESS&&!_wcsicmp(app,media_source))match=1;
    if(!match&&QueryFullProcessImageNameW(p,0,path,&size)){
        wchar_t *name=wcsrchr(path,L'\\');name=name?name+1:path;
        match=!_wcsicmp(name,media_source);
        wchar_t *dot=wcsrchr(name,L'.');if(dot)*dot=0;
        if(!_wcsicmp(name,media_source))match=1;
    }
    CloseHandle(p);return match;
}
static void media_volume_tick(void)
{
    if(!atomic_load(&media_enabled)||!atomic_load(&media_gameplay)){media_volume_actual=-1;return;}
    IMMDeviceEnumerator *devices=NULL;IMMDeviceCollection *collection=NULL;
    if(FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator,NULL,CLSCTX_INPROC_SERVER,&IID_IMMDeviceEnumerator,(void **)&devices)))return;
    int actual=-1,requested=atomic_load(&media_volume_request);
    if(SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(devices,eRender,DEVICE_STATE_ACTIVE,&collection))){
        UINT count=0;IMMDeviceCollection_GetCount(collection,&count);
        for(UINT d=0;d<count;d++){
            IMMDevice *device=NULL;IAudioSessionManager2 *manager=NULL;IAudioSessionEnumerator *sessions=NULL;
            if(FAILED(IMMDeviceCollection_Item(collection,d,&device)))continue;
            if(SUCCEEDED(IMMDevice_Activate(device,&IID_IAudioSessionManager2,CLSCTX_INPROC_SERVER,NULL,(void **)&manager))&&SUCCEEDED(IAudioSessionManager2_GetSessionEnumerator(manager,&sessions))){
                int n=0;IAudioSessionEnumerator_GetCount(sessions,&n);
                for(int i=0;i<n;i++){
                    IAudioSessionControl *control=NULL;IAudioSessionControl2 *control2=NULL;ISimpleAudioVolume *volume=NULL;
                    if(FAILED(IAudioSessionEnumerator_GetSession(sessions,i,&control)))continue;
                    DWORD pid=0;
                    if(SUCCEEDED(IAudioSessionControl_QueryInterface(control,&IID_IAudioSessionControl2,(void **)&control2))&&SUCCEEDED(IAudioSessionControl2_GetProcessId(control2,&pid))&&media_process_matches(pid)&&SUCCEEDED(IAudioSessionControl_QueryInterface(control,&IID_ISimpleAudioVolume,(void **)&volume))){
                        float v=0;
                        if(requested>=0)ISimpleAudioVolume_SetMasterVolume(volume,requested/100.0f,NULL);
                        if(SUCCEEDED(ISimpleAudioVolume_GetMasterVolume(volume,&v)))actual=(int)(v*100+0.5f);
                    }
                    if(volume)ISimpleAudioVolume_Release(volume);if(control2)IAudioSessionControl2_Release(control2);IAudioSessionControl_Release(control);
                }
                IAudioSessionEnumerator_Release(sessions);
            }
            if(manager)IAudioSessionManager2_Release(manager);IMMDevice_Release(device);
        }
        IMMDeviceCollection_Release(collection);
    }
    IMMDeviceEnumerator_Release(devices);atomic_store(&media_volume_actual,actual);
}
int windows_media_volume_get(void){int n=atomic_load(&media_volume_request);return n>=0?n:atomic_load(&media_volume_actual);}
int windows_media_volume_available(void){return atomic_load(&media_volume_actual)>=0;}
void windows_media_volume_set(int percent){if(percent>=0&&percent<=100)atomic_store(&media_volume_request,percent);}
