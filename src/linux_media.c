/* MPRIS media bridge for Linux desktops. Media continues through the normal
 * desktop mixer; this bridge supplies metadata, transport and player volume. */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <locale.h>
#include <iconv.h>
#ifdef MC3_HAS_MPRIS
#include <dbus/dbus.h>
#endif

static atomic_int media_enabled, media_gameplay, media_known_menu, media_native_hud;
static atomic_int media_volume_request=-1, media_volume_actual=-1;
static atomic_uint media_skip_sequence;
static atomic_int media_skip_next;
static pthread_mutex_t media_lock=PTHREAD_MUTEX_INITIALIZER;
static wchar_t media_text[512]=L"PC MUSIC: NO ACTIVE MEDIA";
static wchar_t media_track[512];
static uint64_t media_until;
static pthread_t media_thread;
static int media_started;
static uint64_t now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;}
static void publish_utf8(const char *text)
{
    wchar_t w[512];char ascii[2048],*in=(char *)text,*out=ascii;size_t left_in=strlen(text),left_out=sizeof ascii-1;iconv_t cv=iconv_open("ASCII//TRANSLIT","UTF-8");
    if(cv!=(iconv_t)-1){iconv(cv,&in,&left_in,&out,&left_out);iconv_close(cv);}
    else{for(const unsigned char *p=(const unsigned char *)text;*p&&left_out;p++)if(*p<128){*out++=(char)*p;left_out--;}}
    *out=0;
    size_t n=mbstowcs(w,ascii,511);if(n==(size_t)-1){n=0;for(const unsigned char *p=(const unsigned char *)text;*p&&n<511;p++)if(*p<128)w[n++]=*p;}
    w[n]=0;pthread_mutex_lock(&media_lock);
    const wchar_t *track=!wcsncmp(w,L"NOW PLAYING: ",13)?w+13:NULL;
    if(track&&wcscmp(media_track,track)){wcsncpy(media_track,track,511);media_track[511]=0;media_until=now_ms()+6000;atomic_store(&media_native_hud,0);}
    wcsncpy(media_text,w,511);media_text[511]=0;pthread_mutex_unlock(&media_lock);
}

