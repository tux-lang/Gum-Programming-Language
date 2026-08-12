#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"
chmod +x build.sh run.sh 2>/dev/null || true

CXX="${CXX:-g++}"

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "[X] $CXX not found. Install g++ first, e.g.:"
    echo "      Debian/Ubuntu:  sudo apt install g++ libx11-dev libxtst-dev"
    echo "      Fedora:         sudo dnf install gcc-c++ libX11-devel libXtst-devel"
    echo "      Arch:           sudo pacman -S gcc libx11 libxtst"
    exit 1
fi

if ! pkg-config --exists x11 2>/dev/null && ! ls /usr/include/X11/Xlib.h >/dev/null 2>&1; then
    echo "[X] X11 development headers not found (Xlib.h). Install libx11-dev / libX11-devel."
    exit 1
fi

if ! ls /usr/include/X11/extensions/XTest.h >/dev/null 2>&1; then
    echo "[X] XTest development header not found (XTest.h). Install libxtst-dev / libXtst-devel."
    exit 1
fi

"$CXX" -std=gnu++17 -O2 \
    main.cpp \
    ConsoleControl.cpp \
    Runner.cpp \
    Error.cpp \
    Lexer.cpp \
    Parser.cpp \
    Compiler.cpp \
    Vm.cpp \
    Jit.cpp \
    Interpreter.cpp \
    WindowsPlatform.cpp \
    Http.cpp \
    Json.cpp \
    Telegram.cpp \
    -lX11 -lXtst -lcurl \
    -o sekc

echo "Built ./sekc"
