#ifndef PUNOS_SYSCALL_SYSCALL_H
#define PUNOS_SYSCALL_SYSCALL_H

#include <punos/syscall/constants.h>
#include <punos/syscall/errno.h>
#include <punos/syscall/types.h>

pid_t getpid(void);
pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
pid_t waitpid(pid_t pid, i32 *status, i32 options);
i32 kill(pid_t pid, i32 signal);

i32 open(const char *path, i32 flags, mode_t mode);
i32 close(i32 fd);
ssize_t read(i32 fd, void *buffer, size_t count);
ssize_t write(i32 fd, const void *buffer, size_t count);
i64 lseek(i32 fd, off_t offset, i32 origin);
i32 fstat(i32 fd, struct stat *info);
i32 ftruncate(i32 fd, off_t length);
i32 mkdir(const char *path, mode_t mode);
i32 unlink(const char *path);
i32 rename(const char *old_path, const char *new_path);
i32 fsync(i32 fd);
i32 fcntl(i32 fd, i32 command, unsigned long argument);

void *mmap(void *address, size_t length, i32 protection, i32 flags, i32 fd,
           off_t offset);
i32 mprotect(void *address, size_t length, i32 protection);
i32 munmap(void *address, size_t length);
i32 memfd_create(const char *name, u32 flags);
ssize_t getrandom(void *buffer, size_t length, u32 flags);

i32 sigaction(i32 signal, const struct sigaction *action,
              struct sigaction *old_action);
i32 sigprocmask(i32 how, const sigset_t *set, sigset_t *old_set);

i32 mount(const char *source, const char *target, const char *filesystem,
          u64 flags, const void *data);
i32 nanosleep(const struct timespec *requested, struct timespec *remaining);
i32 clock_gettime(clockid_t clock, struct timespec *time);
i32 timerfd_create(clockid_t clock, i32 flags);
i32 timerfd_settime(i32 fd, i32 flags,
                    const struct itimerspec *new_value,
                    struct itimerspec *old_value);

pid_t clone(clone_entry_t entry, void *child_stack, u64 flags, void *argument,
            i32 *parent_tid, void *tls, i32 *child_tid);
pid_t gettid(void);
_Noreturn void thread_exit(i32 status);
i32 futex(u32 *address, i32 operation, u32 value,
          const struct timespec *timeout, u32 *second_address,
          u32 third_value);

i32 socket(i32 domain, i32 type, i32 protocol);
i32 bind(i32 fd, const struct sockaddr *address, socklen_t length);
i32 listen(i32 fd, i32 backlog);
i32 accept(i32 fd, struct sockaddr *address, socklen_t *length);
i32 accept4(i32 fd, struct sockaddr *address, socklen_t *length, i32 flags);
i32 connect(i32 fd, const struct sockaddr *address, socklen_t length);
i32 getsockopt(i32 fd, i32 level, i32 option, void *value,
               socklen_t *length);
i32 setsockopt(i32 fd, i32 level, i32 option, const void *value,
               socklen_t length);
ssize_t send(i32 fd, const void *buffer, size_t length, i32 flags);
ssize_t recv(i32 fd, void *buffer, size_t length, i32 flags);
ssize_t sendmsg(i32 fd, const struct msghdr *message, i32 flags);
ssize_t recvmsg(i32 fd, struct msghdr *message, i32 flags);
i32 shutdown(i32 fd, i32 how);

i32 epoll_create1(i32 flags);
i32 epoll_ctl(i32 epoll_fd, i32 operation, i32 fd,
              struct epoll_event *event);
i32 epoll_wait(i32 epoll_fd, struct epoll_event *events, i32 max_events,
               i32 timeout_ms);
i32 eventfd(u32 initial_value, i32 flags);

i32 ioctl(i32 fd, u64 request, unsigned long argument);

_Noreturn void exit(i32 status);

#endif
