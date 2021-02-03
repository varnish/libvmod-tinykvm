#!/usr/bin/env bash
clang-10 -O2 -Wall -Wextra -I$2 -target riscv32 -march=rv32imfd -ffreestanding -nostdlib -c $1 -o $3.o
ld.lld-10 -Ttext=0x120000 $3.o -o $3
