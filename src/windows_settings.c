/* Windows window controls; port settings are rendered by the Xbox menu. */
#ifdef _WIN32
#include <windows.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "input/xinput_xbox.h"
#ifndef MC3_APP
#define MC3_APP "mcdub3"
#endif
#ifndef MC3_GAME_TITLE
#define MC3_GAME_TITLE "Midnight Club 3 Remix"
#endif
static SDL_Window *window;
static HWND hwnd;
static int fullscreen_mode=1,resolution_width,resolution_height;
static WNDPROC previous;
static const char *values[]={"1","1.5","2","3","5"};
static atomic_int fullscreen,scale_index=2,swap_interval,fxaa=1,aniso=16,sharp=2,overlay,preset;
static atomic_int frame_rate=30;
static const char *hud_values[]={"1","2","3","5"};
static atomic_int hud_index=1,gamma_index=2,brightness_index=4;
float windows_port_gamma_get(void){return 0.8f+atomic_load(&gamma_index)*0.1f;}
float windows_port_brightness_get(void){return 0.8f+atomic_load(&brightness_index)*0.05f;}
static atomic_int pc_music;
static atomic_int music_hud=1;
int windows_port_music_hud_get(void){return atomic_load(&music_hud);}
static atomic_int music_restore=100;
static atomic_int rumble_strength=100;
static const int rumble_values[]={0,25,50,75,100,125,150,175,200};
void windows_port_music_backup(float volume){int n=(int)(volume*100+0.5f);if(n>=0&&n<=100)music_restore=n;}
float windows_port_music_restore(void){return music_restore/100.0f;}
extern void windows_media_set(int enabled);
extern int windows_media_volume_get(void),windows_media_volume_available(void);
extern void windows_media_volume_set(int);
int windows_port_pc_music_get(void){return atomic_load(&pc_music);}
int windows_port_rumble_get(void){return atomic_load(&rumble_strength);}
float windows_port_sharpen_get(void){return atomic_load(&sharp)*0.25f;}
int windows_port_fxaa_get(void){return atomic_load(&fxaa);}
/* Refresh rate the desktop is running at: the default cadence picks itself
 * from it, so a 120 Hz display gets 120 without anyone editing the env file. */
static int display_hz(void)
{
    DEVMODEA dm;
    memset(&dm, 0, sizeof dm);
    dm.dmSize = (WORD)sizeof dm;
    if (!EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm))
        return 0;
    return (int)dm.dmDisplayFrequency;
}

int windows_port_overlay_get(void){return atomic_load(&overlay);}
int windows_port_vsync_get(void) { return atomic_load(&swap_interval); }
/* Preserve comments and unrelated settings; replace all active copies of a key.
 * Commit by rename so a failed write never truncates the player's settings. */
static int save_settings(const char **keys,const char **values,int count)
{
    char path[2048],tmp[2056],line[4096],*slash;FILE *in,*out;int last='\n';
    GetModuleFileNameA(NULL,path,sizeof path);slash=strrchr(path,'\\');if(!slash)return 0;
    snprintf(slash+1,sizeof path-(slash+1-path),MC3_APP "_env.txt");snprintf(tmp,sizeof tmp,"%s.tmp",path);
    out=fopen(tmp,"wb");if(!out)goto fail;
    in=fopen(path,"rb");
    if(in){while(fgets(line,sizeof line,in)){
        char *p=line;int skip=0;while(*p==' '||*p=='\t')p++;
        for(int i=0;i<count;i++)if(!strncmp(p,keys[i],strlen(keys[i]))&&p[strlen(keys[i])]=='=')skip=1;
        if(!skip){size_t len=strlen(line);fputs(line,out);if(len)last=(unsigned char)line[len-1];}
    }if(ferror(in)){fclose(in);fclose(out);goto fail;}fclose(in);}
    /* Add a separator only for a preserved final line without a newline.
     * Unconditional leading blank lines accumulated on every menu adjustment. */
    if(last!='\n')fputs("\r\n",out);
    for(int i=0;i<count;i++)fprintf(out,"%s=%s\r\n",keys[i],values[i]);
    {int bad=ferror(out);if(fclose(out)!=0||bad)goto fail;}
    if(!MoveFileExA(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))goto fail;
    return 1;
fail:DeleteFileA(tmp);MessageBoxA(hwnd,"Could not save settings beside the game executable.","Port Options",MB_OK|MB_ICONERROR);return 0;
}
static int save_setting(const char *key,const char *value){return save_settings(&key,&value,1);}

