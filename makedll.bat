@echo off

set GCC=gcc
set CFLAGS=-shared -o mgServer.dll mgServerdll.c mongoose.c mgServer.def -Wl,--out-implib=libmgServer.a -Wl,--subsystem,windows -DMGSERVER_EXPORTS -DMG_ENABLE_OPENSSL=1 -DMG_ENABLE_IPV6=1 -DMG_TLS=MG_TLS_OPENSSL -I.
set LDFLAGS=-lssl -lcrypto -lws2_32 -lpthread

%GCC% %CFLAGS% %LDFLAGS%

if %errorlevel% equ 0 (
    echo DLL compiled successfully
) else (
    echo Compilation failed
    exit /b 1
)