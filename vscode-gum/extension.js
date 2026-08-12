const vscode = require("vscode");
const child_process = require("child_process");
const fs = require("fs");
const path = require("path");

const tokenTypes = ["variable", "function"];
const tokenModifiers = ["declaration"];
const legend = new vscode.SemanticTokensLegend(tokenTypes, tokenModifiers);

const builtInFunctions = new Set([
  "random",
  "rand",
  "seed",
  "input",
  "type",
  "assert",
  "exists",
  "isfile",
  "isdir",
  "touch",
  "write",
  "append",
  "read",
  "remove",
  "mkdir",
  "rmdir",
  "rename",
  "copy",
  "os_exists",
  "os_isfile",
  "os_isdir",
  "os_touch",
  "os_write",
  "os_append",
  "os_read",
  "os_remove",
  "os_mkdir",
  "os_rmdir",
  "os_rename",
  "os_copy",
  "http_get",
  "http_post",
  "json_parse",
  "json_get",
  "json_array_length",
  "json_array_get",
  "json_type",
  "tg_cmd_hash",
]);

const reservedWords = new Set([
  "let",
  "set",
  "group",
  "use",
  "print",
  "msg",
  "playsound",
  "send",
  "save",
  "load",
  "click",
  "mousedown",
  "mouseup",
  "mousehold",
  "mousemove",
  "sleep",
  "toggle",
  "loop",
  "unloop",
  "if",
  "elif",
  "else",
  "end",
  "and",
  "or",
  "true",
  "false",
  "nil",
  "tg_token",
  "tg_on",
  "tg_send",
  "tg_reply",
  "tg_typing",
  "tg_photo",
  "tg_sticker",
  "tg_delete",
  "tg_edit",
  "tg_callback_answer",
  "tg_get_chat",
  "tg_leave",
  "TG_CHAT_ID",
  "TG_TEXT",
  "TG_MESSAGE_ID",
  "TG_CALLBACK_DATA",
  "TG_CALLBACK_ID",
]);

