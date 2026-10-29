#ifndef PUNOS_SYSCALL_TYPES_H
#define PUNOS_SYSCALL_TYPES_H

/* Freestanding Linux x86_64 ABI types. Do not include host libc headers here. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;

typedef u64 size_t;
typedef i64 ssize_t;
typedef i64 off_t;
typedef i32 pid_t;
typedef u32 mode_t;
typedef u32 socklen_t;
typedef i64 time_t;
typedef i32 clockid_t;

typedef u64 sigset_t;
typedef void (*sighandler_t)(i32 signum);
typedef i32 (*clone_entry_t)(void *argument);

struct timespec {
    time_t tv_sec;
    i64 tv_nsec;
};

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

struct sigaction {
    sighandler_t sa_handler;
    u64 sa_flags;
    void (*sa_restorer)(void);
    sigset_t sa_mask;
};

struct stat {
    u64 st_dev;
    u64 st_ino;
    u64 st_nlink;
    u32 st_mode;
    u32 st_uid;
    u32 st_gid;
    u32 __pad0;
    u64 st_rdev;
    i64 st_size;
    i64 st_blksize;
    i64 st_blocks;
    u64 st_atime;
    u64 st_atime_nsec;
    u64 st_mtime;
    u64 st_mtime_nsec;
    u64 st_ctime;
    u64 st_ctime_nsec;
    i64 __unused[3];
};

struct sockaddr {
    u16 sa_family;
    char sa_data[14];
};

struct sockaddr_un {
    u16 sun_family;
    char sun_path[108];
};

struct in_addr {
    u32 s_addr;
};

struct sockaddr_in {
    u16 sin_family;
    u16 sin_port;
    struct in_addr sin_addr;
    u8 sin_zero[8];
};

struct in6_addr {
    u8 s6_addr[16];
};

struct sockaddr_in6 {
    u16 sin6_family;
    u16 sin6_port;
    u32 sin6_flowinfo;
    struct in6_addr sin6_addr;
    u32 sin6_scope_id;
};

struct iovec {
    void *iov_base;
    size_t iov_len;
};

struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    i32 msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    i32 cmsg_level;
    i32 cmsg_type;
};

typedef union epoll_data {
    void *ptr;
    i32 fd;
    u32 u32;
    u64 u64;
} epoll_data_t;

struct epoll_event {
    u32 events;
    epoll_data_t data;
} __attribute__((packed));

#endif
