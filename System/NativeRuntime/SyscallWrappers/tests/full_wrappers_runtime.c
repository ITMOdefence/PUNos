#include <punos/syscall/syscall.h>
#include <runtime/thread-state.h>

#include "../src/internal/bootstrap.h"

#define PAGE_SIZE 4096
#define THREAD_STACK_SIZE (64 * 1024)

static volatile i32 signal_seen;
static volatile i32 thread_result;

_Noreturn static void fail(i32 status)
{
    exit(status);
}

static void signal_handler(i32 signal)
{
    signal_seen = signal;
}

static i32 thread_entry(void *argument)
{
    volatile i32 *child_tid = (volatile i32 *)argument;
    if (sys_errno_location() == 0) {
        thread_result = -1;
        return 1;
    }
    if (gettid() <= 0) {
        thread_result = -2;
        return 1;
    }
    if (*child_tid <= 0) {
        thread_result = -3;
        return 1;
    }
    thread_result = 1;
    return 0;
}

static void test_files(void)
{
    static const char path_a[] = "/tmp/punos-wrapper-test-a";
    static const char path_b[] = "/tmp/punos-wrapper-test-b";
    static const char payload[] = "PUNos";
    char buffer[sizeof(payload)] = { 0 };
    struct stat info;
    i32 fd;

    (void)unlink(path_a);
    (void)unlink(path_b);
    fd = open(path_a, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0 || write(fd, payload, sizeof(payload)) != (ssize_t)sizeof(payload) ||
        lseek(fd, 0, SEEK_SET) != 0 ||
        read(fd, buffer, sizeof(buffer)) != (ssize_t)sizeof(buffer) ||
        fstat(fd, &info) != 0 || info.st_size != (i64)sizeof(payload) ||
        fsync(fd) != 0 || close(fd) != 0) {
        fail(30);
    }
    for (size_t index = 0; index < sizeof(payload); ++index) {
        if (buffer[index] != payload[index]) {
            fail(31);
        }
    }
    if (rename(path_a, path_b) != 0 || unlink(path_b) != 0) {
        fail(32);
    }
}

static void test_memory_and_time(void)
{
    struct timespec now;
    struct itimerspec timer = { { 0, 0 }, { 0, 1000000 } };
    void *memory = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    i32 timer_fd;

    if (memory == MAP_FAILED || mprotect(memory, PAGE_SIZE, PROT_READ) != 0 ||
        mprotect(memory, PAGE_SIZE, PROT_READ | PROT_WRITE) != 0 ||
        munmap(memory, PAGE_SIZE) != 0 ||
        clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        fail(33);
    }

    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (timer_fd < 0 || timerfd_settime(timer_fd, 0, &timer, 0) != 0 ||
        close(timer_fd) != 0) {
        fail(34);
    }
}

static void test_shared_memory_and_random(void)
{
    static const char name[] = "punos-wrapper-test";
    u8 random_bytes[32] = { 0 };
    struct stat info;
    void *mapping;
    i32 fd;
    i32 any_random = 0;

    fd = memfd_create(name, MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, PAGE_SIZE) != 0 ||
        fstat(fd, &info) != 0 || info.st_size != PAGE_SIZE) {
        fail(57);
    }
    mapping = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        fail(58);
    }
    ((u8 *)mapping)[0] = 0x5a;
    if (((u8 *)mapping)[0] != 0x5a || munmap(mapping, PAGE_SIZE) != 0 ||
        close(fd) != 0) {
        fail(59);
    }

    if (getrandom(random_bytes, sizeof(random_bytes), 0) !=
        (ssize_t)sizeof(random_bytes)) {
        fail(60);
    }
    for (size_t index = 0; index < sizeof(random_bytes); ++index) {
        if (random_bytes[index] != 0) {
            any_random = 1;
        }
    }
    if (!any_random) {
        fail(61);
    }
}

static void test_signals(void)
{
    struct sigaction action = { signal_handler, SA_RESTART, 0, 0 };
    sigset_t current_mask;

    signal_seen = 0;
    if (sigprocmask(SIG_SETMASK, 0, &current_mask) != 0 ||
        sigaction(SIGTERM, &action, 0) != 0 ||
        kill(getpid(), SIGTERM) != 0 || signal_seen != SIGTERM) {
        fail(35);
    }
}

