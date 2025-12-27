@echo off
echo Cleaning build directories...
if exist build rmdir /s /q build
if exist build-x64 rmdir /s /q build-x64
if exist build-x86 rmdir /s /q build-x86
if exist build-arm64 rmdir /s /q build-arm64
if exist releases rmdir /s /q releases
echo Clean complete.