const completionSpecs = [
  {
    label: "use",
    kind: "Keyword",
    insertText: "use \"${1:file.sek}\"",
    detail: "Подключить .sek файл",
    documentation: "Вставляет код другого .sek файла в это место."
  },
  {
    label: "let",
    kind: "Keyword",
    insertText: "let ${1:name} = ${2:value}",
    detail: "Создать переменную",
    documentation: "Объявляет новую переменную в текущем скрипте."
  },
  {
    label: "set",
    kind: "Keyword",
    insertText: "set ${1:name} = ${2:value}",
    detail: "Изменить переменную",
    documentation: "Меняет значение уже созданной переменной."
  },
  {
    label: "group",
    kind: "Keyword",
    insertText: "group ${1:name}\n    ${2:print \"ok\"}\nend\n\n${1:name}()",
    detail: "Создать группу",
    documentation: "Создает группу команд. Запускается вызовом name()."
  },
  {
    label: "print",
    kind: "Function",
    insertText: "print ${1:value}",
    detail: "Вывести в консоль",
    documentation: "Печатает значение: строку, число, boolean или выражение."
  },
  {
    label: "msg",
    kind: "Function",
    insertText: "msg ${1:\"text\"}",
    detail: "Показать окно",
    documentation: "Открывает обычное Windows-окно сообщения."
  },
  {
    label: "playsound",
    kind: "Function",
    insertText: "playsound ${1:\"file.wav\"}",
    detail: "Проиграть звук",
    documentation: "Запускает звуковой файл из скрипта."
  },
  {
    label: "send",
    kind: "Function",
    insertText: "send ${1:\"text\"}",
    detail: "Отправить ввод",
    documentation: "Печатает текст или специальные клавиши в активное окно."
  },
  {
    label: "save",
    kind: "Function",
    insertText: "save \"${1:save.seksave}\"",
    detail: "Сохранить игру",
    documentation: "Записывает все текущие переменные в файл."
  },
  {
    label: "load",
    kind: "Function",
    insertText: "load \"${1:save.seksave}\"",
    detail: "Загрузить игру",
    documentation: "Загружает переменные из файла сохранения."
  },
  {
    label: "click",
    kind: "Function",
    insertText: "click",
    detail: "Левый клик",
    documentation: "Делает левый клик мышью в текущей позиции курсора."
  },
  {
    label: "mousedown",
    kind: "Function",
    insertText: "mousedown",
    detail: "Зажать левую кнопку",
    documentation: "Нажимает и удерживает левую кнопку мыши."
  },
  {
    label: "mouseup",
    kind: "Function",
    insertText: "mouseup",
    detail: "Отпустить левую кнопку",
    documentation: "Отпускает левую кнопку мыши после mousedown."
  },
  {
    label: "mousehold",
    kind: "Function",
    insertText: "mousehold ${1:100}",
    detail: "Зажать на время",
    documentation: "Удерживает левую кнопку мыши указанное число миллисекунд."
  },
  {
    label: "mousemove",
    kind: "Function",
    insertText: "mousemove, ${1:x}, ${2:y}",
    detail: "Переместить мышь",
    documentation: "Перемещает курсор мыши в указанные экранные координаты (x, y)."
  },
  {
    label: "sleep",
    kind: "Function",
    insertText: "sleep ${1:500}",
    detail: "Пауза в мс",
    documentation: "Останавливает выполнение скрипта на указанное время."
  },
  {
    label: "toggle",
    kind: "Function",
    insertText: "toggle ${1:enabled}",
    detail: "Переключить boolean",
    documentation: "Меняет true на false и false на true."
  },
  {
    label: "loop",
    kind: "Keyword",
    insertText: "loop ${1:5}\n    ${2:print \"tick\"}\nend",
    detail: "Повторить блок",
    documentation: "Запускает блок несколько раз или бесконечно, если число не указано."
  },
  {
    label: "unloop",
    kind: "Keyword",
    insertText: "unloop {${1:1}}",
    detail: "Остановить цикл",
    documentation: "Останавливает именованный цикл вида loop {1}."
  },
  {
    label: "if",
    kind: "Keyword",
    insertText: "if ${1:condition}\n    ${2:print \"ok\"}\nend",
    detail: "Условие",
    documentation: "Выполняет блок, если условие истинное."
  },
  {
    label: "elif",
    kind: "Keyword",
    insertText: "elif ${1:condition}",
    detail: "Еще одно условие",
    documentation: "Проверяется, если предыдущие ветки if/elif не сработали."
  },
  {
    label: "else",
    kind: "Keyword",
    insertText: "else",
    detail: "Иначе",
    documentation: "Ветка по умолчанию, если условия выше ложные."
  },
  {
    label: "end",
    kind: "Keyword",
    insertText: "end",
    detail: "Закрыть блок",
    documentation: "Закрывает if, loop или явный блок хоткея."
  },
  {
    label: "input",
    kind: "Function",
    insertText: "input(${1:\"Name: \"})",
    detail: "Ввод из консоли",
    documentation: "Показывает prompt и возвращает введенную строку."
  },
  {
    label: "random",
    kind: "Function",
    insertText: "random(${1:100})",
    detail: "Случайное число",
    documentation: "Возвращает случайное число: random(), random(max) или random(min, max)."
  },
  {
    label: "rand",
    kind: "Function",
    insertText: "rand(${1:100})",
    detail: "Короткий random",
    documentation: "Алиас для random(...)."
  },
  {
    label: "seed",
    kind: "Function",
    insertText: "seed(${1:123})",
    detail: "Seed рандома",
    documentation: "Задает seed, чтобы random выдавал повторяемые значения."
  },
  {
    label: "type",
    kind: "Function",
    insertText: "type(${1:value})",
    detail: "Тип значения",
    documentation: "Возвращает строку: number, string, boolean или nil."
  },
  {
    label: "assert",
    kind: "Function",
    insertText: "assert(${1:condition}, ${2:\"message\"})",
    detail: "Проверка условия",
    documentation: "Останавливает скрипт с ошибкой, если условие ложное."
  },
  {
    label: "os_write",
    kind: "Function",
    insertText: "os_write(${1:\"file.txt\"}, ${2:\"text\"})",
    detail: "Записать файл",
    documentation: "Создает или перезаписывает текстовый файл."
  },
  {
    label: "os_append",
    kind: "Function",
    insertText: "os_append(${1:\"file.txt\"}, ${2:\"text\"})",
    detail: "Дописать файл",
    documentation: "Создает файл при необходимости и дописывает текст в конец."
  },
  {
    label: "os_read",
    kind: "Function",
    insertText: "os_read(${1:\"file.txt\"})",
    detail: "Прочитать файл",
    documentation: "Возвращает содержимое файла строкой."
  },
  {
    label: "os_remove",
    kind: "Function",
    insertText: "os_remove(${1:\"file.txt\"})",
    detail: "Удалить файл",
    documentation: "Удаляет файл."
  },
  {
    label: "os_exists",
    kind: "Function",
    insertText: "os_exists(${1:\"file.txt\"})",
    detail: "Путь существует",
    documentation: "Возвращает true, если файл или папка существует."
  },
  {
    label: "os_mkdir",
    kind: "Function",
    insertText: "os_mkdir(${1:\"folder\"})",
    detail: "Создать папку",
    documentation: "Создает папку. Если она уже есть, возвращает true."
  },
  {
    label: "os_rmdir",
    kind: "Function",
    insertText: "os_rmdir(${1:\"folder\"})",
    detail: "Удалить папку",
    documentation: "Удаляет пустую папку."
  },
  {
    label: "os_rename",
    kind: "Function",
    insertText: "os_rename(${1:\"old.txt\"}, ${2:\"new.txt\"})",
    detail: "Переименовать",
    documentation: "Переименовывает или перемещает файл или папку."
  },
  {
    label: "os_copy",
    kind: "Function",
    insertText: "os_copy(${1:\"from.txt\"}, ${2:\"to.txt\"})",
    detail: "Копировать файл",
    documentation: "Копирует файл и перезаписывает путь назначения."
  },
  {
    label: "os_touch",
    kind: "Function",
    insertText: "os_touch(${1:\"file.txt\"})",
    detail: "Создать файл",
    documentation: "Создает пустой файл, если его еще нет."
  },
  {
    label: "os_isfile",
    kind: "Function",
    insertText: "os_isfile(${1:\"file.txt\"})",
    detail: "Это файл",
    documentation: "Возвращает true, если путь существует и является файлом."
  },
  {
    label: "os_isdir",
    kind: "Function",
    insertText: "os_isdir(${1:\"folder\"})",
    detail: "Это папка",
    documentation: "Возвращает true, если путь существует и является папкой."
  },
  {
    label: "http_get",
    kind: "Function",
    insertText: "http_get(${1:\"https://api.example.com\"})",
    detail: "HTTP GET",
    documentation: "Выполняет GET-запрос и возвращает тело ответа строкой. При ошибке — пустая строка."
  },
  {
    label: "http_post",
    kind: "Function",
    insertText: "http_post(${1:\"https://api.example.com\"}, ${2:body}, ${3:\"application/json\"})",
    detail: "HTTP POST",
    documentation: "Выполняет POST-запрос с телом body и Content-Type (по умолчанию text/plain)."
  },
  {
    label: "json_parse",
    kind: "Function",
    insertText: "json_parse(${1:\"{...}\"})",
    detail: "Распарсить JSON",
    documentation: "Возвращает объект json (хэндл). При ошибке парсинга — nil."
  },
  {
    label: "json_get",
    kind: "Function",
    insertText: "json_get(${1:obj}, ${2:\"key\"})",
    detail: "Значение по ключу",
    documentation: "Извлекает значение по ключу из объекта json. Если ключа нет — nil."
  },
  {
    label: "json_array_length",
    kind: "Function",
    insertText: "json_array_length(${1:arr})",
    detail: "Длина массива",
    documentation: "Возвращает длину JSON-массива. Если передан не массив — nil."
  },
  {
    label: "json_array_get",
    kind: "Function",
    insertText: "json_array_get(${1:arr}, ${2:0})",
    detail: "Элемент массива",
    documentation: "Возвращает элемент JSON-массива по индексу (с нуля). Вне диапазона — nil."
  },
  {
    label: "json_type",
    kind: "Function",
    insertText: "json_type(${1:val})",
    detail: "Тип значения",
    documentation: "Возвращает тип: number, string, boolean, null, object или array."
  },
  {
    label: "true",
    kind: "Value",
    insertText: "true",
    detail: "Истина",
    documentation: "Boolean-значение true."
  },
  {
    label: "false",
    kind: "Value",
    insertText: "false",
    detail: "Ложь",
    documentation: "Boolean-значение false."
  },
  {
    label: "nil",
    kind: "Value",
    insertText: "nil",
    detail: "Пустое значение",
    documentation: "Значение без результата."
  },
  {
    label: "{Enter}",
    kind: "Constant",
    insertText: "{Enter}",
    detail: "Клавиша Enter",
    documentation: "Используется внутри send."
  },
  {
    label: "{Tab}",
    kind: "Constant",
    insertText: "{Tab}",
    detail: "Клавиша Tab",
    documentation: "Используется внутри send."
  },
  {
    label: "{Space}",
    kind: "Constant",
    insertText: "{Space}",
    detail: "Пробел",
    documentation: "Используется внутри send."
  },
  {
    label: "{Esc}",
    kind: "Constant",
    insertText: "{Esc}",
    detail: "Клавиша Escape",
    documentation: "Используется внутри send."
  },
  {
    label: "and",
    kind: "Operator",
    insertText: "and ",
    detail: "Логическое И",
    documentation: "Возвращает true, если оба операнда истинны. Ленивое вычисление."
  },
  {
    label: "or",
    kind: "Operator",
    insertText: "or ",
    detail: "Логическое ИЛИ",
    documentation: "Возвращает true, если хотя бы один операнд истинен. Ленивое вычисление."
  },
  {
    label: "tg_token",
    kind: "Function",
    insertText: "tg_token \"${1:123456:ABC-DEF}\"",
    detail: "Установить токен бота",
    documentation: "Проверяет токен через getMe и включает long polling в конце скрипта."
  },
  {
    label: "tg_on",
    kind: "Function",
    insertText: "tg_on \"${1:/start}\" \"${2:Привет, я бот!}\"",
    detail: "Статичный ответ на команду",
    documentation: "При сообщении, равном первому аргументу, бот автоматически отвечает вторым."
  },
  {
    label: "tg_send",
    kind: "Function",
    insertText: "tg_send \"${1:text}\"",
    detail: "Отправить текст",
    documentation: "Отправляет текст в последний известный чат. Вариант: tg_send <chat_id> <text>."
  },
  {
    label: "tg_reply",
    kind: "Function",
    insertText: "tg_reply \"${1:text}\"",
    detail: "Ответить на сообщение",
    documentation: "Отправляет текст как ответ (reply_to_message_id) на последнее входящее сообщение."
  },
  {
    label: "tg_typing",
    kind: "Function",
    insertText: "tg_typing",
    detail: "Индикатор «печатает...»",
    documentation: "Показывает sendChatAction typing в текущем чате. Ставь перед долгими операциями."
  },
  {
    label: "tg_photo",
    kind: "Function",
    insertText: "tg_photo ${1:chat_id} \"${2:file_or_url}\"",
    detail: "Отправить фото",
    documentation: "Отправляет фото по file_id или URL. Вариант: tg_photo <file> (в текущий чат)."
  },
  {
    label: "tg_sticker",
    kind: "Function",
    insertText: "tg_sticker ${1:chat_id} \"${2:file_or_url}\"",
    detail: "Отправить стикер",
    documentation: "Отправляет стикер по file_id или URL. Вариант: tg_sticker <file>."
  },
  {
    label: "tg_delete",
    kind: "Function",
    insertText: "tg_delete ${1:chat_id} ${2:message_id}",
    detail: "Удалить сообщение",
    documentation: "Удаляет последнее сообщение бота: tg_delete, tg_delete <chat_id> или tg_delete <chat_id> <message_id>."
  },
  {
    label: "tg_edit",
    kind: "Function",
    insertText: "tg_edit \"${1:new_text}\"",
    detail: "Изменить сообщение",
    documentation: "Редактирует последнее отправленное ботом сообщение (или сообщение кнопки)."
  },
  {
    label: "tg_callback_answer",
    kind: "Function",
    insertText: "tg_callback_answer \"${1:text}\"",
    detail: "Ответить на callback",
    documentation: "Отвечает на callback-запрос кнопки (убирает «часики», показывает уведомление)."
  },
  {
    label: "tg_get_chat",
    kind: "Function",
    insertText: "tg_get_chat ${1:chat_id}",
    detail: "Информация о чате",
    documentation: "Печатает информацию о чате: [gum] chat <id> (<type>): <name>."
  },
  {
    label: "tg_leave",
    kind: "Function",
    insertText: "tg_leave ${1:chat_id}",
    detail: "Покинуть чат",
    documentation: "Заставляет бота покинуть чат или группу."
  },
  {
    label: "tg_cmd_hash",
    kind: "Function",
    insertText: "tg_cmd_hash(\"${1:command}\")",
    detail: "Хеш текста",
    documentation: "Возвращает числовой хеш (FNV-1a 32-bit) строки для сравнения с TG_TEXT или TG_CALLBACK_DATA."
  }
];