static void test_events_and_ioctl(void)
{
    struct epoll_event registration = { EPOLLIN, { .u64 = 0x1234 } };
    struct epoll_event event;
    u64 value = 1;
    i32 event_fd = eventfd(0, EFD_CLOEXEC);
    i32 epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    i32 null_fd;

    if (event_fd < 0 || epoll_fd < 0 ||
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &registration) != 0 ||
        write(event_fd, &value, sizeof(value)) != (ssize_t)sizeof(value) ||
        epoll_wait(epoll_fd, &event, 1, 0) != 1 ||
        event.data.u64 != registration.data.u64 ||
        close(epoll_fd) != 0 || close(event_fd) != 0) {
        fail(36);
    }

    null_fd = open("/dev/null", O_RDONLY | O_CLOEXEC, 0);
    errno = 0;
    if (null_fd < 0 || ioctl(null_fd, 0, 0) != -1 || errno != ENOTTY ||
        close(null_fd) != 0) {
        fail(37);
    }
}

static void test_socket_api(void)
{
    static const char socket_path[] = "/tmp/punos-wrapper-socket";
    static const char payload[] = "socket";
    static const char descriptor_payload[] = "fd";
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    char buffer[sizeof(payload)] = { 0 };
    char descriptor_buffer[sizeof(descriptor_payload)] = { 0 };
    struct iovec send_vector = {
        (void *)descriptor_payload, sizeof(descriptor_payload)
    };
    struct iovec receive_vector = {
        descriptor_buffer, sizeof(descriptor_buffer)
    };
    union ControlBuffer {
        struct cmsghdr alignment;
        u8 bytes[24];
    } send_control = { { 0, 0, 0 } }, receive_control = { { 0, 0, 0 } };
    struct msghdr send_message = {
        0, 0, &send_vector, 1, send_control.bytes, sizeof(send_control), 0
    };
    struct msghdr receive_message = {
        0, 0, &receive_vector, 1, receive_control.bytes,
        sizeof(receive_control), 0
    };
    struct cmsghdr *send_header = (struct cmsghdr *)send_control.bytes;
    struct cmsghdr *receive_header;
    struct stat descriptor_info;
    socklen_t address_length;
    socklen_t option_length;
    i32 option_value = 1;
    i32 server;
    i32 client;
    i32 peer;
    i32 second_client;
    i32 second_peer;
    i32 shared_fd;
    i32 queued_fd;
    i32 received_fd;

    for (size_t index = 0; index < sizeof(socket_path); ++index) {
        address.sun_path[index] = socket_path[index];
    }
    address_length = (socklen_t)(sizeof(address.sun_family) + sizeof(socket_path));
    (void)unlink(socket_path);

    server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0 && (errno == EPERM || errno == EACCES)) {
        return;
    }
    if (server < 0) {
        fail(38);
    }
    errno = 0;
    if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &option_value,
                   sizeof(option_value)) != 0 && errno <= 0) {
        fail(47);
    }
    if (bind(server, (const struct sockaddr *)&address, address_length) != 0) {
        if (errno == EPERM || errno == EACCES) {
            (void)close(server);
            (void)unlink(socket_path);
            return;
        }
        fail(48);
    }
    if (listen(server, 1) != 0) {
        fail(49);
    }

    client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (client < 0 ||
        connect(client, (const struct sockaddr *)&address, address_length) != 0) {
        fail(39);
    }
    peer = accept4(server, 0, 0, SOCK_CLOEXEC);
    if (peer < 0 || send(client, payload, sizeof(payload), MSG_NOSIGNAL) !=
                        (ssize_t)sizeof(payload) ||
        recv(peer, buffer, sizeof(buffer), 0) != (ssize_t)sizeof(buffer)) {
        fail(40);
    }
    for (size_t index = 0; index < sizeof(payload); ++index) {
        if (buffer[index] != payload[index]) {
            fail(41);
        }
    }

    shared_fd = memfd_create("punos-wrapper-fd", MFD_CLOEXEC);
    queued_fd = fcntl(shared_fd, F_DUPFD_CLOEXEC, 0);
    send_header->cmsg_len = sizeof(struct cmsghdr) + sizeof(i32);
    send_header->cmsg_level = SOL_SOCKET;
    send_header->cmsg_type = SCM_RIGHTS;
    *(i32 *)(send_control.bytes + sizeof(struct cmsghdr)) = queued_fd;
    if (shared_fd < 0 || queued_fd < 0 ||
        ftruncate(shared_fd, PAGE_SIZE) != 0 ||
        sendmsg(client, &send_message, MSG_NOSIGNAL) !=
            (ssize_t)sizeof(descriptor_payload) || close(queued_fd) != 0 ||
        recvmsg(peer, &receive_message, MSG_CMSG_CLOEXEC) !=
            (ssize_t)sizeof(descriptor_buffer)) {
        fail(56);
    }
    receive_header = (struct cmsghdr *)receive_control.bytes;
    received_fd = *(i32 *)(receive_control.bytes + sizeof(struct cmsghdr));
    if (descriptor_buffer[0] != descriptor_payload[0] ||
        descriptor_buffer[1] != descriptor_payload[1] ||
        descriptor_buffer[2] != descriptor_payload[2] ||
        receive_header->cmsg_len != sizeof(struct cmsghdr) + sizeof(i32) ||
        receive_header->cmsg_level != SOL_SOCKET ||
        receive_header->cmsg_type != SCM_RIGHTS || received_fd < 0 ||
        fstat(received_fd, &descriptor_info) != 0 ||
        descriptor_info.st_size != PAGE_SIZE || close(received_fd) != 0 ||
        close(shared_fd) != 0) {
        fail(57);
    }

    option_length = sizeof(option_value);
    if (getsockopt(client, SOL_SOCKET, SO_ERROR, &option_value,
                   &option_length) != 0 || option_value != 0 ||
        shutdown(client, SHUT_RDWR) != 0 || close(peer) != 0 ||
        close(client) != 0) {
        fail(42);
    }

    second_client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (second_client < 0 ||
        connect(second_client, (const struct sockaddr *)&address,
                address_length) != 0) {
        fail(54);
    }
    second_peer = accept(server, 0, 0);
    if (second_peer < 0 || close(second_peer) != 0 ||
        close(second_client) != 0 || close(server) != 0 ||
        unlink(socket_path) != 0) {
        fail(55);
    }
}

