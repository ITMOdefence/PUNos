# CRT

Minimal freestanding C runtime for native PUNos executables on Linux x86_64.

It provides `_start`, initializes the main `RuntimeThreadState`, installs the
FS base, calls `main(argc, argv, envp)` and terminates through the public PUNos
`exit()` wrapper.

Build and run the minimal program test from the repository root:

```bash
make -C System/NativeRuntime/CRT check
```

The reusable outputs are:

```text
System/NativeRuntime/CRT/build/crt0.o
System/NativeRuntime/CRT/build/libpunos_crt.a
```