function activate(context) {
  const provider = {
    provideDocumentSemanticTokens(document) {
      const variables = collectVariables(document);
      const userFunctions = collectUserFunctions(document);
      const builder = new vscode.SemanticTokensBuilder(legend);

      for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber += 1) {
        const rawLine = document.lineAt(lineNumber).text;
        const codeLine = maskStringsAndComments(rawLine);
        const declarationRanges = findDeclarationRanges(codeLine);
        const functionDeclarationRanges = findFunctionDeclarationRanges(codeLine);
        const hotkeyRange = findHotkeyRange(codeLine);
        const identifierPattern = /\b[A-Za-z_][A-Za-z0-9_]*\b/g;
        let match;

        while ((match = identifierPattern.exec(codeLine)) !== null) {
          const word = match[0];
          const start = match.index;
          const nextChar = nextNonWhitespace(codeLine, start + word.length);

          if (hotkeyRange && start >= hotkeyRange.start && start < hotkeyRange.end) {
            continue;
          }

          if (builtInFunctions.has(word) && nextChar === "(") {
            builder.push(lineNumber, start, word.length, "function", []);
            continue;
          }

          if (userFunctions.has(word) && nextChar === "(") {
            builder.push(lineNumber, start, word.length, "function", []);
            continue;
          }

          if (functionDeclarationRanges.some((range) => start >= range.start && start < range.end)) {
            builder.push(lineNumber, start, word.length, "function", ["declaration"]);
            continue;
          }

          if (reservedWords.has(word) || !variables.has(word)) {
            continue;
          }

          const modifiers = declarationRanges.some((range) => start >= range.start && start < range.end)
            ? ["declaration"]
            : [];
          builder.push(lineNumber, start, word.length, "variable", modifiers);
        }
      }

      return builder.build();
    },
  };

  const completionProvider = {
    provideCompletionItems() {
      return completionSpecs.map(createCompletionItem);
    },
  };

  const outputChannel = vscode.window.createOutputChannel("Gum Runner");
  const runStatusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
  const stopStatusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 99);
  const liveStatusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 98);
  let runningProcess = null;
  let runningTerminal = null;
  let livePreviewEnabled = false;
  let livePreviewTimer = null;

  runStatusBarItem.command = "gum.runScript";
  runStatusBarItem.text = "$(play) Gum Run";
  runStatusBarItem.tooltip = "Запустить текущий .gum/.sek через sekc.exe";

  stopStatusBarItem.command = "gum.stopScript";
  stopStatusBarItem.text = "$(debug-stop) Gum Stop";
  stopStatusBarItem.tooltip = "Остановить запущенный Gum скрипт";

  liveStatusBarItem.command = "gum.toggleLivePreview";

  const updateStatusBarItems = (editor) => {
    const isSekEditor = Boolean(editor && isSekDocument(editor.document));

    if (!isSekEditor) {
      runStatusBarItem.hide();
      stopStatusBarItem.hide();
      liveStatusBarItem.hide();
      return;
    }

    runStatusBarItem.show();
    liveStatusBarItem.text = livePreviewEnabled ? "$(sync) Gum Live" : "$(sync-ignored) Gum Live";
    liveStatusBarItem.tooltip = livePreviewEnabled
      ? "Live Run включён: скрипт сохраняется и перезапускается после изменений"
      : "Live Run выключен";
    liveStatusBarItem.show();

    if (runningProcess || runningTerminal) {
      stopStatusBarItem.show();
    } else {
      stopStatusBarItem.hide();
    }
  };

  const stopRunningScript = (silent = false) => {
    if (!runningProcess && !runningTerminal) {
      if (!silent) {
        vscode.window.showInformationMessage("Сейчас нет запущенного Gum скрипта.");
      }
      return;
    }

    if (runningProcess) {
      const child = runningProcess;
      runningProcess = null;
    outputChannel.appendLine("\nОстановка Gum процесса...");
      child.kill();
    }

    if (runningTerminal) {
      const terminal = runningTerminal;
      runningTerminal = null;
      terminal.dispose();
    }

    updateStatusBarItems(vscode.window.activeTextEditor);
  };

  const createScriptTerminal = (target, cwd, sekcPath) => {
    const terminal = vscode.window.createTerminal({
      name: `Gum: ${path.basename(target.filePath)}`,
      cwd,
    });

    runningTerminal = terminal;
    terminal.show(true);

    // Run sekc.exe with the script path in the terminal. Use quoted paths.
    const quotedSekc = sekcPath.includes(" ") ? `"${sekcPath}"` : sekcPath;
    const quotedFile = target.filePath.includes(" ") ? `"${target.filePath}"` : target.filePath;
    terminal.sendText(`${quotedSekc} ${quotedFile}`);
    updateStatusBarItems(vscode.window.activeTextEditor);
  };

  const runScript = async (resourceUri, options = {}) => {
    const target = await resolveRunTarget(resourceUri);
    if (!target) {
      return;
    }

    if (target.document?.isDirty) {
      const saved = await target.document.save();
      if (!saved) {
        vscode.window.showErrorMessage("Не удалось сохранить .gum/.sek файл перед запуском.");
        return;
      }
    }

    const config = vscode.workspace.getConfiguration("gum");
    const cwd = resolveRunCwd(target.filePath, config);
    const sekcPath = resolveSekcPath(
      config.get("sekcPath", "sekc.exe"),
      target.filePath,
      cwd,
      context.extensionPath
    );

    if (!options.quiet && config.get("runInTerminal", true)) {
      stopRunningScript(true);
      createScriptTerminal(target, cwd, sekcPath);
      return;
    }

    stopRunningScript(true);

    outputChannel.clear();
    if (config.get("showOutputOnRun", true)) {
      outputChannel.show(true);
    }
    outputChannel.appendLine(`Запуск Gum: ${sekcPath} "${target.filePath}"`);
    outputChannel.appendLine(`Рабочая папка: ${cwd}`);
    outputChannel.appendLine("");

    const child = child_process.spawn(sekcPath, [target.filePath], {
      cwd,
      shell: false,
      windowsHide: true,
    });

    runningProcess = child;
    updateStatusBarItems(vscode.window.activeTextEditor);

    child.stdout.on("data", (data) => {
      outputChannel.append(data.toString("utf8"));
    });

    child.stderr.on("data", (data) => {
      outputChannel.append(data.toString("utf8"));
    });

    child.on("close", (code, signal) => {
      if (runningProcess === child) {
        runningProcess = null;
      }

      const suffix = signal ? `с сигналом ${signal}` : `с кодом ${code}`;
      outputChannel.appendLine(`\nПроцесс завершён ${suffix}`);
      updateStatusBarItems(vscode.window.activeTextEditor);

      if (options.quiet) {
        return;
      }

      if (code === 0) {
        vscode.window.showInformationMessage(`Gum скрипт выполнен: ${path.basename(target.filePath)}`);
      } else if (code !== null) {
        vscode.window.showErrorMessage(`Gum завершился с кодом ${code}`);
      }
    });

    child.on("error", (error) => {
      if (runningProcess === child) {
        runningProcess = null;
      }

      outputChannel.appendLine(`Ошибка запуска: ${error.message}`);
      updateStatusBarItems(vscode.window.activeTextEditor);
      vscode.window.showErrorMessage(`Не удалось запустить sekc.exe: ${error.message}`);
    });
  };

  const runScriptInTerminal = async (resourceUri) => {
    const target = await resolveRunTarget(resourceUri);
    if (!target) {
      return;
    }

    if (target.document?.isDirty) {
      const saved = await target.document.save();
      if (!saved) {
        vscode.window.showErrorMessage("Не удалось сохранить .gum/.sek файл перед запуском.");
        return;
      }
    }

    const config = vscode.workspace.getConfiguration("gum");
    const cwd = resolveRunCwd(target.filePath, config);
    const sekcPath = resolveSekcPath(
      config.get("sekcPath", "sekc.exe"),
      target.filePath,
      cwd,
      context.extensionPath
    );

    stopRunningScript(true);
    createScriptTerminal(target, cwd, sekcPath);
  };

  const scheduleLivePreviewRun = (document) => {
    if (!livePreviewEnabled || !isSekDocument(document)) {
      return;
    }

    if (livePreviewTimer) {
      clearTimeout(livePreviewTimer);
    }

    const config = vscode.workspace.getConfiguration("gum");
    const delay = Math.max(100, Number(config.get("livePreviewDelay", 500)) || 500);
    livePreviewTimer = setTimeout(() => {
      livePreviewTimer = null;
      runScript(document.uri, { quiet: true });
    }, delay);
  };

  updateStatusBarItems(vscode.window.activeTextEditor);

  const activeEditorListener = vscode.window.onDidChangeActiveTextEditor(updateStatusBarItems);
  const terminalCloseListener = vscode.window.onDidCloseTerminal((terminal) => {
    if (terminal === runningTerminal) {
      runningTerminal = null;
      updateStatusBarItems(vscode.window.activeTextEditor);
    }
  });
  const documentChangeListener = vscode.workspace.onDidChangeTextDocument((event) => {
    scheduleLivePreviewRun(event.document);
  });
  const runCommand = vscode.commands.registerCommand("gum.runScript", runScript);
  const runTerminalCommand = vscode.commands.registerCommand("gum.runScriptInTerminal", runScriptInTerminal);
  const stopCommand = vscode.commands.registerCommand("gum.stopScript", () => stopRunningScript(false));
  const liveCommand = vscode.commands.registerCommand("gum.toggleLivePreview", () => {
    livePreviewEnabled = !livePreviewEnabled;
    updateStatusBarItems(vscode.window.activeTextEditor);

    if (livePreviewEnabled) {
      vscode.window.showInformationMessage("Gum Live Run включён.");
      const editor = vscode.window.activeTextEditor;
      if (editor && isSekDocument(editor.document)) {
        scheduleLivePreviewRun(editor.document);
      }
    } else {
      if (livePreviewTimer) {
        clearTimeout(livePreviewTimer);
        livePreviewTimer = null;
      }
      vscode.window.showInformationMessage("Gum Live Run выключен.");
    }
  });

  context.subscriptions.push(
    vscode.languages.registerDocumentSemanticTokensProvider(
      { language: "gum" },
      provider,
      legend
    ),
    vscode.languages.registerCompletionItemProvider(
      { language: "gum" },
      completionProvider,
      ...completionTriggerCharacters()
    ),
    activeEditorListener,
    terminalCloseListener,
    documentChangeListener,
    outputChannel,
    runStatusBarItem,
    stopStatusBarItem,
    liveStatusBarItem,
    runCommand,
    runTerminalCommand,
    stopCommand,
    liveCommand
  );
}

