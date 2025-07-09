#!/bin/bash

gcc test.c -o test.exe -I. -L. -lmgServer -lws2_32 -lpthread

if [ $? -eq 0 ]; then
    echo "Compilation successful. test.exe generated."
else
    echo "Compilation failed."
    exit 1
fi