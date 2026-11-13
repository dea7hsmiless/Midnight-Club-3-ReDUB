#include <ctype.h>
/* Optional temporal outputs layered onto the existing shader generators.
 * Replay the same geometry with its previous transform/constants, then run
 * the current shader normally. Invalid/ambiguous history rejects reuse. */
static const char temporal_uniform_declarations[]=
    "layout(std140,set=0,binding=7)uniform RedubTemporal{\n"
    "vec4 redub_previous_m[4];vec4 redub_previous_vpoff;vec4 redub_previous_aa;\n"
    "vec4 redub_temporal;vec4 redub_render_size;};\n";

static char *temporal_rewrite(const char *src,size_t length,int previous)
{
    static const char *from[]={"main","nv2a_program","c","u_m","u_vpoff","u_aa"};
    static const char *to[]={"redub_previous_main","redub_previous_program","redub_previous_constants",
                             "redub_previous_m","redub_previous_vpoff","redub_previous_aa.xy"};
    size_t capacity=length*16+128,used=0;char *out=malloc(capacity);
    if(!out)return NULL;
    for(size_t i=0;i<length;){
        size_t start=i;
        if(isalpha((unsigned char)src[i])||src[i]=='_'){
            while(i<length&&(isalnum((unsigned char)src[i])||src[i]=='_'))i++;
            const char *replacement=NULL;
            if(previous){for(unsigned k=0;k<sizeof from/sizeof from[0];k++)
                if(i-start==strlen(from[k])&&!memcmp(src+start,from[k],i-start)){replacement=to[k];break;}}
            else if(i-start==4&&!memcmp(src+start,"main",4))replacement="redub_current_main";
            if(replacement){size_t n=strlen(replacement);memcpy(out+used,replacement,n);used+=n;}
            else{memcpy(out+used,src+start,i-start);used+=i-start;}
        }else out[used++]=src[i++];
    }
    out[used]=0;return out;
}

static int temporal_shader_build(char *vertex,size_t vertex_cap,char *fragment,size_t fragment_cap)
{
    const char *version_end=strchr(vertex,'\n'),*main=strstr(vertex,"void main()"),*program=strstr(vertex,"void nv2a_program(");
    char *current=NULL,*previous=NULL,*previous_program=NULL,*out=NULL,*pixel=NULL;
    int result=0;
    if(!version_end||!main)return 0;
    current=temporal_rewrite(main,strlen(main),0);previous=temporal_rewrite(main,strlen(main),1);
    if(program&&program<main)previous_program=temporal_rewrite(program,(size_t)(main-program),1);
    if(!current||!previous||(program&&!previous_program))goto done;
    out=malloc(vertex_cap);pixel=malloc(fragment_cap);if(!out||!pixel)goto done;
    int n=snprintf(out,vertex_cap,
        "%.*s\n%s"
        "layout(std140,set=0,binding=8)uniform RedubPreviousC{vec4 redub_previous_constants[192];};\n"
        "layout(location=7)out vec4 redub_previous;layout(location=8)out vec4 redub_current;\n"
        "layout(location=9)flat out float redub_reject;\n"
        "%.*s\n%s\n%s\n%s\n"
        "void main(){\n"
        "redub_previous=vec4(0);\n"
        "if(redub_temporal.z>0.5){redub_previous_main();redub_previous=gl_Position;}\n"
        "redub_current_main();redub_current=gl_Position;\n"
        "redub_reject=redub_temporal.z>0.5?0.0:1.0;\n"
        "if(redub_temporal.z<0.5)redub_previous=redub_current;\n"
        "gl_Position.xy+=redub_temporal.xy*gl_Position.w*redub_temporal.w;\n"
        "}\n",
        (int)(version_end-vertex),vertex,temporal_uniform_declarations,
        (int)(main-version_end-1),version_end+1,
        previous_program?previous_program:"",previous,current);
    if(n<0||(size_t)n>=vertex_cap)goto done;
    const char *fv=strchr(fragment,'\n'),*fm=strstr(fragment,"void main()");
    if(!fv||!fm)goto done;
    char *fragment_main=temporal_rewrite(fm,strlen(fm),0);
    if(!fragment_main)goto done;
    n=snprintf(pixel,fragment_cap,
        "%.*s\n%s"
        "layout(location=7)in vec4 redub_previous;layout(location=8)in vec4 redub_current;\n"
        "layout(location=9)flat in float redub_reject;layout(location=1)out vec4 redub_motion;\n"
        "%.*s\n%s\n"
        "void main(){redub_current_main();\n"
        "float reject=redub_reject;vec2 motion=vec2(0);\n"
        "if(redub_previous.w>0.000001&&redub_current.w>0.000001)\n"
        "motion=(redub_previous.xy/redub_previous.w-redub_current.xy/redub_current.w)*0.5*redub_render_size.xy;\n"
        "else reject=1.0;\n"
        "if(any(isnan(motion))||any(isinf(motion))||any(greaterThan(abs(motion),redub_render_size.xy*0.5))){reject=1.0;motion=vec2(0);}\n"
        "redub_motion=vec4(motion,reject,gl_FragCoord.z);}\n",
        (int)(fv-fragment),fragment,temporal_uniform_declarations,(int)(fm-fv-1),fv+1,fragment_main);
    free(fragment_main);
    if(n<0||(size_t)n>=fragment_cap)goto done;
    memcpy(vertex,out,strlen(out)+1);memcpy(fragment,pixel,strlen(pixel)+1);result=1;
done:
    free(current);free(previous);free(previous_program);free(out);free(pixel);return result;
}