#ifdef MC3_HAS_MPRIS
static DBusConnection *bus;
static char selected_player[256],paused_player[256];
static unsigned consumed_skip;
static void dbus_err_free(DBusError *e){if(dbus_error_is_set(e))dbus_error_free(e);}
static DBusMessage *call_method(const char *dest,const char *iface,const char *method)
{
    DBusMessage *m=dbus_message_new_method_call(dest,"/org/mpris/MediaPlayer2",iface,method);if(!m)return NULL;
    DBusError e;dbus_error_init(&e);DBusMessage *r=dbus_connection_send_with_reply_and_block(bus,m,500,&e);dbus_message_unref(m);dbus_err_free(&e);return r;
}
static DBusMessage *get_property(const char *dest,const char *name)
{
    DBusMessage *m=dbus_message_new_method_call(dest,"/org/mpris/MediaPlayer2","org.freedesktop.DBus.Properties","Get");if(!m)return NULL;
    const char *iface="org.mpris.MediaPlayer2.Player";dbus_message_append_args(m,DBUS_TYPE_STRING,&iface,DBUS_TYPE_STRING,&name,DBUS_TYPE_INVALID);
    DBusError e;dbus_error_init(&e);DBusMessage *r=dbus_connection_send_with_reply_and_block(bus,m,500,&e);dbus_message_unref(m);dbus_err_free(&e);return r;
}
static int property_string(const char *dest,const char *name,char *out,size_t cap)
{
    DBusMessage *r=get_property(dest,name);if(!r)return 0;DBusMessageIter i,v;int ok=0;
    if(dbus_message_iter_init(r,&i)&&dbus_message_iter_get_arg_type(&i)==DBUS_TYPE_VARIANT){dbus_message_iter_recurse(&i,&v);if(dbus_message_iter_get_arg_type(&v)==DBUS_TYPE_STRING){const char *s;dbus_message_iter_get_basic(&v,&s);snprintf(out,cap,"%s",s);ok=1;}}
    dbus_message_unref(r);return ok;
}
static int property_bool(const char *dest,const char *name)
{
    DBusMessage *r=get_property(dest,name);if(!r)return 0;DBusMessageIter i,v;dbus_bool_t b=0;
    if(dbus_message_iter_init(r,&i)&&dbus_message_iter_get_arg_type(&i)==DBUS_TYPE_VARIANT){dbus_message_iter_recurse(&i,&v);if(dbus_message_iter_get_arg_type(&v)==DBUS_TYPE_BOOLEAN)dbus_message_iter_get_basic(&v,&b);}
    dbus_message_unref(r);return b!=0;
}
static int property_volume(const char *dest,double *volume)
{
    DBusMessage *r=get_property(dest,"Volume");if(!r)return 0;DBusMessageIter i,v;int ok=0;double d=0;
    if(dbus_message_iter_init(r,&i)&&dbus_message_iter_get_arg_type(&i)==DBUS_TYPE_VARIANT){dbus_message_iter_recurse(&i,&v);if(dbus_message_iter_get_arg_type(&v)==DBUS_TYPE_DOUBLE){dbus_message_iter_get_basic(&v,&d);ok=1;}}
    dbus_message_unref(r);if(ok)*volume=d;return ok;
}
static int set_volume(const char *dest,double volume)
{
    DBusMessage *m=dbus_message_new_method_call(dest,"/org/mpris/MediaPlayer2","org.freedesktop.DBus.Properties","Set");if(!m)return 0;
    const char *iface="org.mpris.MediaPlayer2.Player",*prop="Volume";DBusMessageIter i,v;dbus_message_iter_init_append(m,&i);dbus_message_iter_append_basic(&i,DBUS_TYPE_STRING,&iface);dbus_message_iter_append_basic(&i,DBUS_TYPE_STRING,&prop);dbus_message_iter_open_container(&i,DBUS_TYPE_VARIANT,"d",&v);dbus_message_iter_append_basic(&v,DBUS_TYPE_DOUBLE,&volume);dbus_message_iter_close_container(&i,&v);
    DBusError e;dbus_error_init(&e);DBusMessage *r=dbus_connection_send_with_reply_and_block(bus,m,500,&e);dbus_message_unref(m);dbus_err_free(&e);if(r)dbus_message_unref(r);return r!=NULL;
}
static int command(const char *dest,const char *method)
{DBusMessage *r=call_method(dest,"org.mpris.MediaPlayer2.Player",method);if(!r)return 0;dbus_message_unref(r);return 1;}
static int list_players(char out[][256],int cap)
{
    DBusMessage *m=dbus_message_new_method_call("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","ListNames");if(!m)return 0;
    DBusError e;dbus_error_init(&e);DBusMessage *r=dbus_connection_send_with_reply_and_block(bus,m,500,&e);dbus_message_unref(m);dbus_err_free(&e);if(!r)return 0;
    int n=0;DBusMessageIter i,a;const char *s;
    if(dbus_message_iter_init(r,&i)&&dbus_message_iter_get_arg_type(&i)==DBUS_TYPE_ARRAY){dbus_message_iter_recurse(&i,&a);while(dbus_message_iter_get_arg_type(&a)==DBUS_TYPE_STRING){dbus_message_iter_get_basic(&a,&s);if(!strncmp(s,"org.mpris.MediaPlayer2.",23)&&n<cap){snprintf(out[n++],256,"%s",s);}dbus_message_iter_next(&a);}}
    dbus_message_unref(r);return n;
}
static int metadata(const char *dest,char *title,size_t tc,char *artist,size_t ac)
{
    DBusMessage *r=get_property(dest,"Metadata");if(!r)return 0;DBusMessageIter i,v,a,e,k,x;int have=0;
    if(dbus_message_iter_init(r,&i)&&dbus_message_iter_get_arg_type(&i)==DBUS_TYPE_VARIANT){dbus_message_iter_recurse(&i,&v);if(dbus_message_iter_get_arg_type(&v)==DBUS_TYPE_ARRAY){dbus_message_iter_recurse(&v,&a);while(dbus_message_iter_get_arg_type(&a)==DBUS_TYPE_DICT_ENTRY){dbus_message_iter_recurse(&a,&e);if(dbus_message_iter_get_arg_type(&e)!=DBUS_TYPE_STRING){dbus_message_iter_next(&a);continue;}const char *key;dbus_message_iter_get_basic(&e,&key);dbus_message_iter_next(&e);if(dbus_message_iter_get_arg_type(&e)!=DBUS_TYPE_VARIANT){dbus_message_iter_next(&a);continue;}dbus_message_iter_recurse(&e,&x);
        if(!strcmp(key,"xesam:title")&&dbus_message_iter_get_arg_type(&x)==DBUS_TYPE_STRING){const char *s;dbus_message_iter_get_basic(&x,&s);snprintf(title,tc,"%s",s);have=1;}
        else if(!strcmp(key,"xesam:artist")&&dbus_message_iter_get_arg_type(&x)==DBUS_TYPE_ARRAY){DBusMessageIter ar;dbus_message_iter_recurse(&x,&ar);if(dbus_message_iter_get_arg_type(&ar)==DBUS_TYPE_STRING){const char *s;dbus_message_iter_get_basic(&ar,&s);snprintf(artist,ac,"%s",s);}}
        dbus_message_iter_next(&a);}}}
    dbus_message_unref(r);return have;
}
static int player_status(const char *p,char *out,size_t cap){return property_string(p,"PlaybackStatus",out,cap);}
static void player_menu_policy(const char *active,const char *status)
{
    if(!atomic_load(&media_enabled))return;
    if(atomic_load(&media_known_menu)&&!paused_player[0]&&active[0]&&!strcmp(status,"Playing"))if(command(active,"Pause")){snprintf(paused_player,sizeof paused_player,"%s",active);fprintf(stderr,"[MEDIA] paused MPRIS player for game menu: %s\n",active);}
    if(!atomic_load(&media_known_menu)&&paused_player[0]){
        char other[16]="";int n=0;char names[32][256];int count=list_players(names,32);
        for(int i=0;i<count;i++){if(!strcmp(names[i],paused_player))continue;if(player_status(names[i],other,sizeof other)&&!strcmp(other,"Playing")){n=1;break;}}
        char own[16]="";if(!n&&player_status(paused_player,own,sizeof own)&&!strcmp(own,"Paused"))command(paused_player,"Play");
        fprintf(stderr,"[MEDIA] %s paused MPRIS player after game menu\n",n?"did not resume another player was active":"resumed");paused_player[0]=0;
    }
}
static void *media_worker(void *unused)
{
    (void)unused;setlocale(LC_CTYPE,"");dbus_threads_init_default();DBusError e;dbus_error_init(&e);bus=dbus_bus_get_private(DBUS_BUS_SESSION,&e);
    if(bus)dbus_connection_set_exit_on_disconnect(bus,FALSE);
    while(!bus){publish_utf8("PC MUSIC: WAITING FOR SESSION BUS");dbus_err_free(&e);struct timespec retry={1,0};nanosleep(&retry,NULL);dbus_error_init(&e);bus=dbus_bus_get_private(DBUS_BUS_SESSION,&e);if(bus)dbus_connection_set_exit_on_disconnect(bus,FALSE);}
    char names[32][256],status[24],title[512],artist[512],line[1200],active[256];unsigned handled=0;int last_enabled=0;uint64_t next_poll=0;
    while(1){int enabled=atomic_load(&media_enabled);unsigned skip=atomic_load(&media_skip_sequence);
        if(enabled&&skip!=handled){handled=skip;int next=atomic_load(&media_skip_next);const char *p=selected_player[0]?selected_player:paused_player;if(p[0]){const char *m=next?"Next":"Previous";const char *can=next?"CanGoNext":"CanGoPrevious";if(property_bool(p,can))command(p,m);}}
        if(enabled&&now_ms()>=next_poll){next_poll=now_ms()+250;int count=list_players(names,32),found=0;active[0]=0;status[0]=0;
            for(int i=0;i<count;i++)if(player_status(names[i],status,sizeof status)&&!strcmp(status,"Playing")){memcpy(active,names[i],sizeof active);active[sizeof active-1]=0;found=1;break;}
            if(!found&&selected_player[0]){for(int i=0;i<count;i++)if(!strcmp(names[i],selected_player)&&player_status(names[i],status,sizeof status)){memcpy(active,names[i],sizeof active);active[sizeof active-1]=0;found=1;break;}}
            if(!found)for(int i=0;i<count;i++)if(player_status(names[i],status,sizeof status)){memcpy(active,names[i],sizeof active);active[sizeof active-1]=0;found=1;break;}
            player_menu_policy(active,status);
            if(found){snprintf(selected_player,sizeof selected_player,"%s",active);title[0]=artist[0]=0;if(metadata(active,title,sizeof title,artist,sizeof artist)&&title[0]){snprintf(line,sizeof line,"%s: %s%s%s",!strcmp(status,"Playing")?"NOW PLAYING":"PAUSED",title,artist[0]?" - ":"",artist);publish_utf8(line);}else publish_utf8("PC MUSIC: TITLE UNAVAILABLE");
                double volume;if(property_volume(active,&volume)){int actual=(int)(volume*100.0+0.5);if(actual<0)actual=0;if(actual>100)actual=100;atomic_store(&media_volume_actual,actual);int request=atomic_load(&media_volume_request);if(enabled&&request>=0&&actual!=request)set_volume(active,request/100.0);}
            }else{selected_player[0]=0;atomic_store(&media_volume_actual,-1);publish_utf8("PC MUSIC: NO ACTIVE MEDIA");}
        }
        if(!enabled&&last_enabled){if(paused_player[0]){char own[16]="";if(player_status(paused_player,own,sizeof own)&&!strcmp(own,"Paused"))command(paused_player,"Play");paused_player[0]=0;}selected_player[0]=0;}
        last_enabled=enabled;struct timespec ts={0,100000000};nanosleep(&ts,NULL);
    }
    return NULL;
}
#else
static void *media_worker(void *unused){(void)unused;publish_utf8("PC MUSIC: MPRIS SUPPORT NOT BUILT");return NULL;}
#endif

