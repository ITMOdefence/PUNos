#include <punos/syscall/syscall.h>

int main(int argc, char **argv, char **envp)
{
    static const char message[] = "minimal PUNos program reached main\n";

    if (argc < 1 || argv == 0 || argv[0] == 0 || argv[argc] != 0 ||
        envp == 0) {
        return 100;
    }

    if (sys_errno_location() == 0 || errno != 0) {
        return 101;
    }

    if (write(STDOUT_FILENO, message, sizeof(message) - 1) !=
        (ssize_t)(sizeof(message) - 1)) {
        return 102;
    }

    return 37;
}
