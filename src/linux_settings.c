/* Linux Port Options backend. The Xbox menu and settings IDs are shared with
 * Windows; Linux media controls use MPRIS through linux_media.c. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include <limits.h>
#include "input/xinput_xbox.h"

#ifndef MC3_APP
#define MC3_APP "mcdub3"
#endif

static SDL_Window *s_window;
static atomic_int s_fullscreen, s_scale=2, s_vsync, s_fxaa=1, s_aniso=16;
static atomic_int s_sharpen=2, s_overlay, s_preset, s_hud=1;
static atomic_int s_gamma=2, s_brightness=4, s_rumble=100, s_music_hud=1;
static atomic_int s_pc_music, s_music_restore=100;
static atomic_int s_frame_rate=30;
static int save_setting(const char *k,const char *v);
extern void windows_media_set(int enabled);
extern int windows_media_volume_get(void);
extern void windows_media_volume_set(int percent);
extern int windows_media_available(void);
static const char *s_scales[]={"1","1.5","2","3","5"};
static const char *s_hud_scales[]={"1","2","3","5"};
static const int s_rumble_values[]={0,25,50,75,100,125,150,175,200};

float windows_port_gamma_get(void){return .8f+atomic_load(&s_gamma)*.1f;}
float windows_port_brightness_get(void){return .8f+atomic_load(&s_brightness)*.05f;}
int windows_port_music_hud_get(void){return atomic_load(&s_music_hud);}
void windows_port_music_backup(float v){int n=(int)(v*100.0f+0.5f);if(n>=0&&n<=100){atomic_store(&s_music_restore,n);char value[16];snprintf(value,sizeof value,"%d",n);save_setting("MC3_PC_MUSIC_RESTORE_VOLUME",value);}}
float windows_port_music_restore(void){return atomic_load(&s_music_restore)/100.0f;}
int windows_port_pc_music_get(void){return atomic_load(&s_pc_music);}
int windows_port_rumble_get(void){return atomic_load(&s_rumble);}
float windows_port_sharpen_get(void){return atomic_load(&s_sharpen)*.25f;}
int windows_port_fxaa_get(void){return atomic_load(&s_fxaa);}
int windows_port_overlay_get(void){return atomic_load(&s_overlay);}
int windows_port_vsync_get(void){return atomic_load(&s_vsync);}

static void settings_path(char out[PATH_MAX])
{
    const char *configured = getenv("MC3_SETTINGS_FILE");
    if (configured && configured[0]) {
        snprintf(out, PATH_MAX, "%s", configured);
        return;
    }
    char *base=SDL_GetBasePath();
    if(base){snprintf(out,PATH_MAX,"%s%s_env.txt",base,MC3_APP);SDL_free(base);}
    else snprintf(out,PATH_MAX,"%s_env.txt",MC3_APP);
}

/* Read the same NAME=value file Windows uses. This is called before the GL
 * context and renderer caches are created. */
static void load_settings(void)
{
    char path[PATH_MAX],line[2048];FILE *f;
    settings_path(path);f=fopen(path,"rb");if(!f)return;
    while(fgets(line,sizeof line,f)){
        char *p=line,*eq,*end;
        while(*p==' '||*p=='\t')p++;
        if(!*p||*p=='#'||*p==';'||!(eq=strchr(p,'=')))continue;
        *eq++=0;end=eq+strcspn(eq,"\r\n");*end=0;
        if(*p&&*eq)setenv(p,eq,1);
    }
    fclose(f);
}

static int save_settings(const char **keys,const char **values,int count)
{
    char path[PATH_MAX],tmp[PATH_MAX+8],line[4096];FILE *in,*out;int last='\n';
    settings_path(path);snprintf(tmp,sizeof tmp,"%s.tmp",path);
    out=fopen(tmp,"wb");if(!out)return 0;
    in=fopen(path,"rb");
    if(in){while(fgets(line,sizeof line,in)){
        char *p=line;int skip=0;while(*p==' '||*p=='\t')p++;
        for(int i=0;i<count;i++)if(!strncmp(p,keys[i],strlen(keys[i]))&&p[strlen(keys[i])]=='=')skip=1;
        if(!skip){size_t n=strlen(line);fputs(line,out);if(n)last=(unsigned char)line[n-1];}
    }if(ferror(in)){fclose(in);fclose(out);unlink(tmp);return 0;}fclose(in);}
    if(last!='\n')fputs("\n",out);
    for(int i=0;i<count;i++)fprintf(out,"%s=%s\n",keys[i],values[i]);
    {int bad=ferror(out);if(fclose(out)!=0)bad=1;if(bad){unlink(tmp);return 0;}}
    if(rename(tmp,path)!=0){unlink(tmp);return 0;}
    return 1;
}
static int save_setting(const char *k,const char *v){return save_settings(&k,&v,1);}

