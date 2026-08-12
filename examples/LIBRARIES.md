# Инструкция: библиотеки `.sek`

Библиотека в Gum - это обычный `.sek`/`.gum` файл, который ты подключаешь в другой файл через команду `use`.

## Быстрый старт

Создай файл библиотеки, например `my_lib.sek`:

```text
let app_name = "My script"
let delay = 500

print "my_lib loaded"
```

Подключи его в основном файле:

```text
use "my_lib.sek"

print app_name
sleep delay
```

Запусти основной файл:

```bat
sekc.exe main.sek
```

## Как работает `use`

Команда:

```text
use "lib_dev.sek"
```

говорит интерпретатору: возьми код из `lib_dev.sek` и вставь его в это место перед запуском.

После подключения доступны:

- переменные из библиотеки;
- обычные команды из библиотеки;
- хоткеи из библиотеки;
- другие `use`, если библиотека подключает еще файлы.

## Пути к файлам

Путь в `use` считается относительно файла, где написан `use`.

Пример структуры:

```text
project/
  main.sek
  libs/
    text.sek
```

В `main.sek` нужно писать:

```text
use "libs/text.sek"
```

Можно использовать и обратный слеш:

```text
use "libs\\text.sek"
```

## Что класть в библиотеку

Обычно в библиотеку удобно выносить:

- общие настройки;
- часто используемые переменные;
- хоткеи, которые нужны в разных скриптах;
- проверки через `assert`;
- повторяющиеся куски автоматизации.

Пример `settings.sek`:

```text
let click_delay = 50
let enabled = true
let username = "Player"
```

Пример `dev_checks.sek`:

```text
assert(type(click_delay) == "number", "click_delay must be number")
assert(type(enabled) == "boolean", "enabled must be boolean")
```

Основной файл:

```text
use "settings.sek"
use "dev_checks.sek"

print "loaded for " + username
```

## Библиотека с хоткеем

В библиотеке можно объявить хоткей:

```text
# hotkeys.sek

f::
print "F pressed from library"
```

Потом подключить:

```text
use "hotkeys.sek"

print "main loaded"
```

При запуске хоткей из библиотеки тоже будет работать.

## Важное правило про порядок

`use` вставляет код именно в то место, где написан.

Так можно:

```text
use "settings.sek"
print app_name
```

А так нельзя, если `app_name` создан в `settings.sek`:

```text
print app_name
use "settings.sek"
```

Потому что переменная еще не была создана.

## Защита от циклов

Так делать нельзя:

```text
# a.sek
use "b.sek"
```

```text
# b.sek
use "a.sek"
```

Gum остановится с ошибкой `Cyclic use detected`, чтобы скрипт не подключал файлы бесконечно.

## Мини-шаблон библиотеки

```text
# Название: utils.sek
# Что делает: общие настройки и проверки.

let utils_loaded = true

assert(type(utils_loaded) == "boolean", "utils_loaded must be boolean")
```

Подключение:

```text
use "utils.sek"
assert(utils_loaded == true, "utils not loaded")
```