async function resolveRunTarget(resourceUri) {
  const uri = getCommandUri(resourceUri);

  if (uri && uri.scheme === "file") {
    const filePath = uri.fsPath;
    if (!isSekFile(filePath)) {
      vscode.window.showErrorMessage("Выберите .gum/.sek файл для запуска.");
      return null;
    }

    return {
      filePath,
      document: findOpenDocument(filePath),
    };
  }

  const editor = vscode.window.activeTextEditor;
  if (!editor || !isSekDocument(editor.document)) {
    vscode.window.showErrorMessage("Откройте .gum/.sek файл Gum, чтобы запустить скрипт.");
    return null;
  }

  return {
    filePath: editor.document.uri.fsPath,
    document: editor.document,
  };
}

function getCommandUri(resourceUri) {
  if (Array.isArray(resourceUri)) {
    return getCommandUri(resourceUri[0]);
  }

  if (resourceUri instanceof vscode.Uri) {
    return resourceUri;
  }

  if (resourceUri && typeof resourceUri.fsPath === "string") {
    return resourceUri;
  }

  return null;
}

function findOpenDocument(filePath) {
  const wantedPath = comparablePath(filePath);
  return vscode.workspace.textDocuments.find((document) => {
    return document.uri.scheme === "file" && comparablePath(document.uri.fsPath) === wantedPath;
  });
}

