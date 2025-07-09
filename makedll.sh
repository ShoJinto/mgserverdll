#!/bin/bash

gcc -shared -o mgServer.dll mgServerdll.c mongoose.c mgServer.def \
    -Wl,--out-implib=libmgServer.a \
    -Wl,--subsystem,windows \
    -DMGSERVER_EXPORTS -DMG_ENABLE_OPENSSL=1 -DMG_ENABLE_IPV6=1 -DMG_TLS=MG_TLS_OPENSSL -I. \
    -lssl -lcrypto -lws2_32 -lpthread

if [ $? -eq 0 ]; then
    echo "DLL compiled successfully"
else
    echo "Compilation failed"
    exit 1
fi