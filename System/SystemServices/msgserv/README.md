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

`PUNOS_DIR` по умолчанию `../../..`, `REGISTRY` и `METADATA` — файлы
`System/SystemServices/`. Цель `setup` проверяет, что дерево PUNos на месте,
и заменяет прежнюю `vendor`: снапшот больше не подтягивается из сети.

## Обновление снапшота

Новую ревизию брокера переносим вручную тем же набором файлов, что перечислен
в `upstream.json`. Затем обновляем `commit` в `upstream.json` и прогоняем
`make check`. Правки в этом каталоге, не сделанные в `brouk`, должны попасть и
туда: снапшот не расходится с источником.