function isSekDocument(document) {
  return Boolean(
    document &&
      document.uri.scheme === "file" &&
      (document.languageId === "gum" || isSekFile(document.uri.fsPath))
  );
}

function isSekFile(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  return ext === ".gum" || ext === ".sek";
}

function comparablePath(filePath) {
  return process.platform === "win32" ? filePath.toLowerCase() : filePath;
}

function resolveRunCwd(filePath, config) {
  const configured = String(config.get("runCwd", "") || "").trim();
  if (configured) {
    return resolveConfiguredPath(configured, filePath, getWorkspaceDirectory(filePath) || path.dirname(filePath));
  }

  return getWorkspaceDirectory(filePath) || path.dirname(filePath);
}

function resolveSekcPath(configuredPath, filePath, cwd, extensionPath) {
  const configured = stripSurroundingQuotes(String(configuredPath || "sekc.exe").trim() || "sekc.exe");
  const expanded = expandPathVariables(configured, filePath);

  if (isExplicitPath(expanded)) {
    return path.isAbsolute(expanded) ? expanded : path.resolve(cwd, expanded);
  }

  const candidates = uniquePaths([
    path.join(cwd, expanded),
    path.join(path.dirname(filePath), expanded),
    getWorkspaceDirectory(filePath) ? path.join(getWorkspaceDirectory(filePath), expanded) : "",
    path.join(extensionPath, expanded),
    path.resolve(extensionPath, "..", expanded),
  ]);
  const found = candidates.find((candidate) => candidate && fs.existsSync(candidate));

  return found || expanded;
}