static void set_vsync(int n)
{
    if(s_window&&SDL_GL_GetCurrentContext()){
        int applied=n;
        if(SDL_GL_SetSwapInterval(n)!=0){applied=n<0?1:0;SDL_GL_SetSwapInterval(applied);}
        fprintf(stderr,"[PORT] VSync requested %d, applied %d (%s)\n",n,applied,SDL_GetError());
    }
}
static void apply_preset(int n,int defaults)
{
    const char *keys[]={"RECOMP_GL_SCALE","RECOMP_GL_UI_SCALE","RECOMP_GL_FXAA","RECOMP_GL_ANISO","RECOMP_GL_SHARPEN","RECOMP_GL_VSYNC","MC3_GRAPHICS_PRESET","MC3_FPS_OVERLAY","MC3_GAMMA","MC3_BRIGHTNESS"};
    char gamma[16],brightness[16];
    snprintf(gamma,sizeof gamma,"%.1f",defaults?1.0:windows_port_gamma_get());
    snprintf(brightness,sizeof brightness,"%.2f",defaults?1.0:windows_port_brightness_get());
    const char *values[]={n==1?"1.5":n==3?"3":"2",defaults?"2":s_hud_scales[s_hud],"1",n==1?"4":"16",n==1?"0.25":"0.5",n==3?"1":"0",n==1?"1":n==3?"3":"2",defaults?"0":s_overlay?"1":"0",gamma,brightness};
    if(!save_settings(keys,values,10))return;
    atomic_store(&s_scale,n==1?1:n==3?3:2);atomic_store(&s_fxaa,1);atomic_store(&s_aniso,n==1?4:16);atomic_store(&s_sharpen,n==1?1:2);atomic_store(&s_vsync,n==3?1:0);atomic_store(&s_preset,n);
    if(defaults){atomic_store(&s_overlay,0);atomic_store(&s_hud,1);atomic_store(&s_gamma,2);atomic_store(&s_brightness,4);}
    set_vsync(n==3?1:0);
}

