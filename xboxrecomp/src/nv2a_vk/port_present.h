/* Final display processing shared by native Vulkan and DLSS presentation.
 * Never modifies game render targets or their destination-alpha masks. */
#if defined(_WIN32) || defined(__linux__)
static struct {
    VkPipeline pipeline; VkPipelineLayout layout; VkDescriptorSetLayout set;
    VkImage image; VkImageView view; VkDeviceMemory mem; uint32_t w,h;
    VkTex overlay;
} s_port_present;
static const char port_present_shader[]=
"#version 450\n"
"layout(local_size_x=8,local_size_y=8)in;\n"
"layout(set=0,binding=0)uniform sampler2D scene;\n"
"layout(set=0,binding=1)uniform sampler2D overlay;\n"
"layout(rgba8,set=0,binding=2)writeonly uniform image2D outputImage;\n"
"layout(push_constant)uniform Params{vec4 effects;ivec4 rect;}pc;\n"
"vec3 sampleAt(ivec2 p){return texelFetch(scene,clamp(p,ivec2(0),pc.rect.zw),0).rgb;}\n"
"vec3 filteredAt(vec2 uv){vec2 sz=vec2(textureSize(scene,0));return textureLod(scene,clamp(uv,.5/sz,(vec2(pc.rect.zw)+.5)/sz),0).rgb;}\n"
"float luma(vec3 c){return dot(c,vec3(.299,.587,.114));}\n"
"void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy),sz=imageSize(outputImage);if(any(greaterThanEqual(p,sz)))return;\n"
"vec3 c=sampleAt(p),n=sampleAt(p+ivec2(0,-1)),s=sampleAt(p+ivec2(0,1)),e=sampleAt(p+ivec2(1,0)),w=sampleAt(p+ivec2(-1,0));\n"
"if(pc.effects.w>0.5){vec2 inv=1./vec2(sz),uv=(vec2(p)+.5)*inv;float nw=luma(sampleAt(p+ivec2(-1,-1))),ne=luma(sampleAt(p+ivec2(1,-1))),sw=luma(sampleAt(p+ivec2(-1,1))),se=luma(sampleAt(p+ivec2(1,1))),m=luma(c);\n"
"float lo=min(m,min(min(nw,ne),min(sw,se))),hi=max(m,max(max(nw,ne),max(sw,se)));vec2 dir=vec2(-((nw+ne)-(sw+se)),(nw+sw)-(ne+se));float span=pc.effects.w==2.?4.:pc.effects.w==3.?12.:8.;float reduce=max((nw+ne+sw+se)*(pc.effects.w==2.?.0625:.03125),.0078125);dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),vec2(-span),vec2(span))*inv;\n"
"vec3 a=.5*(filteredAt(uv+dir*(-1./6.))+filteredAt(uv+dir*(1./6.)));vec3 b=a*.5+.25*(filteredAt(uv+dir*(-.5))+filteredAt(uv+dir*.5));float lb=luma(b);c=lb<lo||lb>hi?a:b;}\n"
"c=clamp(c+(c-(n+s+e+w)*.25)*pc.effects.z*.5,0.,1.);c=pow(clamp(c*pc.effects.y,0.,1.),vec3(1./pc.effects.x));\n"
"ivec2 q=ivec2(vec2(p)*vec2(pc.rect.xy)/vec2(sz));vec4 o=vec4(0);\n"
"if(q.x>=8&&q.x<1408&&q.y>=8&&q.y<56)o=texelFetch(overlay,ivec2(q.x-8,q.y-8),0);\n"
"if(q.x>=8&&q.x<1408&&q.y>=pc.rect.y-56&&q.y<pc.rect.y-8)o=texelFetch(overlay,ivec2(q.x-8,q.y-(pc.rect.y-56)+48),0);\n"
"imageStore(outputImage,p,vec4(mix(c,o.rgb,o.a),1));}\n";
static void port_present_overlay(void)
{
    extern int windows_port_overlay_get(void),windows_media_get(wchar_t *,int);
    static wchar_t old[640];static Uint64 epoch,previous;static unsigned frames;static double fps,ms;
    Uint64 now=SDL_GetPerformanceCounter();double hz=(double)SDL_GetPerformanceFrequency();
    if(previous){double dt=(now-previous)*1000./hz;ms=ms?ms*.9+dt*.1:dt;}previous=now;
    if(!epoch)epoch=now;frames++;if((now-epoch)/hz>=.5){fps=frames*hz/(now-epoch);frames=0;epoch=now;}
    wchar_t song[512]={0},text[640];windows_media_get(song,512);
    if(windows_port_overlay_get())swprintf(text,640,L"FPS: %.1f  FRAME: %.1f MS\n%ls",fps,ms,song);
    else swprintf(text,640,L"\n%ls",song);
    if(s_port_present.overlay.image&&!wcscmp(text,old))return;
#ifdef _WIN32
    BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=1400;bi.bmiHeader.biHeight=-96;
    bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    void *pixels=NULL;HDC dc=CreateCompatibleDC(NULL);if(!dc)return;
    HBITMAP bmp=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,NULL,0);if(!bmp){DeleteDC(dc);return;}
    HGDIOBJ before=SelectObject(dc,bmp);memset(pixels,0,1400*96*4);
    HFONT font=CreateFontW(-22,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    HGDIOBJ beforefont=SelectObject(dc,font);SetTextColor(dc,RGB(255,255,255));SetBkColor(dc,RGB(16,16,16));
    wchar_t *newline=wcschr(text,L'\n');if(newline)*newline=0;
    RECT top={0,0,1400,48},bottom={0,48,1400,96};
    if(text[0])DrawTextW(dc,text,-1,&top,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
    if(song[0])DrawTextW(dc,song,-1,&bottom,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
    GdiFlush();uint32_t *px=pixels;for(int i=0;i<1400*96;i++)if(px[i]&0xFFFFFF)px[i]|=0xFF000000;
    VkTex *t=&s_port_present.overlay;
    if(!t->image){t->w=1400;t->h=96;make_image(t->w,t->h,VK_FORMAT_B8G8R8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT,&t->image,&t->view,&t->mem);}
    if(t->image){tex_upload(t,pixels,1400*96*4,0,0);if(newline)*newline=L'\n';wcscpy(old,text);}
    SelectObject(dc,beforefont);DeleteObject(font);SelectObject(dc,before);DeleteObject(bmp);DeleteDC(dc);
#else
    static const unsigned char alpha[26][7]={
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,25,21,19,19,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
    static const unsigned char digits[10][7]={
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
    static uint32_t pixels[1400*96];
    memset(pixels,0,sizeof pixels);
    int show_fps=windows_port_overlay_get();
    if(show_fps)for(int y=8;y<56;y++)for(int x=8;x<1408;x++)pixels[y*1400+x]=0x10000000u;
    if(song[0])for(int y=40;y<88;y++)for(int x=8;x<1408;x++)pixels[y*1400+x]=0x10000000u;
    const wchar_t *lines[2]={text,song};const int ys[2]={17,61};
    for(int line=0;line<2;line++){
        if(line==0&&!show_fps)continue;
        const wchar_t *p=lines[line];int x=12;
        while(*p&&x<1380){int c=(int)*p++,glyph=0;unsigned char blank[7]={0};const unsigned char *rows=blank;
            if(c>='a'&&c<='z')c-=32;
            if(c>='A'&&c<='Z')rows=alpha[c-'A'];
            else if(c>='0'&&c<='9')rows=digits[c-'0'];
            else if(c==':'){blank[2]=blank[5]=6;}
            else if(c=='.'){blank[6]=6;}
            else if(c=='-'){blank[3]=31;}
            else if(c=='/'){blank[0]=1;blank[1]=2;blank[2]=2;blank[3]=4;blank[4]=8;blank[5]=8;blank[6]=16;}
            else if(c=='?'){blank[0]=14;blank[1]=17;blank[2]=1;blank[3]=2;blank[4]=4;blank[6]=4;}
            else if(c=='%'){blank[0]=17;blank[1]=2;blank[2]=4;blank[3]=8;blank[4]=17;}
            else if(c!=' '){x+=12;continue;}
            glyph=c!=' ';
            if(glyph)for(int y=0;y<7;y++)for(int b=0;b<5;b++)if(rows[y]&(16>>b))
                for(int sy=0;sy<2;sy++)for(int sx=0;sx<2;sx++)pixels[(ys[line]+y*2+sy)*1400+x+b*2+sx]=0xFFFFFFFFu;
            x+=12;
        }
    }
    VkTex *t=&s_port_present.overlay;
    if(!t->image){t->w=1400;t->h=96;make_image(t->w,t->h,VK_FORMAT_B8G8R8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT,&t->image,&t->view,&t->mem);}
    if(t->image)tex_upload(t,(const uint8_t *)pixels,sizeof pixels,0,0);
    wcscpy(old,text);
#endif
}
static VkImage port_present_process(VkSurf *source)
{
    extern float windows_port_gamma_get(void),windows_port_brightness_get(void),windows_port_sharpen_get(void);
    extern int windows_port_fxaa_get(void),windows_media_gameplay_get(void);
    if(!s_port_present.pipeline){
        VkShaderModule module=compile(GLSLANG_STAGE_COMPUTE,port_present_shader);if(!module)return source->image;
        VkDescriptorSetLayoutBinding bindings[3]={0};for(int i=0;i<3;i++){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;bindings[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;}
        VkDescriptorSetLayoutCreateInfo ds={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};ds.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;ds.bindingCount=3;ds.pBindings=bindings;
        VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,32};
        VkPipelineLayoutCreateInfo pl={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&s_port_present.set;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
        VkComputePipelineCreateInfo cp={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        VkResult result=vkCreateDescriptorSetLayout(s_dev,&ds,NULL,&s_port_present.set);
        if(result==VK_SUCCESS)result=vkCreatePipelineLayout(s_dev,&pl,NULL,&s_port_present.layout);
        cp.layout=s_port_present.layout;cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=module;cp.stage.pName="main";
        if(result==VK_SUCCESS)result=vkCreateComputePipelines(s_dev,VK_NULL_HANDLE,1,&cp,NULL,&s_port_present.pipeline);
        vkDestroyShaderModule(s_dev,module,NULL);if(result!=VK_SUCCESS)return source->image;
    }
    if(s_port_present.w!=source->pw||s_port_present.h!=source->ph){
        flush_frame_wait();
        if(s_port_present.image){vkDestroyImageView(s_dev,s_port_present.view,NULL);vkDestroyImage(s_dev,s_port_present.image,NULL);vkFreeMemory(s_dev,s_port_present.mem,NULL);s_port_present.image=VK_NULL_HANDLE;}
        if(!make_image(source->pw,source->ph,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT,&s_port_present.image,&s_port_present.view,&s_port_present.mem))return source->image;
        s_port_present.w=source->pw;s_port_present.h=source->ph;
    }
    port_present_overlay();if(!s_port_present.overlay.image)return source->image;
    end_rendering();barrier_all();
    VkDescriptorImageInfo images[3]={0};VkWriteDescriptorSet writes[3]={0};VkImageView views[]={source->view,s_port_present.overlay.view,s_port_present.view};
    for(int i=0;i<3;i++){images[i].imageView=views[i];images[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;if(i<2)images[i].sampler=sampler_get(i==0?0x303:0x30303);writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;writes[i].pImageInfo=&images[i];}
    int valid_x=(int)source->pw-1,valid_y=(int)source->ph-1;
    /* DLSS already reconstructs the image. Native Vulkan uses FXAA only
     * while driving, preserving sharp menu and loading text. */
    static int previous_mode=-1;
    int aa_mode=(!s_temporal_on&&windows_media_gameplay_get())?windows_port_fxaa_get():0;
    if(aa_mode!=previous_mode){LOGE("  [VK] active presentation FXAA mode %d%s\n",aa_mode,s_temporal_on?" (DLSS active)":"");previous_mode=aa_mode;}
    struct {float effects[4];int rect[4];} pc={{windows_port_gamma_get(),windows_port_brightness_get(),windows_port_sharpen_get(),(float)aa_mode},{(int)s_sc_extent.width,(int)s_sc_extent.height,valid_x,valid_y}};
    vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_COMPUTE,s_port_present.pipeline);p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_COMPUTE,s_port_present.layout,0,3,writes);
    vkCmdPushConstants(s_cb,s_port_present.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof pc,&pc);vkCmdDispatch(s_cb,(source->pw+7)/8,(source->ph+7)/8,1);barrier_all();return s_port_present.image;
}
#endif
