@echo off
CALL C:\Users\thedy\source\repos\esp-idf\export.bat > nul 2>&1
IF NOT EXIST "C:\Users\thedy\source\repos\NukCPGDrop\firmware\build" mkdir "C:\Users\thedy\source\repos\NukCPGDrop\firmware\build"
cd /d "C:\Users\thedy\source\repos\NukCPGDrop\firmware"
idf.py build
