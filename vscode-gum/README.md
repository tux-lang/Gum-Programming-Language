# Gum

Локальное расширение VS Code для файлов `.gum` (и старых `.sek`).

Что даёт:

- подсветку синтаксиса Gum;
- snippets и автодополнение для частых команд (включая Telegram-команды `tg_*`);
- semantic-подсветку переменных и групп;
- быстрый запуск `.gum`/`.sek` через `sekc.exe`;
- live-вывод stdout/stderr в панель `Gum Runner`;
- запуск в терминале для скриптов с `input(...)`, хоткеями и долгой работой.

## Установка

Из папки расширения:

```powershell
powershell -ExecutionPolicy Bypass -File .\vscode-gum\install.ps1
```

Потом перезапусти VS Code или выполни команду `Developer: Reload Window`.

## Запуск

- Открой `.gum` файл и нажми кнопку `Gum Run` в статусбаре.
- Или нажми `Ctrl+F5`.
- Или кликни правой кнопкой по файлу в Explorer и выбери `Gum: Run Script`.

Команда ищет `sekc.exe` так:

1. настройка `gum.sekcPath`;
2. папка workspace;
3. папка текущего скрипта;
4. родительская папка расширения;
5. `PATH`.

Если `sekc.exe` лежит в корне проекта (рядом с папкой `vscode-gum`), настройка обычно не нужна.

## Терминал

Для интерактивных скриптов используй `Gum: Run Script in Terminal`. Это полезно, если в коде есть:

- `input(...)`;
- хоткеи, которые долго ждут нажатий;
- вывод, который хочется видеть в обычном терминале.

## Live Run

Команда `Gum: Toggle Live Run` включает автоперезапуск текущего скрипта после изменений. Расширение сохраняет файл, останавливает прошлый процесс и запускает новый.

Задержка настраивается через `gum.livePreviewDelay`.

## Настройки

```json
{
  "gum.sekcPath": "sekc.exe",
  "gum.runCwd": "",
  "gum.runInTerminal": true,
  "gum.livePreviewDelay": 500,
  "gum.showOutputOnRun": true
}
```

## Telegram

Расширение подсказывает все встроенные Telegram-команды:

- `tg_token`, `tg_on`, `tg_send`, `tg_reply`, `tg_typing`;
- `tg_photo`, `tg_sticker`, `tg_delete`, `tg_edit`;
- `tg_callback_answer`, `tg_get_chat`, `tg_leave`;
- `tg_cmd_hash(...)` — функция-хеш для сравнения с `TG_TEXT` / `TG_CALLBACK_DATA`.

Подробности и примеры ботов — в корневом `README.md` проекта и в `examples/25_telegram.gum`.
