@echo off
CALL C:\Users\thedy\source\repos\esp-idf\export.bat > nul
cd /d C:\Users\thedy\source\repos\NukCPGDrop\firmware
idf.py build
