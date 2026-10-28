#include <punos/syscall/constants.h>
#include <punos/syscall/syscall.h>

#include "internal/bootstrap.h"
#include "internal/raw_syscall.h"
#include "internal/syscall_result.h"
#include "x86_64/syscall_numbers.h"

extern void __restore_rt(void);

pid_t getpid(void)
{
    return (pid_t)__punos_syscall0(PUNOS_SYS_GETPID);
}

pid_t fork(void)
{
    return (pid_t)__punos_syscall_result(__punos_syscall0(PUNOS_SYS_FORK));
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    long result = __punos_syscall3(PUNOS_SYS_EXECVE, (unsigned long)path,
                                   (unsigned long)argv, (unsigned long)envp);
    return (int)__punos_syscall_result(result);
}

pid_t waitpid(pid_t pid, i32 *status, i32 options)
{
    long result = __punos_syscall4(PUNOS_SYS_WAIT4, (unsigned long)pid,
                                   (unsigned long)status,
                                   (unsigned long)options, 0);
    return (pid_t)__punos_syscall_result(result);
}

i32 kill(pid_t pid, i32 signal)
{
    return (i32)__punos_syscall_result(__punos_syscall2(
        PUNOS_SYS_KILL, (unsigned long)pid, (unsigned long)signal));
}

i32 open(const char *path, i32 flags, mode_t mode)
{
    long result = __punos_syscall3(PUNOS_SYS_OPEN, (unsigned long)path,
                                   (unsigned long)flags,
                                   (unsigned long)mode);
    return (i32)__punos_syscall_result(result);
}

i32 close(i32 fd)
{
    return (i32)__punos_syscall_result(
        __punos_syscall1(PUNOS_SYS_CLOSE, (unsigned long)fd));
}

ssize_t read(i32 fd, void *buffer, size_t count)
{
    return (ssize_t)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_READ, (unsigned long)fd, (unsigned long)buffer,
        (unsigned long)count));
}

ssize_t write(i32 fd, const void *buffer, size_t count)
{
    long result = __punos_syscall3(PUNOS_SYS_WRITE, (unsigned long)fd,
                                   (unsigned long)buffer,
                                   (unsigned long)count);

    return (ssize_t)__punos_syscall_result(result);
}

i64 lseek(i32 fd, off_t offset, i32 origin)
{
    return (i64)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_LSEEK, (unsigned long)fd, (unsigned long)offset,
        (unsigned long)origin));
}

i32 fstat(i32 fd, struct stat *info)
{
    return (i32)__punos_syscall_result(__punos_syscall2(
        PUNOS_SYS_FSTAT, (unsigned long)fd, (unsigned long)info));
}

i32 ftruncate(i32 fd, off_t length)
{
    return (i32)__punos_syscall_result(__punos_syscall2(
        PUNOS_SYS_FTRUNCATE, (unsigned long)fd, (unsigned long)length));
}

void *mmap(void *address, size_t length, i32 protection, i32 flags, i32 fd,
           off_t offset)
{
    long result = __punos_syscall6(PUNOS_SYS_MMAP, (unsigned long)address,
                                   (unsigned long)length,
                                   (unsigned long)protection,
                                   (unsigned long)flags, (unsigned long)fd,
                                   (unsigned long)offset);

    result = __punos_syscall_result(result);
    if (result == -1) {
        return MAP_FAILED;
    }

    return (void *)result;
}

i32 munmap(void *address, size_t length)
{
    long result = __punos_syscall2(PUNOS_SYS_MUNMAP, (unsigned long)address,
                                   (unsigned long)length);

    return (i32)__punos_syscall_result(result);
}

i32 mprotect(void *address, size_t length, i32 protection)
{
    return (i32)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_MPROTECT, (unsigned long)address, (unsigned long)length,
        (unsigned long)protection));
}

i32 memfd_create(const char *name, u32 flags)
{
    return (i32)__punos_syscall_result(__punos_syscall2(
        PUNOS_SYS_MEMFD_CREATE, (unsigned long)name, (unsigned long)flags));
}

ssize_t getrandom(void *buffer, size_t length, u32 flags)
{
    return (ssize_t)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_GETRANDOM, (unsigned long)buffer, (unsigned long)length,
        (unsigned long)flags));
}

i32 sigaction(i32 signal, const struct sigaction *action,
              struct sigaction *old_action)
{
    struct sigaction kernel_action;
    const struct sigaction *kernel_action_pointer = action;