void windows_port_setting_change(int id)
{
    char value[32];
    if(!s_window)return;
    switch(id){
    case 0:{int next=!(SDL_GetWindowFlags(s_window)&SDL_WINDOW_FULLSCREEN);SDL_ClearError();int rc=SDL_SetWindowFullscreen(s_window,next?SDL_WINDOW_FULLSCREEN_DESKTOP:0);if(rc==0){SDL_RaiseWindow(s_window);SDL_PumpEvents();int actual=!!(SDL_GetWindowFlags(s_window)&SDL_WINDOW_FULLSCREEN);if(save_setting("MC3_FULLSCREEN",actual?"1":"0"))s_fullscreen=actual;fprintf(stderr,"[PORT] Fullscreen %s (%s)\n",actual?"enabled":"disabled",SDL_GetError());}else fprintf(stderr,"[PORT] Fullscreen request failed: %s\n",SDL_GetError());break;}
    case 1:{int n=windows_port_vsync_get();n=n==0?1:n==1?-1:0;snprintf(value,sizeof value,"%d",n);if(save_setting("RECOMP_GL_VSYNC",value)){atomic_store(&s_vsync,n);set_vsync(n);}break;}
    case 2:{int n=(s_scale+1)%5;if(save_setting("RECOMP_GL_SCALE",s_scales[n]))s_scale=n;break;}
    case 3:{int n=s_fxaa==0?2:s_fxaa==2?1:s_fxaa==1?3:0;snprintf(value,sizeof value,"%d",n);if(save_setting("RECOMP_GL_FXAA",value))s_fxaa=n;break;}
    case 4:{int n=s_aniso==16?4:s_aniso==4?1:16;snprintf(value,sizeof value,"%d",n);if(save_setting("RECOMP_GL_ANISO",value))s_aniso=n;break;}
    case 5:apply_preset(s_preset>=3?1:s_preset+1,0);break;
    case 6:{int n=(s_sharpen+1)%5;const char *v[]={"0","0.25","0.5","0.75","1"};if(save_setting("RECOMP_GL_SHARPEN",v[n]))s_sharpen=n;break;}
    case 7:if(save_setting("MC3_FPS_OVERLAY",s_overlay?"0":"1"))s_overlay=!s_overlay;break;
    case 8:apply_preset(2,1);break;
    case 9:{if(!windows_media_available())break;int n=!s_pc_music;char music[16],restore[16];snprintf(music,sizeof music,"%d",n);snprintf(restore,sizeof restore,"%d",s_music_restore);const char *keys[]={"MC3_PC_MUSIC","MC3_PC_MUSIC_RESTORE_VOLUME"};const char *values[]={music,restore};if(save_settings(keys,values,2)){atomic_store(&s_pc_music,n);windows_media_set(n);}break;}
    case 10:{int old=windows_media_volume_get(),n=old<0?50:old>=100?0:((old/10)+1)*10;snprintf(value,sizeof value,"%d",n);if(save_setting("MC3_PC_MUSIC_VOLUME",value))windows_media_volume_set(n);break;}
    case 11:if(save_setting("MC3_PC_MUSIC_HUD",s_music_hud?"0":"1"))s_music_hud=!s_music_hud;break;
    case 12:{int n=(s_hud+1)%4;if(save_setting("RECOMP_GL_UI_SCALE",s_hud_scales[n]))s_hud=n;break;}
    case 13:{int n=(s_gamma+1)%7;snprintf(value,sizeof value,"%.1f",.8+n*.1);if(save_setting("MC3_GAMMA",value))s_gamma=n;break;}
    case 14:{int n=(s_brightness+1)%9;snprintf(value,sizeof value,"%.2f",.8+n*.05);if(save_setting("MC3_BRIGHTNESS",value))s_brightness=n;break;}
    case 16:{int n=atomic_load(&s_frame_rate)>=60?30:60;snprintf(value,sizeof value,"%d",n);if(save_setting("MC3_FRAME_RATE",value)){atomic_store(&s_frame_rate,n);fprintf(stderr,"[PORT] frame rate %d saved (restart to apply)\n",n);}break;}
    case 15:{int at=4;for(int i=0;i<9;i++)if(s_rumble_values[i]==s_rumble){at=i;break;}int n=s_rumble_values[(at+1)%9];snprintf(value,sizeof value,"%d",n);if(save_setting("MC3_RUMBLE_STRENGTH",value)){s_rumble=n;xbox_InputSetRumbleStrength(n);}break;}
    }
    if(id>=1&&id<=6){atomic_store(&s_preset,0);save_setting("MC3_GRAPHICS_PRESET","0");}
}

void windows_port_setting_label(int id,char *out,size_t size)
{
    int n=windows_port_vsync_get();
    if(id==0)snprintf(out,size,"FULLSCREEN: %s",s_fullscreen?"ON":"OFF");
    if(id==1)snprintf(out,size,"VSYNC: %s",n<0?"ADAPTIVE":n?"ON":"OFF");
    if(id==2)snprintf(out,size,"RENDER SCALE: %sX (RESTART)",s_scales[s_scale]);
    if(id==3){const char *names[]={"OFF","FXAA","FXAA LOW","FXAA HIGH"};snprintf(out,size,"ANTI-ALIASING: %s",names[s_fxaa>=0&&s_fxaa<=3?s_fxaa:1]);}
    if(id==4)snprintf(out,size,"FILTERING: %dX (RESTART)",s_aniso);
    if(id==5){const char *names[]={"CUSTOM","PERFORMANCE","BALANCED","QUALITY"};snprintf(out,size,"PRESET: %s (RESTART)",names[s_preset]);}
    if(id==6)snprintf(out,size,"SHARPENING: %d%%",s_sharpen*25);
    if(id==7)snprintf(out,size,"FPS OVERLAY: %s",s_overlay?"ON":"OFF");
    if(id==8)snprintf(out,size,"RESTORE GRAPHICS DEFAULTS");
    if(id==9)snprintf(out,size,"PC MUSIC (DRIVING): %s",windows_media_available()?(s_pc_music?"ON":"OFF"):"MPRIS UNAVAILABLE");
    if(id==10){int v=windows_media_volume_get();if(!windows_media_available())snprintf(out,size,"PC MUSIC VOLUME: UNAVAILABLE");else if(v<0)snprintf(out,size,"PC MUSIC VOLUME: AUTO");else snprintf(out,size,"PC MUSIC VOLUME: %d%%",v);}
    if(id==11)snprintf(out,size,"SONG DISPLAY: %s",s_music_hud?"RADIO + FALLBACK":"OVERLAY");
    if(id==12)snprintf(out,size,"HUD TEXTURES: %sX (RESTART)",s_hud_scales[s_hud]);
    if(id==13)snprintf(out,size,"GAMMA: %.1f",windows_port_gamma_get());
    if(id==14)snprintf(out,size,"BRIGHTNESS: %d%%",80+s_brightness*5);
    if(id==15)snprintf(out,size,"CONTROLLER VIBRATION: %d%%",s_rumble);
    if(id==16)snprintf(out,size,"FRAME RATE: %d FPS (RESTART)",atomic_load(&s_frame_rate)>=60?60:30);
}

