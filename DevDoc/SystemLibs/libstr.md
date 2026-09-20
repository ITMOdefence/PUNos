## Описание

Системная библиотека для создания, хранения и обработки UTF-8 строк.
## Имя библиотеки
`sys/str`
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

## Типы

| Наименование | Представление | Допустимые значения | Назначение |
| ------------ | ------------- | ------------------- | ---------- |
| `Bytes` | Тип из `libcore`: `u8 *data`, `usize size` | Блок байтов длиной от `0` | Исходные данные для создания строки |
| `String` | Структура: `char *data`, `usize length` | Корректная UTF-8 строка без внутреннего `NUL` | Владеющий строковый объект |
| `StringCompareResult` | Перечисление | `Less`, `Equal`, `Greater` | Результат лексикографического сравнения строк |

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