    if (action != 0 && action->sa_handler != SIG_DFL &&
        action->sa_handler != SIG_IGN) {
        kernel_action = *action;
        kernel_action.sa_flags |= SA_RESTORER;
        kernel_action.sa_restorer = __restore_rt;
        kernel_action_pointer = &kernel_action;
    }

    return (i32)__punos_syscall_result(__punos_syscall4(
        PUNOS_SYS_RT_SIGACTION, (unsigned long)signal,
        (unsigned long)kernel_action_pointer, (unsigned long)old_action,
        sizeof(sigset_t)));
}

i32 sigprocmask(i32 how, const sigset_t *set, sigset_t *old_set)
{
    return (i32)__punos_syscall_result(__punos_syscall4(
        PUNOS_SYS_RT_SIGPROCMASK, (unsigned long)how, (unsigned long)set,
        (unsigned long)old_set, sizeof(sigset_t)));
}

i32 mkdir(const char *path, mode_t mode)
{
    long result = __punos_syscall2(PUNOS_SYS_MKDIR, (unsigned long)path,
                                   (unsigned long)mode);
    return (i32)__punos_syscall_result(result);
}

i32 unlink(const char *path)
{
    return (i32)__punos_syscall_result(
        __punos_syscall1(PUNOS_SYS_UNLINK, (unsigned long)path));
}

i32 rename(const char *old_path, const char *new_path)
{
    return (i32)__punos_syscall_result(__punos_syscall2(
        PUNOS_SYS_RENAME, (unsigned long)old_path,
        (unsigned long)new_path));
}

i32 fsync(i32 fd)
{
    return (i32)__punos_syscall_result(
        __punos_syscall1(PUNOS_SYS_FSYNC, (unsigned long)fd));
}

i32 mount(const char *source, const char *target, const char *filesystem,
          u64 flags, const void *data)
{
    long result = __punos_syscall5(PUNOS_SYS_MOUNT, (unsigned long)source,
                                   (unsigned long)target,
                                   (unsigned long)filesystem,
                                   (unsigned long)flags,
                                   (unsigned long)data);
    return (i32)__punos_syscall_result(result);
}

i32 nanosleep(const struct timespec *requested, struct timespec *remaining)
{
    long result = __punos_syscall2(PUNOS_SYS_NANOSLEEP,
                                   (unsigned long)requested,
                                   (unsigned long)remaining);
    return (i32)__punos_syscall_result(result);
}

i32 clock_gettime(clockid_t clock, struct timespec *time)
{
    return (i32)__punos_syscall_result(__punos_syscall2(
        PUNOS_SYS_CLOCK_GETTIME, (unsigned long)clock, (unsigned long)time));
}

i32 timerfd_create(clockid_t clock, i32 flags)
{
    return (i32)__punos_syscall_result(__punos_syscall2(
        PUNOS_SYS_TIMERFD_CREATE, (unsigned long)clock,
        (unsigned long)flags));
}

i32 timerfd_settime(i32 fd, i32 flags,
                    const struct itimerspec *new_value,
                    struct itimerspec *old_value)
{
    return (i32)__punos_syscall_result(__punos_syscall4(
        PUNOS_SYS_TIMERFD_SETTIME, (unsigned long)fd, (unsigned long)flags,
        (unsigned long)new_value, (unsigned long)old_value));
}

pid_t gettid(void)
{
    return (pid_t)__punos_syscall0(PUNOS_SYS_GETTID);
}

_Noreturn void thread_exit(i32 status)
{
    __punos_syscall1(PUNOS_SYS_EXIT, (unsigned long)status);
    __builtin_unreachable();
}

i32 futex(u32 *address, i32 operation, u32 value,
          const struct timespec *timeout, u32 *second_address,
          u32 third_value)
{
    return (i32)__punos_syscall_result(__punos_syscall6(
        PUNOS_SYS_FUTEX, (unsigned long)address, (unsigned long)operation,
        (unsigned long)value, (unsigned long)timeout,
        (unsigned long)second_address, (unsigned long)third_value));
}

i32 socket(i32 domain, i32 type, i32 protocol)
{
    return (i32)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_SOCKET, (unsigned long)domain, (unsigned long)type,
        (unsigned long)protocol));
}

i32 bind(i32 fd, const struct sockaddr *address, socklen_t length)
{
    return (i32)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_BIND, (unsigned long)fd, (unsigned long)address,
        (unsigned long)length));
}

i32 listen(i32 fd, i32 backlog)
{
    return (i32)__punos_syscall_result(__punos_syscall2(
        PUNOS_SYS_LISTEN, (unsigned long)fd, (unsigned long)backlog));
}

