#include "windows_paths.h"
#include "kernel/kernel.h"
#include <string.h>
#include <stdarg.h>

void windows_boot(void);
const char *xbox_LookupSymbolicLink(const char *name) { (void)name; return NULL; }
void xbox_log(int level, const char *category, const char *format, ...) { (void)level; (void)category; (void)format; }

int main(void)
{
    char game_buf[4096],save_buf[4096],module[4096];
    const char *game,*save;
    wchar_t host[MAX_PATH];
    windows_boot();
    game=mc3_path_env(L"MC3_GAME_DIR",game_buf,sizeof game_buf);
    save=mc3_path_env(L"MC3_SAVE_DIR",save_buf,sizeof save_buf);
    if(!game||!save||!mc3_module_path(module,sizeof module))return 1;
    xbox_path_init(game,save);
    if(!xbox_translate_path("D:\\default.xbe",host,MAX_PATH))return 2;
    FILE *f=_wfopen(host,L"rb");if(!f)return 3;
    char data[16]={0};fread(data,1,sizeof data-1,f);fclose(f);
    if(strcmp(data,"fixture"))return 4;
    char path[4096],temp[4096];
    snprintf(path,sizeof path,"%s/settings.txt",save);
    snprintf(temp,sizeof temp,"%s/settings.tmp",save);
    f=mc3_fopen_utf8(temp,L"wb");if(!f)return 5;
    fputs("preserved",f);fclose(f);
    if(!mc3_replace_utf8(temp,path)||mc3_attributes_utf8(path)==INVALID_FILE_ATTRIBUTES)return 6;
    mc3_delete_utf8(path);
    if(mc3_attributes_utf8(path)!=INVALID_FILE_ATTRIBUTES)return 7;
    puts("PASS: Unicode module discovery, environment, XBE and kernel asset paths, settings replacement");
    return 0;
}
