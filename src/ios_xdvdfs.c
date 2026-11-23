/* XDVDFS layout reference: XboxDev/extract-xiso, extract-xiso.c.
 * Independent bounded reader; no executable code is produced at install time. */
#define _FILE_OFFSET_BITS 64
#include "ios_xdvdfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <ctype.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#include "xbe_facts.h"
#include "xbe_editions.h"
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define SEEK _fseeki64
#define TELL _ftelli64
#else
#define MKDIR(p) mkdir(p,0700)
#define SEEK fseeko
#define TELL ftello
#endif
#define SECTOR 2048ull
#define MAX_TABLE (16u*1024*1024)
#define SCAN_CHUNK (1024u*1024u)
#define XDVDFS_MAGIC "MICROSOFT*XBOX*MEDIA"
#define XDVDFS_MAGIC_LEN 20u
typedef struct {FILE *f; uint64_t length, base, total, done; unsigned entries, nesting; unsigned char *copy; uint32_t ancestors[49]; int writing; const char *dest; ios_disc_progress cb; void *ctx; char *err; size_t cap;} Disc;
static unsigned u16(const unsigned char *p) {return p[0]|((unsigned)p[1]<<8);}
static uint32_t u32(const unsigned char *p) {return u16(p)|((uint32_t)u16(p+2)<<16);}
static int fail(Disc *d, const char *fmt, ...) {va_list a;va_start(a,fmt);vsnprintf(d->err,d->cap,fmt,a);va_end(a);return 0;}
static int read_at(Disc *d,uint64_t off,void *p,size_t n) {
 if(off>d->length || n>d->length-off || SEEK(d->f,off,SEEK_SET) || fread(p,1,n,d->f)!=n)return fail(d,"Disc image is truncated or unreadable.");return 1;
}
static int volume_header_at(Disc *d,uint64_t descriptor,uint32_t *root,uint32_t *size) {
 unsigned char h[2048];
 if(descriptor<32*SECTOR || descriptor+sizeof h>d->length)return 0;
 if(SEEK(d->f,descriptor,SEEK_SET)||fread(h,1,sizeof h,d->f)!=sizeof h)return 0;
 if(memcmp(h,XDVDFS_MAGIC,XDVDFS_MAGIC_LEN)||
    memcmp(h+sizeof h-XDVDFS_MAGIC_LEN,XDVDFS_MAGIC,XDVDFS_MAGIC_LEN))return 0;
 uint32_t r=u32(h+20),s=u32(h+24);uint64_t base=descriptor-32*SECTOR;
 if(!s||s>MAX_TABLE||base+(uint64_t)r*SECTOR>d->length||
    s>d->length-(base+(uint64_t)r*SECTOR))return 0;
 d->base=base;*root=r;*size=s;return 1;
}
/* Full-disc dumps can place the game partition at offsets other than the
 * handful used by common Redump/XISO layouts. Search for the paired XDVDFS
 * volume signature instead of rejecting a legitimate disc based on its
 * partition start. The overlap preserves signatures across chunk edges. */
