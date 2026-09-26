# libstr

System Lib for UTF-8 strings (`sys/string`): an owned, immutable `String`
type, validation of UTF-8 input, copying, concatenation and comparison.

Specification: `DevDoc/SystemLibs/libstr.md`. Depends on
[libcore](../libcore/README.md) and [libmem](../libmem/README.md).

## Build and test

From the repository root:

```bash
make -C System/SystemLibs/libstr check
```

Outputs:

```text
System/SystemLibs/libstr/build/libpunos_str.a     # the library
System/SystemLibs/libstr/build/string_runtime     # runtime test / QEMU self-test
```

## Usage

```c
#include <sys/string.h>

ErrorCode greet(const String *name, String *out_greeting)
{
    String prefix;
    ErrorCode result = string_from_c_str("Hello, ", &prefix);

    if (result != CORE_OK) {
        return result;
    }
    result = string_concat(&prefix, name, out_greeting);
    (void)string_destroy(&prefix);
    return result;
}
```

Compile with the `include` directories of libstr, libmem, libcore and
Syscall Wrappers. Link order:

```text
crt0.o <objects> libpunos_str.a libpunos_mem.a libpunos_core.a libpunos_crt.a libpunos_syscall.a
```

## What a String guarantees

Every `String` that a `string_*` operation returns:

- holds `length` bytes of well-formed UTF-8 with no `NUL` among them;
- has a `NUL` at `data[length]`, so `data` is also a valid C string;
- is at most `STRING_MAX_LENGTH` (1 MiB) bytes long.

UTF-8 is validated once, when the string is created (`string_from_utf8`,
`string_from_c_str`). Copying and concatenating valid strings always gives a
valid string, so these operations do not validate again. Operations that take
a `String` only do cheap checks (`data` is not `NULL`, the length is within
the limit, `data[length]` is `NUL`), which catch uninitialised and destroyed
strings but not every forged one.

Strings are immutable: no operation changes an existing string. To "modify" a
string, create a new one and destroy the old.

## UTF-8 validation

Validation follows RFC 3629 (Unicode table 3-7). Rejected:

| Input | Example |
| --- | --- |
| `NUL` byte anywhere | `a\0b` |
| continuation byte without a lead byte | `80` |
| overlong encoding | `C0 80` (NUL), `E0 9F BF`, `F0 8F BF BF` |
| UTF-16 surrogates U+D800…U+DFFF | `ED A0 80` |
| code points above U+10FFFF | `F4 90 80 80`, leads `F5`…`FF` |
| truncated sequence | `E2 82` at the end of the input |
| bad continuation byte | `C3 28` |

## Implementation decisions

- **Header name.** The header is `<sys/string.h>`, as in
  `DevDoc/SystemLibs/_LibsList.md` and the consumers' specifications (the
  library page used to say `sys/str`).
- **Memory.** A non-empty string is one libmem block of `length + 1` bytes.
  Because libmem maps whole pages, **every non-empty string costs at least
  4 KiB**. This follows from the contracts (memory only from libmem, libmem
  only from `mmap`) and is fine for paths, names and messages, but code that
  keeps thousands of strings alive should know it. A smaller-grained allocator
  would be a change to libmem, not to libstr.
- **Empty strings take no memory.** All empty strings point to one shared,
  never-written `""` inside the library; `string_destroy()` recognises it and
  does not free it.
- **Length limit errors.** Input longer than `STRING_MAX_LENGTH` and a
  concatenation whose result would be longer both return `CORE_OVERFLOW`.
  `string_from_c_str()` reads at most `STRING_MAX_LENGTH + 1` bytes of its
  source, so a missing `NUL` cannot make it run through memory.
- **Invalid UTF-8** returns `CORE_INVALID_ARGUMENT`.
- **`string_destroy()` sets the string to `{ NULL, 0 }`**; destroying it again
  is a no-op. Any other operation rejects `{ NULL, 0 }` as not a valid string.
- **`string_concat()`** accepts the same string as both `left` and `right`.
  It writes a *new* string to `out_string` and does not destroy what was there:
  `string_concat(&a, &b, &a)` leaks the old `a`.
- **Comparison** is by unsigned bytes, which for UTF-8 is the same as by Unicode
  code points. There is no locale or normalisation: `é` as one code point and
  as `e` + combining accent are different strings.
- **Enum values** are `STRING_COMPARE_RESULT_LESS` (`-1`), `..._EQUAL` (`0`)
  and `..._GREATER` (`1`).
- **No syscalls.** `libpunos_str.a` refers only to `memory_*` functions: all
  memory goes through libmem, as the contract requires.

## Tests

- `tests/abi_compile.c`: layout of `String`, `STRING_MAX_LENGTH`, values of
  `StringCompareResult`.
- `tests/string_runtime.c`, check groups: `1–29` well-formed UTF-8 (including
  the first and last code point of every sequence length and around the
  surrogates), `30–59` malformed UTF-8, `60–79` length limit, `80–99`
  `string_from_c_str`, `100–119` destroy and copy, `120–149` concat,
  `150–169` compare and equals, `170–179` length and `c_str`.
  - That `string_from_c_str()` never reads past its limit is checked by
    placing the text right before an inaccessible page: one byte too many
    would kill the test with `SIGSEGV`.
  - Truncated sequences are also checked with a valid continuation byte just
    past the end of the input, so an out-of-bounds read cannot hide the bug.

The test was checked against 16 deliberately broken versions of `string.c`.
All 16 are caught. The first run caught 15; the missed one (no truncation
check) was hidden by the `NUL` after a string literal, which led to checks
55–57.
