## Описание

Системная библиотека для создания, хранения и обработки UTF-8 строк.

## Текущий статус реализации

Контракт реализован полностью в `System/SystemLibs/libstr`. Готовы:

- публичный заголовок `<sys/string.h>` со всеми типами и константами;
- статическая библиотека `libpunos_str.a` со всеми операциями из таблицы ниже;
- проверка UTF-8 по RFC 3629;
- compile-time проверки ABI (`tests/abi_compile.c`);
- runtime-тест всех операций, корректного и некорректного UTF-8 и границы
  длины (`tests/string_runtime.c`), который выполняется на хосте командой
  `make -C System/SystemLibs/libstr check` и внутри PUNos в QEMU как
  `/punos/bin/libstr-selftest`.

Библиотека не делает системных вызовов и получает память только через
`memory_*` из `libmem` (контракты 1 и 2). Решения, принятые при реализации,
описаны в `System/SystemLibs/libstr/README.md`.

## Имя библиотеки
`sys/string`

Заголовок — `<sys/string.h>`, как в `_LibsList.md` и спецификациях
потребителей.
## Контракты

1. Библиотека использует типы и `ErrorCode` из `libcore`, а память выделяет и
   освобождает только через `libmem`.
2. Библиотека не использует Syscall Wrappers.
3. `String` владеет своим буфером памяти. Он должен быть освобождён ровно один
   раз через `string_destroy()`.
4. `String.data` содержит завершающий байт `NUL`, который не входит в
   `String.length`.
5. Каждая строка содержит корректную UTF-8 последовательность без `NUL` внутри
   строки.
6. `string_c_str()` возвращает невладеющий указатель, действительный, пока
   исходный `String` не изменён и не уничтожен.
7. Все операции возвращают `ErrorCode` из `libcore`.
8. Корректность UTF-8 определяется по RFC 3629: запрещены избыточно длинные
   (overlong) последовательности, суррогаты `U+D800` … `U+DFFF`, значения выше
   `U+10FFFF`, обрезанные последовательности и одиночные байты продолжения.
   Некорректный UTF-8 или `NUL` внутри данных дают `CORE_INVALID_ARGUMENT`.
9. Входные данные длиннее `STRING_MAX_LENGTH`, а также результат конкатенации
   длиннее `STRING_MAX_LENGTH` дают `CORE_OVERFLOW`. `string_from_c_str()`
   читает не более `STRING_MAX_LENGTH + 1` байтов источника.
10. UTF-8 проверяется при создании строки; операции, принимающие `String`,
    выполняют только дешёвые проверки инварианта (`data != NULL`, длина не
    больше предела, `data[length] == NUL`). Нарушение даёт
    `CORE_INVALID_ARGUMENT`, как и нулевые указатели в аргументах.
11. Строки неизменяемы. `string_destroy()` записывает в строку
    `{ data = NULL, length = 0 }`; повторное уничтожение ничего не делает.
    Другие операции считают такую строку недействительной.
12. `string_concat()` допускает одну и ту же строку в `left` и `right` и
    записывает в `out_string` новую строку, не уничтожая прежнее значение.
13. Сравнение выполняется по беззнаковым байтам, что для UTF-8 совпадает с
    порядком кодовых точек Unicode. Локаль и нормализация не применяются.
14. Непустая строка занимает отдельный блок `libmem`, то есть не меньше одной
    страницы (4 КиБ). Пустые строки память не занимают.

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| ------------ | ------------- | ------------------- | ---------- |
| `Bytes` | Тип из `libcore`: `u8 *data`, `usize size` | Блок байтов длиной от `0` | Исходные данные для создания строки |
| `String` | Структура: `char *data`, `usize length` | Корректная UTF-8 строка без внутреннего `NUL` | Владеющий строковый объект |
| `StringCompareResult` | Перечисление | `Less` (`-1`), `Equal` (`0`), `Greater` (`1`) | Результат лексикографического сравнения строк |

В C значения перечисления имеют префикс типа: `STRING_COMPARE_RESULT_LESS`,
`STRING_COMPARE_RESULT_EQUAL`, `STRING_COMPARE_RESULT_GREATER`.

## Константы

| Наименование | Назначение |
| ------------ | ---------- |
| `STRING_MAX_LENGTH` | Максимальная длина строки в байтах: `1048576` |

## Операции

| Операция | Аргументы | Возвращаемое значение | Назначение |
| -------- | --------- | --------------------- | ---------- |
| `string_from_utf8(bytes, out_string)` | `Bytes bytes`, `String *out_string` | `ErrorCode` | Проверяет UTF-8 данные и создаёт строку |
| `string_from_c_str(source, out_string)` | `const char *source`, `String *out_string` | `ErrorCode` | Создаёт строку из `NUL`-терминированной UTF-8 последовательности |
| `string_destroy(string)` | `String *string` | `ErrorCode` | Освобождает память строки через `libmem` и делает её недействительной |
| `string_copy(source, out_string)` | `const String *source`, `String *out_string` | `ErrorCode` | Создаёт независимую копию строки |
| `string_concat(left, right, out_string)` | `const String *left`, `const String *right`, `String *out_string` | `ErrorCode` | Создаёт строку из конкатенации двух строк |
| `string_length(string, out_length)` | `const String *string`, `usize *out_length` | `ErrorCode` | Возвращает длину строки в байтах без завершающего `NUL` |
| `string_compare(left, right, out_result)` | `const String *left`, `const String *right`, `StringCompareResult *out_result` | `ErrorCode` | Лексикографически сравнивает две строки |
| `string_equals(left, right, out_equal)` | `const String *left`, `const String *right`, `bool *out_equal` | `ErrorCode` | Проверяет равенство двух строк |
| `string_c_str(string, out_data)` | `const String *string`, `const char **out_data` | `ErrorCode` | Возвращает указатель на `NUL`-терминированные данные строки |