i32 accept(i32 fd, struct sockaddr *address, socklen_t *length)
{
    return (i32)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_ACCEPT, (unsigned long)fd, (unsigned long)address,
        (unsigned long)length));
}

i32 accept4(i32 fd, struct sockaddr *address, socklen_t *length, i32 flags)
{
    return (i32)__punos_syscall_result(__punos_syscall4(
        PUNOS_SYS_ACCEPT4, (unsigned long)fd, (unsigned long)address,
        (unsigned long)length, (unsigned long)flags));
}

i32 connect(i32 fd, const struct sockaddr *address, socklen_t length)
{
    return (i32)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_CONNECT, (unsigned long)fd, (unsigned long)address,
        (unsigned long)length));
}

i32 getsockopt(i32 fd, i32 level, i32 option, void *value,
               socklen_t *length)
{
    return (i32)__punos_syscall_result(__punos_syscall5(
        PUNOS_SYS_GETSOCKOPT, (unsigned long)fd, (unsigned long)level,
        (unsigned long)option, (unsigned long)value, (unsigned long)length));
}

i32 setsockopt(i32 fd, i32 level, i32 option, const void *value,
               socklen_t length)
{
    return (i32)__punos_syscall_result(__punos_syscall5(
        PUNOS_SYS_SETSOCKOPT, (unsigned long)fd, (unsigned long)level,
        (unsigned long)option, (unsigned long)value, (unsigned long)length));
}

ssize_t send(i32 fd, const void *buffer, size_t length, i32 flags)
{
    return (ssize_t)__punos_syscall_result(__punos_syscall6(
        PUNOS_SYS_SENDTO, (unsigned long)fd, (unsigned long)buffer,
        (unsigned long)length, (unsigned long)flags, 0, 0));
}

ssize_t recv(i32 fd, void *buffer, size_t length, i32 flags)
{
    return (ssize_t)__punos_syscall_result(__punos_syscall6(
        PUNOS_SYS_RECVFROM, (unsigned long)fd, (unsigned long)buffer,
        (unsigned long)length, (unsigned long)flags, 0, 0));
}

i32 fcntl(i32 fd, i32 command, unsigned long argument)
{
    return (i32)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_FCNTL, (unsigned long)fd, (unsigned long)command, argument));
}

ssize_t sendmsg(i32 fd, const struct msghdr *message, i32 flags)
{
    return (ssize_t)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_SENDMSG, (unsigned long)fd, (unsigned long)message,
        (unsigned long)flags));
}

ssize_t recvmsg(i32 fd, struct msghdr *message, i32 flags)
{
    return (ssize_t)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_RECVMSG, (unsigned long)fd, (unsigned long)message,
        (unsigned long)flags));
}

i32 shutdown(i32 fd, i32 how)
{
    return (i32)__punos_syscall_result(__punos_syscall2(
        PUNOS_SYS_SHUTDOWN, (unsigned long)fd, (unsigned long)how));
}

i32 epoll_create1(i32 flags)
{
    return (i32)__punos_syscall_result(
        __punos_syscall1(PUNOS_SYS_EPOLL_CREATE1, (unsigned long)flags));
}

i32 epoll_ctl(i32 epoll_fd, i32 operation, i32 fd,
              struct epoll_event *event)
{
    return (i32)__punos_syscall_result(__punos_syscall4(
        PUNOS_SYS_EPOLL_CTL, (unsigned long)epoll_fd,
        (unsigned long)operation, (unsigned long)fd, (unsigned long)event));
}

i32 epoll_wait(i32 epoll_fd, struct epoll_event *events, i32 max_events,
               i32 timeout_ms)
{
    return (i32)__punos_syscall_result(__punos_syscall4(
        PUNOS_SYS_EPOLL_WAIT, (unsigned long)epoll_fd,
        (unsigned long)events, (unsigned long)max_events,
        (unsigned long)timeout_ms));
}

i32 eventfd(u32 initial_value, i32 flags)
{
    return (i32)__punos_syscall_result(__punos_syscall2(
        PUNOS_SYS_EVENTFD2, (unsigned long)initial_value,
        (unsigned long)flags));
}

i32 ioctl(i32 fd, u64 request, unsigned long argument)
{
    return (i32)__punos_syscall_result(__punos_syscall3(
        PUNOS_SYS_IOCTL, (unsigned long)fd, (unsigned long)request, argument));
}

_Noreturn void exit(i32 status)
{
    __punos_raw_exit_group(status);
}
