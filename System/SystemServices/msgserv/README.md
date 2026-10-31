# msgserv: снапшот брокера в дереве PUNos

Это снапшот репозитория `ITMOdefence/brouk` на ревизии из `upstream.json`
(ветка `msgserv-implementation`). Исходники брокера, его тесты и генератор
прав и секретов лежат здесь так же, как `appserv`, `logserv` и `initserv`.

## Что откуда

- `include/`, `src/`, `tests/`, `tools/generate_messaging.py` — код снапшота.
- `config/messaging.json` — правила прав; единственный файл конфигурации
  msgserv, который живёт в этом дереве.
- Реестр сервисов и каталог приложений **не копируются**: генератор и сборка
  читают `System/SystemServices/platform_registry.json` и `applications.json`
  напрямую. Запись `msgserv` в реестре — часть интеграции PUNos.
- `secret.salt` создаётся при первой сборке и не коммитится (`.gitignore`
  корня PUNos). Новая соль обнуляет все выданные секреты.

## Сборка

Из корня PUNos или из этого каталога:

```sh
make -C System/SystemServices/msgserv all
make -C System/SystemServices/msgserv check
make -C System/SystemServices/msgserv runtime-test
```

## Как проверить обмен

Текущий контракт `msgserv` — обмен между приложениями одной системы PUNos
через локальный IPC (`AF_UNIX`). Сетевого транспорта между машинами или
подсетями в v1 нет. Поэтому проверка брокера — это несколько независимых
клиентов-приложений, подключённых к одному процессу `msgserv`:

- `make -C System/SystemServices/msgserv check-unit` проверяет маршрутизацию,
  подписки, права, `client_id`, корреляцию запросов и ответы, таймауты и
  отключения на настоящем `ipclib` и event loop.
- `make -C System/SystemServices/msgserv check-protocol` проверяет границы
  кадров и отклонение повреждённых или неподдерживаемых сообщений.
- `make -C System/SystemServices/msgserv check-faults` проверяет обработку
  отказов памяти и IPC.
- `make -C System/SystemServices/msgserv check-generator` проверяет каталог
  прав и генерацию секретов.
- `make -C System/SystemServices/msgserv check-integration` запускает настоящий
  процесс брокера с несколькими AF_UNIX-клиентами и проверяет публикацию,
  запрос/ответ, лимит ожидающих запросов, права, переполнение очередей и
  журналирование.
- `make -C System/ApplicationLayer/appframework check-system` включает
  `tests/system/messaging_integration.py`: несколько отдельных процессов
  `appframework` обмениваются событиями и запросами через реальный брокер;
  проверяются также переподключение и восстановление подписок после
  перезапуска `msgserv`.

Успешная публикация должна показать `delivered=2`, а оба подписчика должны
получить событие с ожидаемым topic и payload. Для request/response проверяются
`correlation_id`, status и payload; межлежащий event не должен приниматься за
ответ. Эти проверки доказывают взаимодействие приложений через брокер внутри
одного экземпляра PUNos. Для доказательства обмена между машинами потребуется
отдельный сетевой транспорт и отдельный сетевой интеграционный тест — текущая
реализация такого поведения не обещает.

`PUNOS_DIR` по умолчанию `../../..`, `REGISTRY` и `METADATA` — файлы
`System/SystemServices/`. Цель `setup` проверяет, что дерево PUNos на месте,
и заменяет прежнюю `vendor`: снапшот больше не подтягивается из сети.

## Обновление снапшота

Новую ревизию брокера переносим вручную тем же набором файлов, что перечислен
в `upstream.json`. Затем обновляем `commit` в `upstream.json` и прогоняем
`make check`. Правки в этом каталоге, не сделанные в `brouk`, должны попасть и
туда: снапшот не расходится с источником.
