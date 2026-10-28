# librandom

`librandom` exposes kernel-provided cryptographic random bytes through
`<sys/random.h>`. `random_fill()` retries interrupted and partial `getrandom()`
calls and succeeds only after the complete destination has been filled.

It is intended for capability tokens and identifiers that must not be
predictable. Time, PID and userspace pseudo-random generators are not valid
substitutes.

```bash
make -C System/SystemLibs/librandom check
```
