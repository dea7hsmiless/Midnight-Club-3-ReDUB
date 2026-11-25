#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static uint8_t *memory;
static uint32_t calls,methods[4],values[4];
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)memory; }
void nv2a_pb_exec_method(uint32_t channel,uint32_t method,uint32_t value)
{
    (void)channel;
    if(calls<4){methods[calls]=method;values[calls]=value;}calls++;
}
void nv2a_pb_exec_report(void) {}
/* This branch's scanner counts desyncs; the counter lives on the exec side,
 * which this unit test replaces with stubs. */
void nv2a_pb_note_desync(void) {}
uint32_t nv2a_pb_scan(uint32_t put);
void nv2a_pb_resync(uint32_t get);
#define RAM(at) (*(uint32_t *)(memory+0x80000000u+(at)))
#define LIVE_PUT (*(uint32_t *)(memory+0xFD800040u))
static void check(int ok,const char *name)
{
    printf("%s: %s\n",ok?"PASS":"FAIL",name);
    if(!ok)exit(1);
}
static void reset(void){calls=0;nv2a_pb_resync(0x1000);}
int main(void)
{
    memory=VirtualAlloc(NULL,(SIZE_T)1<<32,MEM_RESERVE,PAGE_NOACCESS);
    check(memory!=NULL,"reserve sparse guest address space");
    check(VirtualAlloc(memory+0x80000000u,64u<<20,MEM_COMMIT,PAGE_READWRITE)!=NULL,"map guest command RAM");
    check(VirtualAlloc(memory+0xFD800000u,4096,MEM_COMMIT,PAGE_READWRITE)!=NULL,"map DMA registers");
    _putenv_s("RECOMP_PB_EXEC","1");
    reset();RAM(0x1000)=(2u<<18)|0x200u;RAM(0x1004)=0xAA;RAM(0x1008)=0xBB;
    LIVE_PUT=0x100C;
    check(nv2a_pb_scan(LIVE_PUT)==0x100C&&calls==2&&methods[0]==0x200&&methods[1]==0x204&&values[1]==0xBB,"execute submitted packet exactly once");
    reset();LIVE_PUT=0x1008;
    check(nv2a_pb_scan(LIVE_PUT)==0x1000&&calls==0,"retain incomplete packet without side effects");
    LIVE_PUT=0x100C;
    check(nv2a_pb_scan(LIVE_PUT)==0x100C&&calls==2,"resume completed packet");
    reset();LIVE_PUT=0x100C;
    check(nv2a_pb_scan(0x1008)==0x100C&&calls==2,"refresh reused PUT inside packet payload");
    reset();RAM(0x1000)=0x2001;RAM(0x2000)=0x3002;
    RAM(0x3000)=(1u<<18)|0x300u;RAM(0x3004)=11;RAM(0x3008)=0x20000;
    RAM(0x2004)=(1u<<18)|0x400u;RAM(0x2008)=22;RAM(0x200C)=0x2000100C;
    check(nv2a_pb_scan(0x100C)==0x100C&&calls==2&&values[0]==11&&values[1]==22,"follow jump, call, return and ring wrap");
    reset();RAM(0x1000)=0x08000001;
    check(nv2a_pb_scan(0x100C)==0x1000&&calls==0,"invalid GPU target is not reported consumed");
    VirtualFree(memory,0,MEM_RELEASE);
    return 0;
}
