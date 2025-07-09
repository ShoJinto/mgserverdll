@echo off
 gcc test.c -o test.exe -I. -L. -lmgServer -lws2_32 -lpthread
 if %errorlevel% equ 0 (
     echo Compilation successful. test.exe generated.
 ) else (
     echo Compilation failed.
     exit /b %errorlevel%
 )