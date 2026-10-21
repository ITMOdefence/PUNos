#ifndef APPSERV_TEST_FAKES_H
#define APPSERV_TEST_FAKES_H
#include <appserv/appserv.h>
extern i32 fake_allocate_after;
extern ErrorCode fake_start_error, fake_terminate_error;
extern bool fake_finished, fake_wait_interrupt;
extern ProcessExitStatus fake_exit_status;
extern usize fake_starts, fake_terminates, fake_polls, fake_waits, fake_releases;
extern bool fake_bad_ownership;
extern ProcessLaunch fake_launch;
void fake_reset(void);
#endif
