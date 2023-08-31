#!/usr/bin/env bash
musl-gcc -static -O2 data.c -o /tmp/wpizza
musl-gcc -static -O2 basic.c -o /tmp/xpizza
musl-gcc -static -march=native -O2 complex.c -o /tmp/ypizza
g++ -static -std=c++17 -march=native -Ofast lodepng.cpp mandelbrot.cpp -o /tmp/zpizza
#bash update.sh xpizza.com /tmp/xpizza
#bash update.sh ypizza.com /tmp/ypizza
bash update.sh zpizza.com /tmp/zpizza
