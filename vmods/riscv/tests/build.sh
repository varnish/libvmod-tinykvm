#!/usr/bin/env bash
if ! command -v "clang-11" &> /dev/null; then
	export RCC="clang-10"
	export RLD="ld.lld-10"
else
	export RCC="clang-11"
	export RLD="ld.lld-11"
fi
$RCC -O0 -Wall -Wextra -I$2 -target riscv32 -march=rv32imfd -ffreestanding -nostdlib -c $1 -o $3.o
$RLD -Ttext=0x120000 $3.o -o $3