static int find_volume(Disc *d,uint32_t *root,uint32_t *size) {
 static const uint64_t common[]={32*SECTOR,0xFD90000ull+32*SECTOR,
                                 0x2080000ull+32*SECTOR};
 for(unsigned i=0;i<sizeof common/sizeof common[0];i++)
   if(volume_header_at(d,common[i],root,size))return 1;
 unsigned char *buf=malloc(SCAN_CHUNK+2048);if(!buf)return fail(d,"Insufficient memory while scanning the ISO.");
 uint64_t off=0;size_t carry=0;int found=0;
 while(off<d->length&&!found){
   size_t want=(d->length-off)>SCAN_CHUNK?SCAN_CHUNK:(size_t)(d->length-off);
   if(SEEK(d->f,off,SEEK_SET))break;
   size_t got=fread(buf+carry,1,want,d->f);if(!got)break;
   size_t available=carry+got;unsigned char *p=buf,*end=buf+available;
   while(p<end){
     p=memchr(p,'M',(size_t)(end-p));if(!p)break;
     size_t at=(size_t)(p-buf);
     if(available-at>=XDVDFS_MAGIC_LEN&&
        !memcmp(p,XDVDFS_MAGIC,XDVDFS_MAGIC_LEN)){
       uint64_t absolute=off-(uint64_t)carry+at;
       if(volume_header_at(d,absolute,root,size)){found=1;break;}
     }
     p++;
   }
   carry=available<2047?available:2047;
   memmove(buf,buf+available-carry,carry);off+=got;
 }
 free(buf);return found;
}
static int table(Disc*,uint32_t,uint32_t,const char*,unsigned);
static int node(Disc*,unsigned char*,unsigned char*,uint32_t,uint32_t,const char*,unsigned,unsigned);
static int node_impl(Disc *d,unsigned char *b,unsigned char *seen,uint32_t size,uint32_t off,const char *path,unsigned depth,unsigned treeDepth) {
 if(treeDepth>128 || off>size || size-off<14 || (off&3))return fail(d,"Invalid XDVDFS directory entry.");
 if(seen[off/4])return fail(d,"Cyclic XDVDFS directory tree.");seen[off/4]=1;
 if(++d->entries>200000)return fail(d,"Too many disc entries.");
 unsigned left=u16(b+off),right=u16(b+off+2),n=b[off+13],attr=b[off+12];
 uint32_t sector=u32(b+off+4),bytes=u32(b+off+8);
 if(!n || n>size-off-14)return fail(d,"Invalid disc filename length.");
 char name[256],rel[2048],out[4096];memcpy(name,b+off+14,n);name[n]=0;
 if(!*path && !strcasecmp(name,"default.xbe"))strcpy(name,"default.xbe");
 for(unsigned i=0;i<n;i++)if((unsigned char)name[i]<32 || strchr("/\\:",name[i]))return fail(d,"Unsafe disc filename.");
 if(!strcmp(name,".")||!strcmp(name,"..")||name[n-1]=='.'||name[n-1]==' ')return fail(d,"Unsafe disc filename.");
 if(snprintf(rel,sizeof rel,"%s%s%s",path,*path?"/":"",name)>=(int)sizeof rel || snprintf(out,sizeof out,"%s/%s",d->dest,rel)>=(int)sizeof out)return fail(d,"Disc path is too long.");
 uint64_t pos=d->base+(uint64_t)sector*SECTOR;
 if(pos>d->length || bytes>d->length-pos)return fail(d,"Disc file extends beyond image.");
 if(left && !node(d,b,seen,size,left*4u,path,depth,treeDepth+1))return 0;
 if(attr&0x10) {
   if(d->writing && MKDIR(out))return fail(d,"Cannot create directory (duplicate name or storage error).");
   if(bytes && !table(d,sector,bytes,rel,depth+1))return 0;
 } else if(d->writing) {
   /* A fresh staging directory and exclusive creation prohibit aliases/overwrites. */
   FILE *f=fopen(out,"wbx");if(!f)return fail(d,"Cannot create game file (duplicate name or storage error).");
   unsigned char *buf=d->copy;uint64_t remain=bytes;
   while(remain){size_t chunk=remain>256*1024?256*1024:(size_t)remain;
     if(!read_at(d,pos,buf,chunk)||fwrite(buf,1,chunk,f)!=chunk){fclose(f);return fail(d,"Read/write failed. Check free storage.");}
     pos+=chunk;remain-=chunk;d->done+=chunk;if(d->cb)d->cb(d->done,d->total,rel,d->ctx);
   }
   if(fclose(f))return fail(d,"Cannot finish writing game file.");
 } else {d->total+=bytes;if(d->total>d->length*2)return fail(d,"Disc contains excessive overlapping data.");}
 if(right && !node(d,b,seen,size,right*4u,path,depth,treeDepth+1))return 0;
 return 1;
}
static int node(Disc *d,unsigned char *b,unsigned char *seen,uint32_t size,uint32_t off,const char *path,unsigned depth,unsigned treeDepth) {
 if(d->nesting>=48)return fail(d,"Excessively nested disc tree.");
 d->nesting++;int ok=node_impl(d,b,seen,size,off,path,depth,treeDepth);d->nesting--;return ok;
}
static int table(Disc *d,uint32_t sector,uint32_t size,const char *path,unsigned depth) {
 if(depth>48 || size>MAX_TABLE || size<14)return fail(d,"Invalid or excessively nested directory.");
 for(unsigned i=0;i<depth;i++)if(d->ancestors[i]==sector)return fail(d,"Cyclic XDVDFS directories.");
 d->ancestors[depth]=sector;
 unsigned char *b=malloc(size),*seen=calloc((size+3)/4,1);int ok=0;
 if(!b||!seen){fail(d,"Insufficient memory for disc directory.");goto end;}
 if(read_at(d,d->base+(uint64_t)sector*SECTOR,b,size))ok=node(d,b,seen,size,0,path,depth,0);
 end:free(b);free(seen);return ok;
}
int ios_disc_verify(const char *dir,char *error,size_t cap) {
 char p[4096];snprintf(p,sizeof p,"%s/default.xbe",dir);FILE *f=fopen(p,"rb");
 if(!f){snprintf(error,cap,"default.xbe is missing. Choose an original Xbox Remix disc.");return 0;}
 uint64_t h=14695981039346656037ull,n=0;unsigned char b[65536];size_t got;
 while((got=fread(b,1,sizeof b,f))){n+=got;for(size_t i=0;i<got;i++){h^=b[i];h*=1099511628211ull;}}
 int bad=ferror(f);fclose(f);int supported=(h==MC3_XBE_FNV64);
 #define EDITION(name,hash,region,version) supported |= (h==(hash));
 MC3_XBE_EDITIONS(EDITION)
 #undef EDITION
 if(bad||n!=MC3_XBE_SIZE||!supported){snprintf(error,cap,"Unsupported or damaged default.xbe. This build supports Xbox DUB Edition Remix (region7, USA, Europe).");return 0;}
 if(cap)error[0]=0;return 1;
}
int ios_disc_extract(const char *image,const char *dest,ios_disc_progress cb,void *ctx,char *err,size_t cap) {
 Disc d={0};d.dest=dest;d.cb=cb;d.ctx=ctx;d.err=err;d.cap=cap;
 d.f=fopen(image,"rb");if(!d.f)return fail(&d,"Cannot open selected ISO.");
 SEEK(d.f,0,SEEK_END);d.length=TELL(d.f);uint32_t root=0,size=0;
 int ok=0;if(!find_volume(&d,&root,&size)){fail(&d,"No Xbox XDVDFS volume found. The image may use raw 2352-byte sectors, be compressed, or not be an Xbox ISO.");goto end;}
 if(!table(&d,root,size,"",0))goto end;
 d.copy=malloc(256*1024);if(!d.copy){fail(&d,"Insufficient copy memory.");goto end;}
 d.writing=1;d.entries=0;
 if(!table(&d,root,size,"",0))goto end;
 ok=ios_disc_verify(dest,err,cap);
 end:free(d.copy);fclose(d.f);return ok;
}
