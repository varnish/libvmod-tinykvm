#!/usr/bin/env bash
CLANG_VERSION=10
if command -v "clang-11" &> /dev/null; then
	CLANG_VERSION=11
fi
if command -v "clang-12" &> /dev/null; then
	CLANG_VERSION=12
fi
if command -v "clang-13" &> /dev/null; then
	CLANG_VERSION=13
fi
if command -v "clang-14" &> /dev/null; then
	CLANG_VERSION=14
fi
if command -v "clang-15" &> /dev/null; then
	CLANG_VERSION=15
fi
export RCC="clang-${CLANG_VERSION}"
export RLD="ld.lld-${CLANG_VERSION}"
$RCC -O0 -Wall -Wextra -I$2 -target riscv32 -march=rv32imafd -ffreestanding -nostdlib -c $1 -o $3.o
$RLD -Ttext=0x120000 $3.o -o $3
