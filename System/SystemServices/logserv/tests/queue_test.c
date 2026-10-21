#define CHECK_TEST_NAME "logserv"
#include "check.h"
#include <logserv.h>
#include "wire.h"
static i32 fail_after=-1;
ErrorCode __real_memory_allocate(usize size,MemoryBlock *block);
ErrorCode __wrap_memory_allocate(usize size,MemoryBlock *block)
{
    if (fail_after==0) return CORE_OUT_OF_MEMORY;
    if (fail_after>0) --fail_after;
    return __real_memory_allocate(size,block);
}
int main(int argc,char **argv,char **envp)
{
    (void)argc;(void)argv;(void)envp;
    LogQueue q={.ready=true};
    EXPECT(1,mutex_create(&q.mutex)==CORE_OK);
    EXPECT(2,condition_create(&q.condition)==CORE_OK);
    u8 payload[19]={1,0};
    wire_write(payload+2,4,1);wire_write(payload+6,4,2);payload[10]=2;
    wire_write(payload+11,2,1);wire_write(payload+13,4,1);payload[17]='c';payload[18]='m';
    for (i32 i=0;i<3;++i) {
        fail_after=i;
        EXPECT(3,log_queue_submit(&q,(Bytes){payload,19})==CORE_OUT_OF_MEMORY);
        EXPECT(4,q.records==0 && q.bytes==0);
    }
    fail_after=-1;
    EXPECT(5,log_queue_submit(&q,(Bytes){payload,19})==CORE_OK);
    EXPECT(6,q.records==1 && q.bytes==2);
    EXPECT(7,log_queue_stop(&q)==CORE_OK);
    EXPECT(8,q.mutex==NULL && q.condition==NULL && q.records==0);
    check_passed();return 0;
}