void windows_media_set(int enabled)
{
    atomic_store(&media_enabled,enabled!=0);
    if(enabled&&!media_started){if(!pthread_create(&media_thread,NULL,media_worker,NULL)){pthread_detach(media_thread);media_started=1;}else publish_utf8("PC MUSIC: CANNOT START MEDIA HELPER");}
    if(enabled){pthread_mutex_lock(&media_lock);if(media_track[0])media_until=now_ms()+6000;pthread_mutex_unlock(&media_lock);}
}
void windows_media_context(int context)
{
    atomic_store(&media_known_menu,context==2);atomic_store(&media_gameplay,context==1);
    if(context==1){pthread_mutex_lock(&media_lock);if(media_track[0])media_until=now_ms()+6000;pthread_mutex_unlock(&media_lock);}
}
int windows_media_gameplay_get(void){return atomic_load(&media_gameplay);}
void windows_media_native_hud(int active){atomic_store(&media_native_hud,active!=0);}
void windows_media_route_commit(const wchar_t *text,int native){(void)text;atomic_store(&media_native_hud,native!=0);}
void windows_media_skip(int next){atomic_store(&media_skip_next,next!=0);atomic_fetch_add(&media_skip_sequence,1);}
int windows_media_banner_get(wchar_t *out,int count)
{
    if(!atomic_load(&media_enabled)||!atomic_load(&media_gameplay)||count<1)return 0;
    pthread_mutex_lock(&media_lock);int visible=now_ms()<media_until;if(visible){wcsncpy(out,media_text,(size_t)count-1);out[count-1]=0;}pthread_mutex_unlock(&media_lock);return visible;
}
int windows_media_get(wchar_t *out,int count)
{
    if(count<1)return 0;pthread_mutex_lock(&media_lock);int visible=atomic_load(&media_enabled)&&atomic_load(&media_gameplay)&&!atomic_load(&media_native_hud)&&now_ms()<media_until;
    if(visible){wcsncpy(out,media_text,(size_t)count-1);out[count-1]=0;}pthread_mutex_unlock(&media_lock);return visible;
}
int windows_media_volume_get(void){int n=atomic_load(&media_volume_request);return n>=0?n:atomic_load(&media_volume_actual);}
void windows_media_volume_set(int percent){if(percent>=0&&percent<=100)atomic_store(&media_volume_request,percent);}
#ifdef MC3_HAS_MPRIS
int windows_media_available(void){return 1;}
#else
int windows_media_available(void){return 0;}
#endif
