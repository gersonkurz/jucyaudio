@echo off
:: Build Debug for native architecture (or specified arch)
call "%~dp0build.cmd" %1 Debug
