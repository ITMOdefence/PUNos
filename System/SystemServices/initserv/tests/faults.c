#include <sys/process.h>
static i32 attempts;
ErrorCode __real_process_start(const ProcessLaunch *launch,ProcessHandle *process);
ErrorCode __wrap_process_start(const ProcessLaunch *launch,ProcessHandle *process)
{
    const String *path=launch->executable;
    if (path->length>=5) {
        const char *suffix=path->data+path->length-5;
        if (suffix[0]=='r' && suffix[1]=='e' && suffix[2]=='t' && suffix[3]=='r' && suffix[4]=='y' && attempts++<3)
            return CORE_OUT_OF_MEMORY;
    }
    return __real_process_start(launch,process);
}
