#!/usr/bin/env bash
clang-10 -O1 -Wall -Wextra -target riscv32 -march=rv32imfd -ffreestanding -nostdlib -c $1 -o $2.o
ld.lld-10 -Ttext=0x120000 --undefined=on_recv $2.o -o $2
