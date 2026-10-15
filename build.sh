#!/usr/bin/env sh
# Build with g++ (MinGW/MSYS2 on Windows, or any Linux/macOS toolchain)
set -e
g++ -std=c++17 -O2 -Wall -Wextra -finput-charset=UTF-8 -fexec-charset=UTF-8 src/main.cpp -o mini-ehr
echo "Built ./mini-ehr"
