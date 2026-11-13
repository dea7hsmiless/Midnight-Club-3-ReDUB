/* Projection jitter in render-pixel units. Motion remains unjittered. */
static float temporal_halton(unsigned index,unsigned base)
{
    float fraction=1.0f,value=0.0f;
    while(index){fraction/=base;value+=fraction*(index%base);index/=base;}
    return value;
}
static void temporal_jitter_pixels(uint32_t frame,float *x,float *y)
{
    /* Quality at 1.5x: 8*(output/render)^2 = 18 phases. */
    unsigned index=frame%18+1;
    *x=temporal_halton(index,2)-0.5f;*y=temporal_halton(index,3)-0.5f;
}
