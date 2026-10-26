#!/bin/sh

gcc -o maker -std=c89 -O3 -Wall -Wextra -Werror -Wpedantic -Wconversion amake.c bmk.c _fsio.c
