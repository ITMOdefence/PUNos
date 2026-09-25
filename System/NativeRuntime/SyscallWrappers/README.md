# Syscall Wrappers

Freestanding Linux x86_64 syscall layer for native PUNos components.

The complete v1 implementation contains:

- public ABI types and constants in `include/punos/syscall`;
- internal raw syscall entry points for zero through six arguments;
- internal syscall numbers fixed against Linux 6.12.111 x86_64;
- bootstrap FS-base setup for CRT;
- `RuntimeThreadState`, thread-local `errno` and raw-result conversion;
- every public wrapper specified in `DevDoc/NativeRuntime/Syscall Wrappers.md`;
- signal-return and clone assembly trampolines;
- compile-time ABI layout checks;
- freestanding raw, public API and full integration runtime tests.

The full integration test is also packaged as `syscall-selftest` in the
development initramfs and executed inside QEMU.

Run the compile checks with:

```bash
make -C System/NativeRuntime/SyscallWrappers check
```

The shared internal runtime state is declared in
`System/NativeRuntime/include/runtime/thread-state.h`. CRT and `libthread`
must use that declaration instead of defining local copies.
