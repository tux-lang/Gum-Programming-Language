#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"
chmod +x build.sh run.sh 2>/dev/null || true

if [ "$#" -ne 1 ]; then
    echo "Usage: ./run.sh file.gum"
    exit 1
fi

if [ ! -x ./sekc ]; then
    ./build.sh
fi

./sekc "$1"
