#include <punos/syscall/syscall.h>

static const struct timespec poll_delay = { .tv_sec = 1, .tv_nsec = 0 };

static size_t text_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void console_text(const char *text)
{
    i32 console = open("/dev/console", O_WRONLY, 0);
    if (console >= 0) {
        (void)write(console, text, text_length(text));
        (void)close(console);
        return;
    }
    (void)write(STDERR_FILENO, text, text_length(text));
}

static void console_error(const char *stage, i32 error)
{
    char digits[12];
    size_t count = 0;
    u32 value = (u32)error;

    console_text("PUNos init: ");
    console_text(stage);
    console_text(" failed, errno=");
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        --count;
        console_text((char[2]){ digits[count], '\0' });
    }
    console_text("\n");
}

_Noreturn static void failed(const char *stage, i32 error)
{
    console_error(stage, error);
    for (;;) {
        (void)nanosleep(&poll_delay, 0);
    }
}

static void required_mount(const char *source, const char *target,
                           const char *filesystem, u64 flags)
{
    if (mount(source, target, filesystem, flags, 0) != 0) {
        failed(target, errno);
    }
}

static int persistent_data_requested(void)
{
    static const char option[] = "punos.data=/dev/vda";
    char command_line[4096];
    i32 file = open("/proc/cmdline", O_RDONLY, 0);
    if (file < 0) failed("open kernel command line", errno);
    ssize_t count;
    do {
        count = read(file, command_line, sizeof(command_line) - 1);
    } while (count < 0 && errno == EINTR);
    i32 error = errno;
    (void)close(file);
    if (count < 0) failed("read kernel command line", error);
    command_line[count] = '\0';
    for (size_t start = 0; start < (size_t)count; ++start) {
        if (start != 0 && command_line[start - 1] != ' ') continue;
        size_t index = 0;
        while (option[index] != '\0' && command_line[start + index] == option[index]) ++index;
        if (option[index] == '\0' &&
            (command_line[start + index] == ' ' || command_line[start + index] == '\n' ||
             command_line[start + index] == '\0')) return 1;
    }
    return 0;
}

static void mount_persistent_data(void)
{
    if (!persistent_data_requested()) return;
    if (mkdir("/data", 0755) != 0 && errno != EEXIST) failed("create /data", errno);
    required_mount("/dev/vda", "/data", "ext4", MS_NOSUID | MS_NODEV);
    if (mkdir("/data/apps", 0755) != 0 && errno != EEXIST) failed("create /data/apps", errno);
    console_text("PUNos init: persistent data ready\n");
}

int main(int argc, char **argv, char **envp)
{
    pid_t initserv_pid;
    char *const initserv_argv[] = { (char *)"/punos/bin/initserv", 0 };
    char *const empty_env[] = { 0 };

    (void)argc;
    (void)argv;
    (void)envp;

    console_text("PUNos init: userspace started\n");
    if (getpid() != 1) {
        failed("PID check", EINVAL);
    }

    required_mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID);
    required_mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC);
    required_mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC);
    mount_persistent_data();
    required_mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV);
    if (mkdir("/run/punos", 0755) != 0 && errno != EEXIST) {
        failed("/run/punos", errno);
    }
    if (mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, 0) != 0) {
        console_error("/tmp", errno);
    }

    console_text("PUNos init: filesystems ready\n");
#ifdef PUNOS_SMOKE_BOOT
    pid_t smoke = fork();
    if (smoke == 0) {
        char *const args[] = { (char *)"/punos/bin/initserv-smoke", 0 };
        (void)execve(args[0], args, empty_env); exit(126);
    }
    i32 smoke_status = 0;
    if (smoke < 0 || waitpid(smoke, &smoke_status, 0) != smoke || smoke_status != 0)
        failed("smoke tests", EIO);
#endif

    initserv_pid = fork();
    if (initserv_pid < 0) {
        failed("fork initserv", errno);
    }
    if (initserv_pid == 0) {
        (void)execve("/punos/bin/initserv", initserv_argv, empty_env);
        console_error("exec initserv", errno);
        exit(126);
    }

    console_text("PUNos init: initserv started\n");
#ifdef PUNOS_SMOKE_BOOT
    pid_t probe = fork();
    if (probe == 0) {
        char *const args[] = { (char *)"/punos/bin/services-bootcheck", 0 };
        (void)execve(args[0], args, empty_env); exit(126);
    }
    i32 probe_status = 0;
    if (probe < 0 || waitpid(probe, &probe_status, 0) != probe || probe_status != 0)
        failed("services probe", EIO);
#endif

    for (;;) {
        i32 status = 0;
        pid_t child;
        do {
            child = waitpid(-1, &status, WNOHANG);
            if (child == initserv_pid) {
                failed("initserv exited", ECHILD);
            }
        } while (child > 0);

        if (child < 0 && errno != ECHILD && errno != EINTR) {
            failed("waitpid", errno);
        }
        (void)nanosleep(&poll_delay, 0);
    }
}
