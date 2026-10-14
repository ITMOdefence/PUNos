#include <special/log.h>
#include <punos/syscall/syscall.h>
static i32 owner;
static bool fail_flush;
static _Atomic bool fail_join;
ErrorCode __real_log_store_open(const LogStoreConfig *config,LogStoreHandle *store);
ErrorCode __real_log_store_append(LogStoreHandle store,const LogRecordInput *record);
ErrorCode __real_log_store_flush(LogStoreHandle store);
ErrorCode __real_log_store_close(LogStoreHandle *store);
ErrorCode __wrap_log_store_open(const LogStoreConfig *config,LogStoreHandle *store)
{
    owner=gettid();
    if (owner==getpid()) return CORE_INVALID_STATE;
    return __real_log_store_open(config,store);
}
ErrorCode __wrap_log_store_append(LogStoreHandle store,const LogRecordInput *record)
{
    if (owner!=gettid()) return CORE_INVALID_STATE;
    if (record->component->length==9 && record->component->data[0]=='f') fail_join=true;
    if (record->component->length==10 && record->component->data[0]=='f') fail_flush=true;
    if (record->component->length==11 && record->component->data[0]=='f') return CORE_IO_ERROR;
    if (record->component->length==5 && record->component->data[0]=='p') {
        DurationMs remaining; (void)time_sleep(5000,&remaining);
    }
    return __real_log_store_append(store,record);
}
ErrorCode __wrap_log_store_flush(LogStoreHandle store)
{ return owner!=gettid()?CORE_INVALID_STATE:(fail_flush?CORE_IO_ERROR:__real_log_store_flush(store)); }
ErrorCode __wrap_log_store_close(LogStoreHandle *store)
{ return owner==gettid()?__real_log_store_close(store):CORE_INVALID_STATE; }

ErrorCode __real_thread_join(ThreadHandle *thread,ThreadExitStatus *status);
ErrorCode __wrap_thread_join(ThreadHandle *thread,ThreadExitStatus *status)
{ return fail_join?CORE_IO_ERROR:__real_thread_join(thread,status); }
