## Описание

Системная библиотека для выделения, освобождения и обработки блоков памяти.
## Имя библиотеки
`sys/mem`
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

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| ------------ | ------------- | ------------------- | ---------- |
| `MemoryBlock` | Структура: `u8 *data`, `usize size` | Блок, созданный `memory_allocate()` | Владеющий объект выделенной памяти |
| `MemoryProtection` | Битовая маска `u32` | Комбинация флагов `MEM_READ`, `MEM_WRITE`, `MEM_EXECUTE` | Права доступа к блоку памяти |
| `MemoryCompareResult` | Перечисление | `Less`, `Equal`, `Greater` | Результат лексикографического сравнения двух блоков байтов |

## Константы

| Наименование | Назначение |
| ------------ | ---------- |
| `MEM_NONE` | Запрещает доступ к блоку памяти |
| `MEM_READ` | Разрешает чтение блока памяти |
| `MEM_WRITE` | Разрешает запись в блок памяти |
| `MEM_EXECUTE` | Разрешает выполнение кода из блока памяти |

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