static void toggle_fullscreen(void)
{
    int next=fullscreen?0:fullscreen_mode;
    if(next==2){
        SDL_DisplayMode desktop,requested,closest;
        int display=SDL_GetWindowDisplayIndex(window);
        if(SDL_GetDesktopDisplayMode(display,&desktop)!=0)return;
        requested=desktop;
        requested.w=resolution_width?resolution_width:desktop.w;
        requested.h=resolution_height?resolution_height:desktop.h;
        if(!SDL_GetClosestDisplayMode(display,&requested,&closest)||closest.w!=requested.w||closest.h!=requested.h||
           SDL_SetWindowDisplayMode(window,&closest)!=0){
            fprintf(stderr,"[PORT] exclusive resolution %dx%d unavailable; retaining windowed mode\n",requested.w,requested.h);
            MessageBoxA(hwnd,"The selected exclusive fullscreen resolution is unavailable on this display. Choose another resolution in the launcher.","Display resolution",MB_OK|MB_ICONINFORMATION);return;
        }
    }
    if(SDL_SetWindowFullscreen(window,next==2?SDL_WINDOW_FULLSCREEN:next?SDL_WINDOW_FULLSCREEN_DESKTOP:0)!=0) {
        MessageBoxA(hwnd,SDL_GetError(),"Fullscreen",MB_OK|MB_ICONERROR); return;
    }
    fullscreen=next;
    save_setting("MC3_FULLSCREEN",fullscreen==2?"2":fullscreen?"1":"0");
}
static void apply_preset(int n,int defaults)
{
    const char *keys[]={"RECOMP_GL_SCALE","RECOMP_GL_UI_SCALE","RECOMP_GL_FXAA","RECOMP_GL_ANISO","RECOMP_GL_SHARPEN","RECOMP_GL_VSYNC","MC3_GRAPHICS_PRESET","MC3_FPS_OVERLAY","MC3_GAMMA","MC3_BRIGHTNESS"};
    const char *vals[]={n==1?"1.5":n==3?"3":"2",defaults?"2":hud_values[hud_index],"1",n==1?"4":"16",n==1?"0.25":"0.5",n==3?"1":"0",n==1?"1":n==3?"3":"2",defaults?"0":overlay?"1":"0",defaults?"1":NULL,defaults?"1":NULL};
    char g[16],b[16];snprintf(g,sizeof g,"%.1f",windows_port_gamma_get());snprintf(b,sizeof b,"%.2f",windows_port_brightness_get());if(!defaults){vals[8]=g;vals[9]=b;}
    if(!save_settings(keys,vals,10))return;
    scale_index=n==1?1:n==3?3:2;fxaa=1;aniso=n==1?4:16;sharp=n==1?1:2;
    atomic_store(&swap_interval,n==3?1:0);preset=n;if(defaults){overlay=0;hud_index=1;gamma_index=2;brightness_index=4;}
}
static void change_on_main(void *p)
{
    int id=*(int *)p;
    if(id==0){toggle_fullscreen();return;}
    if(id==1){int n=windows_port_vsync_get();n=n==0?1:n==1?-1:0;if(save_setting("RECOMP_GL_VSYNC",n<0?"-1":n?"1":"0"))atomic_store(&swap_interval,n);}
    else if(id==2){int n=(scale_index+1)%5;if(save_setting("RECOMP_GL_SCALE",values[n]))scale_index=n;}
    else if(id==3){int next=fxaa==0?2:fxaa==2?1:fxaa==1?3:0;char value[4];snprintf(value,sizeof value,"%d",next);if(save_setting("RECOMP_GL_FXAA",value))fxaa=next;}
    else if(id==4){int n=aniso==16?4:aniso==4?1:16;if(save_setting("RECOMP_GL_ANISO",n==16?"16":n==4?"4":"1"))aniso=n;}
    else if(id==5){apply_preset(preset>=3?1:preset+1,0);return;}
    else if(id==6){int n=(sharp+1)%5;const char *v[]={"0","0.25","0.5","0.75","1"};if(save_setting("RECOMP_GL_SHARPEN",v[n]))sharp=n;}
    else if(id==7){if(save_setting("MC3_FPS_OVERLAY",overlay?"0":"1"))overlay=!overlay;return;}
    else if(id==8){apply_preset(2,1);return;}
    else if(id==9){int n=!pc_music;char volume[16];snprintf(volume,sizeof volume,"%d",(int)music_restore);const char *keys[]={"MC3_PC_MUSIC","MC3_PC_MUSIC_RESTORE_VOLUME"};const char *vals[]={n?"1":"0",volume};if(save_settings(keys,vals,2)){pc_music=n;windows_media_set(n);}return;}
    else if(id==10){int old=windows_media_volume_get(),n=old<0?50:old>=100?0:((old/10)+1)*10;char v[16];snprintf(v,sizeof v,"%d",n);if(save_setting("MC3_PC_MUSIC_VOLUME",v))windows_media_volume_set(n);return;}
    else if(id==11){int n=!music_hud;if(save_setting("MC3_PC_MUSIC_HUD",n?"1":"0"))music_hud=n;return;}
    else if(id==12){int n=(hud_index+1)%4;if(save_setting("RECOMP_GL_UI_SCALE",hud_values[n]))hud_index=n;return;}
    else if(id==13){int n=(gamma_index+1)%7;char v[16];snprintf(v,sizeof v,"%.1f",0.8+n*0.1);if(save_setting("MC3_GAMMA",v))gamma_index=n;return;}
    else if(id==14){int n=(brightness_index+1)%9;char v[16];snprintf(v,sizeof v,"%.2f",0.8+n*0.05);if(save_setting("MC3_BRIGHTNESS",v))brightness_index=n;return;}
    else if(id==16){
        int cur=atomic_load(&frame_rate), n;
#ifdef MC3_ALLOW_120
        n = cur >= 120 ? 30 : cur >= 60 ? 120 : 60;
#else
        n = cur >= 60 ? 30 : 60;
#endif
        char v[8];snprintf(v,sizeof v,"%d",n);if(save_setting("MC3_FRAME_RATE",v)){atomic_store(&frame_rate,n);fprintf(stderr,"[PORT] frame rate %d saved (restart to apply)\n",n);}return;}
    else if(id==15){int at=4;for(int i=0;i<9;i++)if(rumble_values[i]==rumble_strength){at=i;break;}int n=rumble_values[(at+1)%9];char v[8];snprintf(v,sizeof v,"%d",n);if(save_setting("MC3_RUMBLE_STRENGTH",v)){atomic_store(&rumble_strength,n);xbox_InputSetRumbleStrength(n);}return;}
    if(id>=1&&id<=6){preset=0;save_setting("MC3_GRAPHICS_PRESET","0");}
}

