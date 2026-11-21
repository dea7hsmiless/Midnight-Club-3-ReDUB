#define _GNU_SOURCE
#include "kernel.h"
#include "win32_compat.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

typedef void (*guest_fn)(void);
guest_fn recomp_lookup(uint32_t a){(void)a;return NULL;}
guest_fn recomp_lookup_manual(uint32_t a){(void)a;return NULL;}
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static _Thread_local int role;
static int reached,resume_a,started_b,seek_b,done_b,fd,mode;
static HANDLE file_a,file_b;
static char a_data[512],b_data[512];
static XBOX_IO_STATUS_BLOCK a_status,b_status;
static XBOX_FILE_POSITION_INFORMATION position;
static int failures;
off_t __real_lseek(int,off_t,int);

/* Force the exact interleaving that used to read or write the wrong offset.
 * With the fix, B blocks on the shared handle lock until A has completed. */
off_t __wrap_lseek(int current,off_t offset,int origin)
{
    off_t result=__real_lseek(current,offset,origin);
    if(role==1&&current==fd&&origin==SEEK_SET){
        pthread_mutex_lock(&lock);reached=1;pthread_cond_broadcast(&cv);
        while(!resume_a)pthread_cond_wait(&cv,&lock);
        pthread_mutex_unlock(&lock);
    }else if(role==2){
        pthread_mutex_lock(&lock);seek_b=1;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&lock);
    }
    return result;
}
static void check(int ok,const char *name)
{
    printf("%s: %s\n",ok?"PASS":"FAIL",name);if(!ok)failures++;
}
static void *first(void *unused)
{
    (void)unused;role=1;LARGE_INTEGER offset={.QuadPart=0};
    if(mode==1)xbox_NtWriteFile(file_a,NULL,NULL,NULL,&a_status,a_data,sizeof a_data,&offset);
    else xbox_NtReadFile(file_a,NULL,NULL,NULL,&a_status,a_data,sizeof a_data,&offset);
    return NULL;
}
static void *second(void *unused)
{
    (void)unused;role=2;
    pthread_mutex_lock(&lock);started_b=1;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&lock);
    LARGE_INTEGER offset={.QuadPart=512};
    if(mode==1)xbox_NtWriteFile(file_b,NULL,NULL,NULL,&b_status,b_data,sizeof b_data,&offset);
    else if(mode==2)xbox_NtQueryInformationFile(file_b,&b_status,&position,sizeof position,XboxFilePositionInformation);
    else if(mode==3){position.CurrentByteOffset.QuadPart=768;xbox_NtSetInformationFile(file_b,&b_status,&position,sizeof position,XboxFilePositionInformation);}
    else xbox_NtReadFile(file_b,NULL,NULL,NULL,&b_status,b_data,sizeof b_data,&offset);
    pthread_mutex_lock(&lock);done_b=1;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&lock);
    return NULL;
}
static void exercise(int test)
{
    mode=test;reached=resume_a=started_b=seek_b=done_b=0;
    memset(&a_status,0,sizeof a_status);memset(&b_status,0,sizeof b_status);
    char path[]="file-concurrency-XXXXXX";
    fd=mkstemp(path);if(fd<0)exit(10);
    char data[1024];memset(data,'A',512);memset(data+512,'B',512);
    if(write(fd,data,sizeof data)!=sizeof data)exit(11);
    file_a=w32_open_handle(fd,path);
    if(test==4)file_b=w32_open_handle(open(path,O_RDWR),path);
    else if(!DuplicateHandle(GetCurrentProcess(),file_a,GetCurrentProcess(),&file_b,0,FALSE,0))exit(12);
    memset(a_data,mode==1?'C':0,sizeof a_data);memset(b_data,mode==1?'D':0,sizeof b_data);
    pthread_t a,b;pthread_create(&a,NULL,first,NULL);
    pthread_mutex_lock(&lock);while(!reached)pthread_cond_wait(&cv,&lock);pthread_mutex_unlock(&lock);
    pthread_create(&b,NULL,second,NULL);
    pthread_mutex_lock(&lock);while(!started_b)pthread_cond_wait(&cv,&lock);
    struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec++;
    while(!seek_b&&pthread_cond_timedwait(&cv,&lock,&deadline)==0){}
    if(seek_b)while(!done_b)pthread_cond_wait(&cv,&lock);
    int independent_progress=done_b;
    resume_a=1;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&lock);
    pthread_join(a,NULL);pthread_join(b,NULL);
    check(a_status.Status==0&&a_status.Information==512,"first operation transfers all requested bytes");
    if(mode==1){
        char result[1024];ssize_t n=pread(fd,result,sizeof result,0);
        char expected[1024];memset(expected,'C',512);memset(expected+512,'D',512);
        check(b_status.Status==0&&b_status.Information==512&&n==1024&&!memcmp(result,expected,1024),"concurrent writes through duplicate handles preserve both offsets");
    }else{
        check(!memcmp(a_data,data,512),"interleaved operation cannot change first reader's offset");
        if(mode==2)check(b_status.Status==0&&position.CurrentByteOffset.QuadPart==512,"position query waits for the pending read to advance the cursor");
        else if(mode==3){
            XBOX_IO_STATUS_BLOCK ios;XBOX_FILE_POSITION_INFORMATION final;
            xbox_NtQueryInformationFile(file_a,&ios,&final,sizeof final,XboxFilePositionInformation);
            check(b_status.Status==0&&final.CurrentByteOffset.QuadPart==768,"position update is atomic with the pending read");
        }else check(b_status.Status==0&&b_status.Information==512&&!memcmp(b_data,data+512,512),"second reader receives its own requested bytes");
    }
    if(mode==4)check(independent_progress,"different file handles remain independent");
    CloseHandle(file_b);CloseHandle(file_a);unlink(path);
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    for(int test=0;test<5;test++)exercise(test);
    int pipes[2];if(pipe(pipes))return 20;
    write(pipes[1],"abc",3);close(pipes[1]);
    HANDLE stream=w32_open_handle(pipes[0],NULL);XBOX_IO_STATUS_BLOCK ios;
    LARGE_INTEGER offset={.QuadPart=0};char data[3];
    check(xbox_NtReadFile(stream,NULL,NULL,NULL,&ios,data,3,&offset)!=0&&ios.Information==0,"seek failure is reported before transferring bytes");
    check(xbox_NtReadFile(stream,NULL,NULL,NULL,&ios,data,3,NULL)==0&&ios.Information==3&&!memcmp(data,"abc",3),"failed seek releases the per-handle lock");
    CloseHandle(stream);
    return failures?1:0;
}
