[Read in English](README.md)

# DeltaFeedback

Self-hosted форма обратной связи для персонального сайта. Visitor отправляет
сообщение через лёгкую HTML-форму; вы получаете его в Delta Chat и
отвечаете обычной функцией «reply». Ответ появляется на странице тикета у
visitor'а в реальном времени.

- Один C++ бинарник, один файл SQLite.
- POW-капча (SHA-256 hashcash, по умолчанию 18 бит) + honeypot + проверка
  времени заполнения формы.
- Приветственный HTML-блок per locale, переключатель языка по браузеру (ru
  / en).
- Один админ: первый, кто написал боту, становится админом; остальные —
  молча блокируются.
- Тикеты удаляются через 7 дней после последней активности (с любой
  стороны).
- Ответы админа маршрутизируются через DC «reply» (цитата) — `[ID]`
  печатать не нужно; fallback на `[ID]` сохранён для кросс-девайс случаев.

Лицензия: **GPLv3**.

## Зависимости

- C++17 (g++ или clang)
- CMake ≥ 3.16
- `libsqlite3-dev`, `libssl-dev`, `libasio-dev`
- `libdeltachat` из
  [deltachat-core-rust](https://github.com/chatmail/core), собрана заранее

CrowCpp 1.2.0 и gtest подтягиваются CMake'ом автоматически.

## Сборка

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build
```

`CMakeLists.txt` ищет `libdeltachat.so` по пути
`../deltachatbot-cpp/deltachat-core-rust/target/release/`. Если в другом
месте — задайте `-DDC_CORE=/path/to/deltachat-core-rust`.

## Настройка и запуск

```bash
cp config.example.ini config.ini
./build/deltafeedback --register <chatmail-domain> config.ini
./build/deltafeedback --run config.ini
```

`--register` создаёт новую chatmail-учётку и пишет креды в `config.ini`.
`--run` стартует DC event loop и HTTP-сервер (по умолчанию
`0.0.0.0:8080`). На первом запуске бот печатает Delta Chat invite URL —
откройте его в своём DC клиенте, чтобы добавить бота; ваш аккаунт станет
админом.

Если `config` опущен — используется `./config.ini`.

## Сброс админа

```bash
./build/deltafeedback --reset-admin config.ini
```

Очищает owner contact id, удаляет все тикеты и POW-состояние. Следующий,
кто напишет боту, станет новым админом.

## За reverse proxy

IP visitor'а берётся из `X-Forwarded-For` (первый), потом `X-Real-IP`, в
последнюю очередь — connecting peer. В продакшене биндитесь на
`127.0.0.1`, TLS заворачивайте на caddy/nginx.

## Debian-пакет

Готовые `.deb` для Debian 12 (bookworm) и 13 (trixie) собираются GitHub
Actions на каждый push в `main` — workflow в `.github/workflows/deb.yml`.
Релизы (теги `v*`) автоматически прикладывают артефакты в GitHub Release.

Локальная сборка пакета:

```bash
cmake -S . -B build && cmake --build build -j
./packaging/build-deb.sh   # → dist/deltafeedback_<ver>_<codename>_<arch>.deb
```

Что устанавливает пакет:

| Путь                                    | Назначение                                |
|-----------------------------------------|-------------------------------------------|
| `/usr/bin/deltafeedback`                | бинарник                                  |
| `/usr/lib/deltafeedback/libdeltachat.so`| зашитая DC core (rpath прописан)          |
| `/usr/share/deltafeedback/web/`         | фронтэнд — **перезатирается** на обновлении |
| `/etc/deltafeedback/config.example.ini` | reference-конфиг — перезатирается         |
| `/etc/deltafeedback/config.ini`         | рабочий конфиг — **никогда** не перезатирается |
| `/lib/systemd/system/deltafeedback.service` | systemd unit                          |
| `/var/lib/deltafeedback/`               | SQLite + DC БД (создаются при установке)  |

После установки:

```bash
sudo -u deltafeedback deltafeedback --register <chatmail-domain> /etc/deltafeedback/config.ini
sudo systemctl enable --now deltafeedback.service
```

## Кастомизация приветственного блока

Редактируйте `web/welcome.ru.html` и `web/welcome.en.html`. Чистый HTML;
пустой файл скрывает блок. Перезагружается при переключении языка.