function resolveConfiguredPath(configuredPath, filePath, basePath) {
  const expanded = expandPathVariables(stripSurroundingQuotes(configuredPath), filePath);
  return path.isAbsolute(expanded) ? expanded : path.resolve(basePath, expanded);
}

function expandPathVariables(value, filePath) {
  const workspaceDirectory = getWorkspaceDirectory(filePath) || "";
  let expanded = value
    .replace(/\$\{workspaceFolder\}/g, workspaceDirectory)
    .replace(/\$\{fileDirname\}/g, path.dirname(filePath))
    .replace(/\$\{file\}/g, filePath);

  if (expanded === "~" || expanded.startsWith(`~${path.sep}`) || expanded.startsWith("~/")) {
    expanded = path.join(require("os").homedir(), expanded.slice(2));
  }

  return expanded;
}

function getWorkspaceDirectory(filePath) {
  const workspaceFolder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(filePath));
  return workspaceFolder?.uri.fsPath;
}

function stripSurroundingQuotes(value) {
  if (
    (value.startsWith("\"") && value.endsWith("\"")) ||
    (value.startsWith("'") && value.endsWith("'"))
  ) {
    return value.slice(1, -1);
  }

  return value;
}

function isExplicitPath(value) {
  return (
    path.isAbsolute(value) ||
    value.startsWith(".") ||
    value.includes("\\") ||
    value.includes("/")
  );
}

