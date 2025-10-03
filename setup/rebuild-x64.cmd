cd ..\out\build\X64-Release
cmake --build . --target install --config Release
cd ..\..\..\setup
makensis setup-x64.nsi
pause
