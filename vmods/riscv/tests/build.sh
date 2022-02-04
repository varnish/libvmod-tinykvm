#!/usr/bin/env bash
if ! command -v "clang-12" &> /dev/null; then
	if ! command -v "clang-11" &> /dev/null; then
		RCC="clang-10"
		RLD="ld.lld-10"
	else
		RCC="clang-11"
		RLD="ld.lld-11"
	fi
else
	RCC="clang-12"
	RLD="ld.lld-12"
fi
$RCC -O0 -Wall -Wextra -I$2 -target riscv32 -march=rv32imfd -ffreestanding -nostdlib -c $1 -o $3.o
$RLD -Ttext=0x120000 $3.o -o $3
