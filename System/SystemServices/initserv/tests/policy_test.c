#define CHECK_TEST_NAME "initserv"
#include "check.h"
#include <initserv.h>
int main(int argc,char **argv,char **envp)
{
    (void)argc;(void)argv;(void)envp;
    DurationMs delay=INIT_MONITOR_MS;
    DurationMs expected[]={2000,4000,8000,16000,30000,30000};
    for (usize i=0;i<6;++i) {
        delay=init_restart_next(delay);
        EXPECT(1,delay==expected[i]);
    }
    check_passed();return 0;
}
