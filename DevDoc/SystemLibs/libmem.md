## Описание

Системная библиотека для выделения, освобождения и обработки блоков памяти.

## Текущий статус реализации

Контракт реализован полностью в `System/SystemLibs/libmem`. Готовы:

- публичный заголовок `<sys/memory.h>` со всеми типами и константами;
- статическая библиотека `libpunos_mem.a` со всеми операциями из таблицы ниже;
- compile-time проверки ABI (`tests/abi_compile.c`);
- runtime-тест всех операций и ошибочных путей (`tests/memory_runtime.c`),
  который выполняется на хосте командой
  `make -C System/SystemLibs/libmem check` и внутри PUNos в QEMU как
  `/punos/bin/libmem-selftest`.

Библиотека обращается к ядру только через `mmap()`, `munmap()` и `mprotect()`
(контракт 1) и не содержит вызовов `memcpy()`/`memset()`. Устройство и
решения, принятые при реализации, описаны в
`System/SystemLibs/libmem/README.md`.

## Имя библиотеки
`sys/memory`

Заголовок — `<sys/memory.h>`, как в `_LibsList.md` и спецификациях
потребителей.
## Контракты

1. Библиотека использует только Syscall Wrappers `mmap()`, `munmap()` и
   `mprotect()`.
2. Все операции возвращают `ErrorCode` из `libcore`.
3. `MemoryBlock`, созданный `memory_allocate()`, должен быть освобождён ровно
   один раз через `memory_free()`.
4. После `memory_free()` блок становится недействительным и не используется.
5. `memory_allocate()` при ненулевом размере возвращает блок, заполненный
   нулевыми байтами. При `size = 0` она возвращает пустой блок
   `{ data = NULL, size = 0 }` без вызова `mmap()`.
6. `memory_reallocate()` сохраняет первые `min(old_size, new_size)` байтов
   исходного блока. При ошибке исходный блок остаётся действительным.
7. `memory_copy()` не допускает перекрытия исходной и целевой областей;
   `memory_move()` допускает.
8. Операции с блоком нулевой длины допустимы; его указатель не разыменовывается.
9. `memory_reallocate(block, 0)` освобождает прежний блок и записывает в него
   `{ data = NULL, size = 0 }`.
10. Каждый непустой блок — отдельное анонимное отображение `mmap()`,
    округлённое вверх до целых страниц по 4 КиБ. Блоки не делят страницы,
    поэтому `memory_protect()` не затрагивает соседние блоки. Непустой блок
    всегда начинается на границе страницы; структура, не удовлетворяющая этому
    условию или несогласованная (`data == NULL` при ненулевом `size` и
    наоборот), отклоняется с `CORE_INVALID_ARGUMENT`.
11. Байты, добавленные при увеличении блока через `memory_reallocate()`,
    равны нулю.
12. После успешного `memory_reallocate()` с ненулевым размером блок имеет права
    `MEM_READ | MEM_WRITE` независимо от прежних прав.
13. `memory_free()` записывает в блок `{ data = NULL, size = 0 }`; освобождение
    пустого блока ничего не делает и возвращает `CORE_OK`.
14. Перекрытие в `memory_copy()` проверяется для реально записываемой области:
    `source` и первых `source.size` байтов `destination`. При перекрытии
    возвращается `CORE_INVALID_ARGUMENT`.
15. `Bytes` с `data == NULL` и ненулевым размером, а также `Bytes`, выходящий
    за конец адресного пространства, отклоняются с `CORE_INVALID_ARGUMENT`.
    Нулевые указатели `block`, `out_block` и `out_result` также дают
    `CORE_INVALID_ARGUMENT`.
16. Размер, который нельзя отобразить (не округляется до целых страниц или
    превышает доступное адресное пространство), даёт `CORE_OUT_OF_MEMORY`.

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| ------------ | ------------- | ------------------- | ---------- |
| `MemoryBlock` | Структура: `u8 *data`, `usize size` | Блок, созданный `memory_allocate()` | Владеющий объект выделенной памяти |
| `MemoryProtection` | Битовая маска `u32` | Комбинация флагов `MEM_READ`, `MEM_WRITE`, `MEM_EXECUTE` | Права доступа к блоку памяти |
| `MemoryCompareResult` | Перечисление | `Less` (`-1`), `Equal` (`0`), `Greater` (`1`) | Результат лексикографического сравнения двух блоков байтов (байты сравниваются как беззнаковые; более короткий префикс меньше) |

В C значения перечисления имеют префикс типа: `MEMORY_COMPARE_RESULT_LESS`,
`MEMORY_COMPARE_RESULT_EQUAL`, `MEMORY_COMPARE_RESULT_GREATER` (правило
именования описано в `System/SystemLibs/README.md`).

## Константы

| Наименование | Назначение |
| ------------ | ---------- |
| `MEM_NONE` | Запрещает доступ к блоку памяти (`0`) |
| `MEM_READ` | Разрешает чтение блока памяти (`1`) |
| `MEM_WRITE` | Разрешает запись в блок памяти (`2`) |
| `MEM_EXECUTE` | Разрешает выполнение кода из блока памяти (`4`) |

Другие биты в `MemoryProtection` недопустимы и дают `CORE_INVALID_ARGUMENT`.
На x86_64 `MEM_WRITE` и `MEM_EXECUTE` также разрешают чтение.

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| -------- | --------- | --------------------- | ---------- |
| `memory_allocate(size, out_block)` | `usize size`, `MemoryBlock *out_block` | `ErrorCode` | Выделяет анонимный блок памяти через `mmap()`; при нулевом размере возвращает пустой блок без syscall |
| `memory_reallocate(block, new_size)` | `MemoryBlock *block`, `usize new_size` | `ErrorCode` | Изменяет размер блока, сохраняя его данные |
| `memory_free(block)` | `MemoryBlock *block` | `ErrorCode` | Освобождает блок памяти через `munmap()` и делает его недействительным |
| `memory_protect(block, protection)` | `MemoryBlock *block`, `MemoryProtection protection` | `ErrorCode` | Изменяет права доступа к блоку через `mprotect()` |
| `memory_copy(destination, source)` | `Bytes destination`, `Bytes source` | `ErrorCode` | Копирует байты без перекрытия областей; размер `source` не должен превышать размер `destination` |
| `memory_move(destination, source)` | `Bytes destination`, `Bytes source` | `ErrorCode` | Копирует байты с допустимым перекрытием областей |
| `memory_set(destination, value)` | `Bytes destination`, `u8 value` | `ErrorCode` | Заполняет весь блок указанным байтом |
| `memory_compare(left, right, out_result)` | `Bytes left`, `Bytes right`, `MemoryCompareResult *out_result` | `ErrorCode` | Лексикографически сравнивает два блока байтов |
