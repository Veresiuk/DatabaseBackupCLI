# DatabaseBackupCLI

Консольна утиліта на C++ для резервного копіювання та відновлення PostgreSQL, MySQL і MongoDB. PostgreSQL також зберігає конфігурацію backup-завдань та їхню історію через ODB. СУБД запускаються в Docker Compose.

## Можливості

- Перегляд завдань (`list-jobs`) і історії резервного копіювання (`history [job-id]`).
- Повні логічні копії PostgreSQL (`.dump`), MySQL (`.sql.gz`) і MongoDB (`.archive.gz`) за завданням або всіх активних завдань.
- Відновлення логічної копії PostgreSQL, MySQL або MongoDB.
- Фізичні повні та інкрементальні копії PostgreSQL-кластера через `pg_basebackup`.
- Об’єднання повного та інкрементального ланцюжка, перевірка копії й тестове відновлення в тимчасовому PostgreSQL.
- Retention для кластерних копій: dry-run за замовчуванням; автоматичне очищення зберігає чотири повні ланцюжки та найновіший ланцюжок.
- Windows Task Scheduler через `scripts/run_backup.ps1`, журнал у `D:\DatabaseBackups\scheduler.log` та необов’язкові Slack-сповіщення.

Логічне відновлення і backup-и всіх трьох СУБД перевірені на тестових базах. Кластерне інкрементальне відновлення перевірене запуском тимчасового PostgreSQL: у тестовій базі знайдено три записи `backup_jobs`.

## Вимоги

- Windows, PowerShell і Docker Desktop із Docker Compose.
- Build2.
- ODB, ODB PostgreSQL runtime та бібліотеки PostgreSQL, потрібні для збирання і запуску CLI.
- Вільне місце для копій у `D:/DatabaseBackups`.

Шляхи до Build2 та ODB у buildfile і PowerShell-сценарії налаштовані для локального середовища розробки. На іншому комп’ютері їх потрібно змінити.

## Налаштування

Запусти сервіси:

```powershell
docker compose up -d db mysql mongo
```

CLI підключається до PostgreSQL-контрольної бази. Задай для свого Windows-користувача змінні середовища:

- `DB_HOST` — адреса PostgreSQL, зазвичай `127.0.0.1`.
- `DB_PORT` — порт PostgreSQL, зазвичай `5432`.
- `DB_NAME` — контрольна база, наприклад `backup_demo`.
- `DB_USER` і `DB_PASSWORD` — облікові дані PostgreSQL.
- `MYSQL_BACKUP_DATABASE` і `MONGO_BACKUP_DATABASE` — бази для відповідних логічних backup-завдань.
- `SLACK_WEBHOOK_URL` — необов’язково; Slack Incoming Webhook для повідомлень про результат.

`compose.yaml` містить локальні облікові дані для розробки. Заміни їх перед використанням у спільному чи production-середовищі.

## Збірка

Якщо шлях до проєкту містить кирилицю, у цьому середовищі використовуй SUBST-диск:

```powershell
subst R: "D:\Робочий стіл\Projects"
Set-Location R:\DatabaseBackupCLI
$build2Bin = "D:\build2\build2-toolchain-0.18.1-x86_64-windows10-mingw-gcc14\bin"
$env:Path = "$build2Bin;$env:Path"
b src/@build-output/
```

Виконуваний файл: `build-output\DatabaseBackupCLI\DatabaseBackupCLI.exe`.
Для ручного запуску CLI також мають бути доступні DLL-каталоги ODB і PostgreSQL у `PATH`; `scripts/run_backup.ps1` налаштовує їх автоматично.

## Команди CLI

Запускай команди з кореня проєкту:

```powershell
$exe = ".\build-output\DatabaseBackupCLI\DatabaseBackupCLI.exe"

& $exe --help
& $exe check-db
& $exe list-jobs
& $exe backup 1
& $exe backup-all
& $exe history
& $exe history 1
```

Відновлення логічних копій: шлях указується відносно `D:/DatabaseBackups`; цільова база має існувати заздалегідь:

```powershell
& $exe restore PostgreSQL "postgres/backup.dump" restore_test
& $exe restore MySQL "mysql/backup.sql.gz" mysql_restore_test
& $exe restore MongoDB "mongodb/backup.archive.gz" mongo_restore_test
```

Кластерні копії PostgreSQL:

```powershell
& $exe backup-cluster full
& $exe backup-cluster incremental
& $exe combine-cluster incremental_YYYYMMDDTHHMMSSZ
& $exe test-cluster-restore incremental_YYYYMMDDTHHMMSSZ
& $exe prune-cluster 4
```

`test-cluster-restore` об’єднує потрібний ланцюжок, перевіряє його та запускає окремий тимчасовий PostgreSQL для контрольного запиту. Retention без `--apply` лише показує кандидати на очищення. Команда з `--apply` видаляє старі каталоги копій; спершу переглянь dry-run результат.

## Автоматичний запуск

Сценарій `scripts/run_backup.ps1` підтримує два режими:

- `Daily` (режим за замовчуванням): запускає всі активні логічні завдання та інкрементальну кластерну копію.
- `Full`: створює повну кластерну копію PostgreSQL.

Після успішного backup сценарій застосовує retention із лімітом чотири повні ланцюжки та надсилає Slack-повідомлення, якщо задано `SLACK_WEBHOOK_URL`.

Приклади ручного запуску:

```powershell
& "R:\scripts\run_backup.ps1" -Mode Daily
& "R:\scripts\run_backup.ps1" -Mode Full
```

У Task Scheduler створи щоденне завдання з аргументом `-File "R:\scripts\run_backup.ps1" -Mode Daily` і щотижневе завдання з `-File "R:\scripts\run_backup.ps1" -Mode Full`. Для обох вибери запуск від свого користувача, щоб Docker Desktop був доступний. У полі **Program/script** вкажи `powershell.exe`, а в **Add arguments** додай `-NoProfile -ExecutionPolicy Bypass` перед `-File`.

## Файли проєкту

- `compose.yaml` — контейнери PostgreSQL, MySQL і MongoDB та спільний каталог backup-файлів.
- `migrations/` — SQL-міграції контрольної бази.
- `src/DatabaseBackupCLI/` — реалізації C++, ODB-код і buildfile.
- `include/DatabaseBackupCLI/` — заголовки CLI, сервісів і згенеровані заголовки ODB.
- `scripts/` — backup/restore СУБД, кластерні операції та запуск за розкладом.
- `build-output/` — результати локальної збірки.