static void test_thread_api(void)
{
    struct RuntimeThreadState child_state;
    i32 child_tid = 0;
    u32 futex_word = 1;
    void *stack = mmap(0, THREAD_STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    u64 flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND |
                CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS |
                CLONE_PARENT_SETTID | CLONE_CHILD_SETTID |
                CLONE_CHILD_CLEARTID;
    pid_t result;
    i32 observed_tid;

    if (stack == MAP_FAILED) {
        fail(43);
    }
    child_state.self = &child_state;
    child_state.errno_value = 0;
    child_state.reserved = 0;
    thread_result = 0;
    result = clone(thread_entry, (u8 *)stack + THREAD_STACK_SIZE, flags,
                   &child_tid, &child_tid, &child_state, &child_tid);
    if (result <= 0) {
        fail(44);
    }

    if (futex((u32 *)&child_tid, FUTEX_WAKE, 1, 0, 0, 0) < 0) {
        fail(45);
    }
    errno = 0;
    if (futex(&futex_word, FUTEX_WAIT, 0, 0, 0, 0) != -1 ||
        errno != EAGAIN) {
        fail(56);
    }
    while ((observed_tid = child_tid) != 0) {
        errno = 0;
        if (futex((u32 *)&child_tid, FUTEX_WAIT, (u32)observed_tid,
                  0, 0, 0) == -1 && errno != EAGAIN && errno != EINTR) {
            fail(51);
        }
    }
    if (thread_result != 1) {
        fail(50 - thread_result);
    }
    if (munmap(stack, THREAD_STACK_SIZE) != 0) {
        fail(45);
    }
}

_Noreturn void _start(void)
{
    static const char message[] = "full syscall wrappers runtime test passed\n";
    static struct RuntimeThreadState state;

    state.self = &state;
    state.errno_value = 0;
    state.reserved = 0;
    if (__punos_raw_arch_set_fs(&state) != 0) {
        __punos_raw_exit_group(29);
    }

    test_files();
    test_memory_and_time();
    test_shared_memory_and_random();
    test_signals();
    test_events_and_ioctl();
    test_socket_api();
    test_thread_api();

    if (write(STDOUT_FILENO, message, sizeof(message) - 1) !=
        (ssize_t)(sizeof(message) - 1)) {
        fail(46);
    }
    exit(0);
}
