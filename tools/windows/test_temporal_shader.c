#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../../xboxrecomp/src/nv2a_gl/gl_vsh.h"
#include "../../xboxrecomp/src/nv2a_gl/gl_psh.h"
#include "../../xboxrecomp/src/nv2a_vk/temporal_shader.h"
extern int nv2a_shader_vk;
static int save(const char *directory,const char *name,const char *text)
{
    char path[4096];snprintf(path,sizeof path,"%s/%s",directory,name);
    FILE *file=fopen(path,"wb");if(!file)return 0;
    fputs(text,file);return fclose(file)==0;
}
int main(int argc,char **argv)
{
    static char vertex[640*1024],fragment[128*1024];
    uint32_t program[1][4]={{0,(1u<<21)|0x1b,2u<<26,(15u<<12)|(1u<<11)|(15u<<20)|1}};
    Nv2aPshKey key={0};nv2a_shader_vk=1;
    if(argc!=2)return 1;
    for(int vp=0;vp<2;vp++){
        size_t n=(size_t)snprintf(vertex,sizeof vertex,"%s",nv2a_gl_vsh_prelude());
        if(vp){int k=nv2a_gl_vsh_program(program,1,0,vertex+n,sizeof vertex-n);if(k<0)return 2;n+=(size_t)k;}
        snprintf(vertex+n,sizeof vertex-n,"%s",vp?nv2a_gl_vsh_main_program():nv2a_gl_vsh_fixed());
        if(nv2a_gl_psh(&key,fragment,sizeof fragment)<0)return 3;
        if(!temporal_shader_build(vertex,sizeof vertex,fragment,sizeof fragment))return 4;
        if(!strstr(vertex,"redub_previous_main")||!strstr(fragment,"redub_motion=vec4"))return 5;
        if(!save(argv[1],vp?"program.vert":"fixed.vert",vertex)||!save(argv[1],vp?"program.frag":"fixed.frag",fragment))return 6;
    }
    puts("Generated current/previous transform shaders from actual fixed-function and vertex-program generators.");
    return 0;
}
