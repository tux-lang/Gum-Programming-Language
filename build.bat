@echo off
setlocal

g++ -std=gnu++17 -O2 -DNDEBUG ^
  main.cpp ^
  ConsoleControl.cpp ^
  Runner.cpp ^
  Error.cpp ^
  Lexer.cpp ^
  Parser.cpp ^
  Compiler.cpp ^
  Vm.cpp ^
  Jit.cpp ^
  Interpreter.cpp ^
  WindowsPlatform.cpp ^
  Http.cpp ^
  Json.cpp ^
  Telegram.cpp ^
  -lwinmm ^
  -lcurl ^
  -static -static-libgcc -static-libstdc++ ^
  -o sekc.exe

if errorlevel 1 exit /b %errorlevel%

echo Built sekc.exe
