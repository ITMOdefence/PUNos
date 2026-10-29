#include <punos/syscall/syscall.h>

#define OFFSET_OF(type, member) __builtin_offsetof(type, member)

_Static_assert(sizeof(u8) == 1, "u8 ABI mismatch");
_Static_assert(sizeof(u16) == 2, "u16 ABI mismatch");
_Static_assert(sizeof(u32) == 4, "u32 ABI mismatch");
_Static_assert(sizeof(u64) == 8, "u64 ABI mismatch");
_Static_assert(sizeof(void *) == 8, "only x86_64 is supported");
_Static_assert(sizeof(size_t) == 8, "size_t ABI mismatch");
_Static_assert(sizeof(struct timespec) == 16, "timespec ABI mismatch");
_Static_assert(sizeof(struct itimerspec) == 32, "itimerspec ABI mismatch");
_Static_assert(sizeof(sigset_t) == 8, "sigset_t ABI mismatch");
_Static_assert(sizeof(struct sigaction) == 32, "sigaction ABI mismatch");
_Static_assert(OFFSET_OF(struct sigaction, sa_mask) == 24,
               "sigaction layout mismatch");
_Static_assert(sizeof(struct stat) == 144, "stat ABI mismatch");
_Static_assert(OFFSET_OF(struct stat, st_size) == 48, "stat layout mismatch");
_Static_assert(OFFSET_OF(struct stat, st_ctime_nsec) == 112,
               "stat layout mismatch");
_Static_assert(sizeof(struct sockaddr) == 16, "sockaddr ABI mismatch");
_Static_assert(sizeof(struct sockaddr_un) == 110, "sockaddr_un ABI mismatch");
_Static_assert(sizeof(struct sockaddr_in) == 16, "sockaddr_in ABI mismatch");
_Static_assert(sizeof(struct sockaddr_in6) == 28, "sockaddr_in6 ABI mismatch");
_Static_assert(sizeof(struct iovec) == 16, "iovec ABI mismatch");
_Static_assert(sizeof(struct msghdr) == 56, "msghdr ABI mismatch");
_Static_assert(OFFSET_OF(struct msghdr, msg_iov) == 16,
               "msghdr layout mismatch");
_Static_assert(OFFSET_OF(struct msghdr, msg_flags) == 48,
               "msghdr layout mismatch");
_Static_assert(sizeof(struct cmsghdr) == 16, "cmsghdr ABI mismatch");
_Static_assert(sizeof(epoll_data_t) == 8, "epoll_data_t ABI mismatch");
_Static_assert(sizeof(struct epoll_event) == 12, "epoll_event ABI mismatch");
_Static_assert(OFFSET_OF(struct epoll_event, data) == 4,
               "epoll_event layout mismatch");

int punos_syscall_abi_compile_test(void)
{
    return 0;
}
