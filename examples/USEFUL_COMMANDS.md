# Полезные команды Gum

Эта шпаргалка не про весь язык подряд, а про команды, которые чаще всего помогают писать и отлаживать скрипты.

## `use`

Подключает другой `.sek` файл.

```text
use "lib_dev.sek"

print app_name
```

Полезно для библиотек, общих настроек и хоткеев, которые хочется использовать в разных скриптах.

## `group`

Создает группу команд, которую можно запускать по имени.

```text
group setup
print "installing editor"
print "installing browser"
end

setup()
```

Полезно, когда один кусок сценария нужен несколько раз. Полный пример: `examples/17_group.sek`.

## `set`

Меняет значение уже созданной переменной.

```text
let x = 1
set x = 2
print x
```

Если переменной нет, `set` остановится с ошибкой. Полный пример: `examples/18_set.sek`.

## `save` / `load`

Сохраняет и загружает переменные.

```text
let player_name = "root"
let level = 1

save "save.seksave"

set level = 2
load "save.seksave"
print level
```

Полезно для игры: можно сохранять имя компьютера, выбранную ОС, деньги, прогресс и флаги квестов. Полный пример: `examples/19_save_load.sek`.

## `os_*`

Файловые функции в стиле маленькой `os`-библиотеки.

```text
os_mkdir("data")
os_write("data/note.txt", "hello")
os_append("data/note.txt", " world")
print os_read("data/note.txt")

if os_exists("data/note.txt")
print "file exists"
end

os_remove("data/note.txt")
os_rmdir("data")
```

Главные функции: `os_exists`, `os_isfile`, `os_isdir`, `os_touch`, `os_write`, `os_append`, `os_read`, `os_remove`, `os_mkdir`, `os_rmdir`, `os_rename`, `os_copy`.
Полный пример: `examples/20_os_files.sek`.

## `print`

Печатает значение в консоль.

```text
print "started"
print "x = " + x
print random(100)
```

Лучший друг отладки: вставил `print`, понял, доходит ли код до нужного места.

## `type(...)`

Показывает тип значения.

```text
let x = 123
print type(x)
```

Возможные ответы:

- `number`
- `string`
- `boolean`
- `nil`

Очень удобно, когда что-то не складывается или `send` ругается на тип.

Полный пример: `examples/15_type.sek`.

## `assert(...)`

Проверяет условие. Если условие ложное, скрипт останавливается с ошибкой.

```text
let delay = 100
assert(type(delay) == "number", "delay must be number")
assert(delay >= 0, "delay cannot be negative")
```

Полезно класть такие проверки в библиотеки:

```text
use "settings.sek"

assert(type(enabled) == "boolean", "enabled must be boolean")
```

Полный пример: `examples/16_assert.sek`.

## `input(...)`

Спрашивает текст в консоли.

```text
let name = input("Name: ")
print "hello, " + name
```

Можно быстро тестировать разные значения без изменения файла.

## `random(...)` / `rand(...)`

Генерирует случайные числа.

```text
print random()
print random(10)
print random(1, 100)
```

`rand(...)` - короткое имя для `random(...)`.

## `seed(...)`

Делает случайные числа повторяемыми.

```text
seed(123)
print random(100)
print random(100)

seed(123)
print random(100)
print random(100)
```

Полезно для тестов: при одинаковом seed будут одинаковые результаты.

## `if / elif / else`

Условия.

```text
if enabled == true
print "enabled"
else
print "disabled"
end
```

Можно использовать вместе с `assert`, `type`, `random`, `input`.

## `loop`

Повторяет блок.

```text
loop 5
print "tick"
sleep 100
end
```

Бесконечный цикл:

```text
loop
click
sleep 50
end
```

## `unloop`

Останавливает именованный цикл.

```text
loop {1}
print random(100)
if random(100) > 90
    unloop {1}
end
end
```

`loop {1}` задает номер цикла, а `unloop {1}` просит его остановиться.

## `toggle`

Переключает boolean-переменную.

```text
let enabled = true

f: toggle enabled
```

После каждого нажатия `f` значение будет меняться:

```text
true -> false -> true
```

## `sleep`

Пауза в миллисекундах.

```text
sleep 500
```

Нужен почти во всех автоматизациях, чтобы окно успевало реагировать.

## `send`

Печатает текст или отправляет клавиши в активное окно.

```text
send "hello"
send "line 1"{Enter}"line 2"
send {Tab}
```

Частые клавиши:

- `{Enter}`
- `{Tab}`
- `{Space}`
- `{Esc}`
- `{Backspace}`
- `{Delete}`
- `{Up}` `{Down}` `{Left}` `{Right}`
- `{F1}` ... `{F12}`

## `click`

Левый клик мышью в текущей позиции курсора.

```text
click
sleep 100
click
```

## `msg`

Показывает окно сообщения Windows.

```text
msg "done"
msg "random: " + random(100)
```

Удобно, когда скрипт работает без видимой консоли или нужно явно показать результат.

## Мини-набор для разработки

Если сомневаешься, что брать в новый скрипт, начни так:

```text
use "settings.sek"

print "script started"
assert(type(enabled) == "boolean", "enabled must be boolean")

if enabled
print "enabled"
else
print "disabled"
end
```
