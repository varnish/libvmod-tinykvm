#!/usr/bin/env bash
musl-gcc -static -march=native -O2 basic.c -o /tmp/xpizza
musl-gcc -static -march=native -O2 complex.c -o /tmp/ypizza
g++ -static -std=c++17 -march=native -O2 lodepng.cpp mandelbrot.cpp -o /tmp/zpizza
#bash update.sh xpizza.com /tmp/xpizza
#bash update.sh ypizza.com /tmp/ypizza
#bash update.sh zpizza.com /tmp/zpizza
