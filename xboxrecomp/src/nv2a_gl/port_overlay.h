/* One vertex batch per visible overlay frame; no GPU readback or frame queue. */
#ifdef _WIN32
#include <windows.h>
static void port_quad(float *,int *,float,float,float,float,float,int,int);
/* Cache Unicode text as horizontal pixel runs. Rasterize only when the track
 * or window width changes, without a GPU readback or media query per frame. */
static void port_music_quads(float *verts,int *n,int sw,int sh)
{
    extern int windows_media_get(wchar_t *,int);
    static wchar_t previous[512];static int width,run_count;
    static unsigned short runs[16000][3];
    wchar_t text[512];if(!windows_media_get(text,512))return;
    int w=sw-32;if(w>1400)w=1400;if(w<64)return;
    if(w!=width||wcscmp(text,previous)){
        width=w;wcscpy(previous,text);run_count=0;
        BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-28;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
        void *pixels=NULL;HDC dc=CreateCompatibleDC(NULL);if(!dc)return;
        HBITMAP bmp=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,NULL,0);
        if(!bmp){DeleteDC(dc);return;}
        HGDIOBJ old=SelectObject(dc,bmp);memset(pixels,0,w*28*4);
        HFONT font=CreateFontW(-20,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HGDIOBJ oldfont=SelectObject(dc,font);SetTextColor(dc,RGB(255,255,255));SetBkColor(dc,RGB(0,0,0));
        RECT rect={0,0,w,28};DrawTextW(dc,text,-1,&rect,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX|DT_VCENTER);
        GdiFlush();unsigned int *p=pixels;
        for(int y=0;y<28;y++)for(int x=0;x<w;){
            if(!(p[y*w+x]&0xFFFFFF)){x++;continue;}int start=x;
            while(x<w&&(p[y*w+x]&0xFFFFFF))x++;
            if(run_count<16000){runs[run_count][0]=start;runs[run_count][1]=y;runs[run_count++][2]=x-start;}
        }
        SelectObject(dc,oldfont);DeleteObject(font);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);
    }
    port_quad(verts,n,8,sh-44,w+16,36,0.06f,sw,sh);
    for(int i=0;i<run_count;i++)port_quad(verts,n,16+runs[i][0],sh-40+runs[i][1],runs[i][2],1,1,sw,sh);
}
static const unsigned char port_glyphs[][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},{31,16,16,30,16,16,16},{30,17,17,30,16,16,16},
 {15,16,16,14,1,1,30},{30,17,17,30,20,18,17},{14,17,17,31,17,17,17},
 {17,27,21,21,17,17,17},{31,16,16,30,16,16,31},
 {0,4,4,0,4,4,0},{0,0,0,0,0,6,6}};
static int port_glyph(char c){if(c>='0'&&c<='9')return c-'0';const char *s="FPSRAME:.",*p=strchr(s,c);return p?(int)(p-s)+10:-1;}
static void port_quad(float *v,int *n,float x,float y,float w,float h,float c,int sw,int sh)
{
    float l=x*2/sw-1,r=(x+w)*2/sw-1,t=1-y*2/sh,b=1-(y+h)*2/sh;
    const float q[]={l,t,c,l,b,c,r,b,c,l,t,c,r,b,c,r,t,c};memcpy(v+*n,q,sizeof q);*n+=18;
}
static void port_overlay_draw(void)
{
    extern int windows_port_overlay_get(void);
    extern int windows_port_pc_music_get(void);
    static GLuint prog,vao,vbo;static int failed;
    static Uint64 prev,epoch;static unsigned frames;static double fps,ms;
    Uint64 now=SDL_GetPerformanceCounter();double hz=(double)SDL_GetPerformanceFrequency();
    if(prev){double dt=(now-prev)/hz*1000;ms=ms?ms*0.9+dt*0.1:dt;}
    prev=now;if(!epoch)epoch=now;frames++;
    if((now-epoch)/hz>=0.5){fps=frames*hz/(now-epoch);frames=0;epoch=now;}
    if((!windows_port_overlay_get()&&!windows_port_pc_music_get())||failed)return;
    if(!prog){
        const char *vs="#version 330 core\nlayout(location=0)in vec3 p;out float c;void main(){gl_Position=vec4(p.xy,0,1);c=p.z;}";
        const char *fs="#version 330 core\nin float c;out vec4 o;void main(){o=vec4(vec3(c),1);}";
        GLuint v=compile(GL_VERTEX_SHADER,&vs,1),f=compile(GL_FRAGMENT_SHADER,&fs,1);GLint ok=0;
        if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);failed=1;return;}
        prog=glCreateProgram();glAttachShader(prog,v);glAttachShader(prog,f);glLinkProgram(prog);
        glDeleteShader(v);glDeleteShader(f);glGetProgramiv(prog,GL_LINK_STATUS,&ok);
        if(!ok){glDeleteProgram(prog);prog=0;failed=1;return;}
        glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);
    }
    int sw,sh;SDL_GL_GetDrawableSize(s_win,&sw,&sh);if(sw<1||sh<1)return;
    char text[64];snprintf(text,sizeof text,"FPS: %.1f  FRAME: %.1f MS",fps,ms);
    static float verts[320000];int n=0,scale=sh>=1000?3:2;
    if(windows_port_overlay_get())port_quad(verts,&n,8,8,(strlen(text)*6+4)*scale,11*scale,0.06f,sw,sh);
    for(int i=0;windows_port_overlay_get()&&text[i];i++){int g=port_glyph(text[i]);if(g<0)continue;
        for(int y=0;y<7;y++)for(int x=0;x<5;x++)if(port_glyphs[g][y]&(16>>x))
            port_quad(verts,&n,12+(i*6+x)*scale,12+y*scale,scale,scale,1,sw,sh);
    }
    port_music_quads(verts,&n,sw,sh);
    GLint oldprog,oldvao,oldvbo,oldfb,vp[4],flags[5],mask[4];
    const GLenum caps[]={GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,GL_SCISSOR_TEST};
    glGetIntegerv(0x8B8D,&oldprog);glGetIntegerv(0x85B5,&oldvao);glGetIntegerv(0x8894,&oldvbo);
    glGetIntegerv(0x8CA6,&oldfb);glGetIntegerv(0x0BA2,vp);glGetIntegerv(0x0C23,mask);
    for(int i=0;i<5;i++){glGetIntegerv(caps[i],&flags[i]);glDisable(caps[i]);}
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);glViewport(0,0,sw,sh);glColorMask(1,1,1,1);
    glUseProgram(prog);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,n*sizeof(float),verts,GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,0,3*sizeof(float),NULL);
    glDrawArrays(GL_TRIANGLES,0,n/3);
    glBindVertexArray(oldvao);glBindBuffer(GL_ARRAY_BUFFER,oldvbo);glUseProgram(oldprog);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,oldfb);glViewport(vp[0],vp[1],vp[2],vp[3]);
    glColorMask(mask[0],mask[1],mask[2],mask[3]);for(int i=0;i<5;i++)if(flags[i])glEnable(caps[i]);
    state_dirty();
}
#endif