function uniquePaths(paths) {
  const seen = new Set();
  return paths.filter((candidate) => {
    if (!candidate) {
      return false;
    }

    const key = comparablePath(candidate);
    if (seen.has(key)) {
      return false;
    }

    seen.add(key);
    return true;
  });
}

function createCompletionItem(spec) {
  const item = new vscode.CompletionItem(spec.label, vscode.CompletionItemKind[spec.kind]);
  item.insertText = new vscode.SnippetString(spec.insertText);
  item.detail = spec.detail;
  item.documentation = spec.documentation;
  return item;
}

function completionTriggerCharacters() {
  return ["{", "r", "s", "p", "m", "l", "i", "e", "a", "t", "u", "c", "d", "o", "j", "h", "g", "n", "b", "y", "q", "w", "v", "k", "f"];
}

function collectVariables(document) {
  const variables = new Set();
  const declarationPattern = /\blet\s+([A-Za-z_][A-Za-z0-9_]*)\b/g;

  for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber += 1) {
    const codeLine = maskStringsAndComments(document.lineAt(lineNumber).text);
    let match;

    while ((match = declarationPattern.exec(codeLine)) !== null) {
      variables.add(match[1]);
    }
  }

  return variables;
}

function collectUserFunctions(document) {
  const functions = new Set();
  const declarationPattern = /\bgroup\s+([A-Za-z_][A-Za-z0-9_]*)\b/g;

  for (let lineNumber = 0; lineNumber < document.lineCount; lineNumber += 1) {
    const codeLine = maskStringsAndComments(document.lineAt(lineNumber).text);
    let match;

    while ((match = declarationPattern.exec(codeLine)) !== null) {
      functions.add(match[1]);
    }
  }

  return functions;
}

