#include <punos/syscall/syscall.h>

void *memcpy(void *destination, const void *source, size_t size);
void *memset(void *destination, int value, size_t size);

int main(int argc, char **argv, char **envp)
{
    static const char message[] = "minimal PUNos program reached main\n";
    static const u8 source[] = { 0x31, 0x42, 0x53, 0x64 };
    u8 copied[sizeof(source)];
    u8 cleared[sizeof(source)];

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
    if (memcpy(copied, source, sizeof(source)) != copied ||
        copied[0] != source[0] || copied[3] != source[3]) {
        return 103;
    }
    if (memset(cleared, 0xa5, sizeof(cleared)) != cleared ||
        cleared[0] != 0xa5 || cleared[3] != 0xa5) {
        return 104;
    }

    return 37;
}