void windows_port_settings_attach(SDL_Window *win)
{
    const char *v;load_settings();s_window=win;
    v=getenv("RECOMP_GL_SCALE");for(int i=0;i<5;i++)if(v&&atof(v)==atof(s_scales[i]))s_scale=i;
    v=getenv("RECOMP_GL_UI_SCALE");for(int i=0;i<4;i++)if(v&&atoi(v)==atoi(s_hud_scales[i]))s_hud=i;
    v=getenv("RECOMP_GL_VSYNC");s_vsync=v?atoi(v):0;
    v=getenv("RECOMP_GL_FXAA");s_fxaa=v?atoi(v):1;if(s_fxaa<0||s_fxaa>3)s_fxaa=1;
    v=getenv("RECOMP_GL_ANISO");s_aniso=v?atoi(v):16;
    v=getenv("RECOMP_GL_SHARPEN");s_sharpen=v?(int)(atof(v)*4+.5):2;if(s_sharpen<0)s_sharpen=0;if(s_sharpen>4)s_sharpen=4;
    v=getenv("MC3_FPS_OVERLAY");s_overlay=v&&atoi(v)!=0;
    v=getenv("MC3_GRAPHICS_PRESET");s_preset=v?atoi(v):0;if(s_preset<0||s_preset>3)s_preset=0;
    v=getenv("MC3_FRAME_RATE");if(!v||!v[0])v=getenv("MC3_FRAME_RATE_TEST_FPS");
    atomic_store(&s_frame_rate,v&&atoi(v)>=60?60:30);
    v=getenv("MC3_GAMMA");s_gamma=v?(int)((atof(v)-.8)*10+.5):2;if(s_gamma<0)s_gamma=0;if(s_gamma>6)s_gamma=6;
    v=getenv("MC3_BRIGHTNESS");s_brightness=v?(int)((atof(v)-.8)*20+.5):4;if(s_brightness<0)s_brightness=0;if(s_brightness>8)s_brightness=8;
    v=getenv("MC3_PC_MUSIC_HUD");s_music_hud=!v||atoi(v)!=0;
    v=getenv("MC3_PC_MUSIC_RESTORE_VOLUME");if(v){int n=atoi(v);if(n>=0&&n<=100)s_music_restore=n;}
    v=getenv("MC3_PC_MUSIC_VOLUME");if(v){int n=atoi(v);if(n>=0&&n<=100)windows_media_volume_set(n);}
    v=getenv("MC3_PC_MUSIC");s_pc_music=windows_media_available()&&v&&atoi(v)!=0;windows_media_set(s_pc_music);
    v=getenv("MC3_RUMBLE_STRENGTH");int r=v?atoi(v):100;if(r<0)r=0;if(r>200)r=200;s_rumble=r;xbox_InputSetRumbleStrength(r);
    SDL_SetWindowTitle(win,MC3_GAME_TITLE);
    v=getenv("MC3_RESOLUTION");if(v&&strcmp(v,"auto")){unsigned w=0,h=0;char t;if(sscanf(v,"%ux%u%c",&w,&h,&t)==2&&w>=640&&w<=7680&&h>=360&&h<=4320)SDL_SetWindowSize(win,(int)w,(int)h);}
    v=getenv("MC3_FULLSCREEN");if(v&&atoi(v)){s_fullscreen=1;if(SDL_SetWindowFullscreen(win,SDL_WINDOW_FULLSCREEN_DESKTOP)!=0)fprintf(stderr,"[PORT] Startup fullscreen request failed: %s\n",SDL_GetError());}
    else s_fullscreen=0;
}