function findDeclarationRanges(codeLine) {
  const ranges = [];
  const declarationPattern = /\blet\s+([A-Za-z_][A-Za-z0-9_]*)\b/g;
  let match;

  while ((match = declarationPattern.exec(codeLine)) !== null) {
    const start = match.index + match[0].lastIndexOf(match[1]);
    ranges.push({ start, end: start + match[1].length });
  }

  return ranges;
}

function findFunctionDeclarationRanges(codeLine) {
  const ranges = [];
  const declarationPattern = /\bgroup\s+([A-Za-z_][A-Za-z0-9_]*)\b/g;
  let match;

  while ((match = declarationPattern.exec(codeLine)) !== null) {
    const start = match.index + match[0].lastIndexOf(match[1]);
    ranges.push({ start, end: start + match[1].length });
  }

  return ranges;
}

function findHotkeyRange(codeLine) {
  const match = codeLine.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(::|:)/);
  if (!match) {
    return null;
  }

  const start = match[0].indexOf(match[1]);
  return { start, end: start + match[1].length };
}

function nextNonWhitespace(line, start) {
  for (let index = start; index < line.length; index += 1) {
    if (!/\s/.test(line[index])) {
      return line[index];
    }
  }

  return "";
}

function maskStringsAndComments(line) {
  let result = "";
  let index = 0;

  while (index < line.length) {
    const char = line[index];

    if (char === "#") {
      result += " ".repeat(line.length - index);
      break;
    }

    if (char === "\"") {
      result += " ";
      index += 1;

      while (index < line.length) {
        const stringChar = line[index];
        result += " ";
        index += 1;

        if (stringChar === "\"") {
          break;
        }
      }

      continue;
    }

    result += char;
    index += 1;
  }

  return result;
}

function deactivate() {}

module.exports = {
  activate,
  deactivate,
};