void windows_port_setting_change(int id)
{
    /* Never wait for the window thread from a guest UI callback. The guest
     * may hold its execution lock while Windows/SDL waits for presentation. */
    if(!hwnd||!PostMessageA(hwnd,WM_APP+0x331,(WPARAM)id,0))
        fprintf(stderr,"[PORT] could not queue setting %d (Windows error %lu)\n",id,GetLastError());
}
void windows_port_setting_label(int id,char *out,size_t size)
{
    int n=windows_port_vsync_get();
    if(id==0) snprintf(out,size,"FULLSCREEN: %s",fullscreen?"ON":"OFF");
    if(id==1) snprintf(out,size,"VSYNC: %s",n<0?"ADAPTIVE":n?"ON":"OFF");
    if(id==2) snprintf(out,size,"RENDER SCALE: %sX (RESTART)",values[scale_index]);
    if(id==3){const char *names[]={"OFF","FXAA","FXAA LOW","FXAA HIGH"};snprintf(out,size,"ANTI-ALIASING: %s",names[fxaa>=0&&fxaa<=3?fxaa:1]);}
    if(id==4) snprintf(out,size,"FILTERING: %dX (RESTART)",aniso);
    if(id==5){const char *names[]={"CUSTOM","PERFORMANCE","BALANCED","QUALITY"};snprintf(out,size,"PRESET: %s (RESTART)",names[preset]);}
    if(id==6) snprintf(out,size,"SHARPENING: %d%%",sharp*25);
    if(id==7) snprintf(out,size,"FPS OVERLAY: %s",overlay?"ON":"OFF");
    if(id==8) snprintf(out,size,"RESTORE GRAPHICS DEFAULTS");
    if(id==9) snprintf(out,size,"PC MUSIC (DRIVING): %s",pc_music?"ON":"OFF");
    if(id==10){int n=windows_media_volume_get();if(n<0)snprintf(out,size,"PC MUSIC VOLUME: AUTO");else snprintf(out,size,"PC MUSIC VOLUME: %d%%",n);}
    if(id==11)snprintf(out,size,"SONG DISPLAY: %s",music_hud?"RADIO + FALLBACK":"OVERLAY");
    if(id==12)snprintf(out,size,"HUD TEXTURES: %sX (RESTART)",hud_values[hud_index]);
    if(id==13)snprintf(out,size,"GAMMA: %.1f",windows_port_gamma_get());
    if(id==14)snprintf(out,size,"BRIGHTNESS: %d%%",80+brightness_index*5);
    if(id==15)snprintf(out,size,"CONTROLLER VIBRATION: %d%%",rumble_strength);
    if(id==16)snprintf(out,size,"FRAME RATE: %d FPS (RESTART)",atomic_load(&frame_rate)>=60?60:30);

}
static LRESULT CALLBACK settings_proc(HWND h,UINT msg,WPARAM w,LPARAM l)
{
    if(msg==WM_APP+0x331){int id=(int)w;fprintf(stderr,"[PORT] applying setting %d\n",id);change_on_main(&id);fprintf(stderr,"[PORT] applied setting %d\n",id);return 0;}
    if((msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN) && !(l&(1L<<30)) &&
       (w==VK_F11 || (w==VK_RETURN && (GetKeyState(VK_MENU)&0x8000)))) {
        toggle_fullscreen();return 0;
    }
    return CallWindowProcA(previous,h,msg,w,l);
}
void windows_port_settings_attach(SDL_Window *win)
{
    SDL_SysWMinfo info;const char *v;
    if(window)return;
    SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(win,&info))return;
    window=win;hwnd=info.info.win.window;
    v=getenv("RECOMP_GL_SCALE");
    for(int i=0;i<5;i++)if(v && atof(v)==atof(values[i]))scale_index=i;
    v=getenv("RECOMP_GL_UI_SCALE");for(int i=0;i<4;i++)if(v&&atoi(v)==atoi(hud_values[i]))hud_index=i;
    v=getenv("MC3_GAMMA");gamma_index=v?(int)((atof(v)-0.8)*10+0.5):2;if(gamma_index<0)gamma_index=0;if(gamma_index>6)gamma_index=6;
    v=getenv("MC3_BRIGHTNESS");brightness_index=v?(int)((atof(v)-0.8)*20+0.5):4;if(brightness_index<0)brightness_index=0;if(brightness_index>8)brightness_index=8;
    v=getenv("RECOMP_GL_VSYNC");atomic_store(&swap_interval,v?atoi(v):0);
    v=getenv("RECOMP_GL_FXAA");fxaa=v?atoi(v):1;if(fxaa<0||fxaa>3)fxaa=1;
    v=getenv("RECOMP_GL_ANISO");aniso=v?atoi(v):16;
    v=getenv("RECOMP_GL_SHARPEN");sharp=v?(int)(atof(v)*4+0.5):2;if(sharp<0)sharp=0;if(sharp>4)sharp=4;
    v=getenv("MC3_FPS_OVERLAY");overlay=v&&atoi(v)!=0;
    v=getenv("MC3_GRAPHICS_PRESET");preset=v?atoi(v):0;if(preset<0||preset>3)preset=0;
    v=getenv("MC3_FRAME_RATE");if(!v||!v[0])v=getenv("MC3_FRAME_RATE_TEST_FPS");
    {
        /* Native 120 is the default where the display can show it, 60 otherwise.
         * MC3_ALLOW_120 builds accept all three cadences; other builds clamp. */
        int hz = display_hz();
        int n = (v && atoi(v) > 0) ? atoi(v) : (hz >= 100 ? 120 : 60);
#ifdef MC3_ALLOW_120
        if (n != 30 && n != 60 && n != 120) n = 60;
#else
        n = n >= 60 ? 60 : 30;
#endif
        atomic_store(&frame_rate, n);
        fprintf(stderr, "[PORT] frame rate %d (display %d Hz%s)\n", n, hz,
                v ? ", from settings" : ", chosen from refresh");
    }
    v=getenv("MC3_PC_MUSIC");pc_music=v&&atoi(v)!=0;windows_media_set(pc_music);
    v=getenv("MC3_PC_MUSIC_RESTORE_VOLUME");if(v){int n=atoi(v);if(n>=0&&n<=100)music_restore=n;}
    v=getenv("MC3_PC_MUSIC_VOLUME");if(v){int n=atoi(v);if(n>=0&&n<=100)windows_media_volume_set(n);}
    v=getenv("MC3_PC_MUSIC_HUD");music_hud=!v||atoi(v)!=0;
    v=getenv("MC3_RUMBLE_STRENGTH");{int n=v?atoi(v):100;if(n<0)n=0;if(n>200)n=200;atomic_store(&rumble_strength,n);xbox_InputSetRumbleStrength(n);}
    previous=(WNDPROC)SetWindowLongPtrA(hwnd,GWLP_WNDPROC,(LONG_PTR)settings_proc);
    SDL_SetWindowTitle(win,MC3_GAME_TITLE);
    v=getenv("MC3_RESOLUTION");
    if(v&&strcmp(v,"auto")){
        unsigned w=0,h=0;char trailing;
        if(strlen(v)<20&&sscanf(v,"%ux%u%c",&w,&h,&trailing)==2&&w>=640&&w<=7680&&h>=360&&h<=4320){
            resolution_width=(int)w;resolution_height=(int)h;
            SDL_SetWindowSize(win,resolution_width,resolution_height);
            fprintf(stderr,"[PORT] requested window/exclusive resolution %ux%u\n",w,h);
        }else fprintf(stderr,"[PORT] ignoring invalid MC3_RESOLUTION: %s\n",v);
    }
    v=getenv("MC3_FULLSCREEN");fullscreen_mode=v&&atoi(v)==2?2:1;
    if(v&&atoi(v))toggle_fullscreen();
}
#endif
