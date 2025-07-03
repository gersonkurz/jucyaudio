#!/bin/zsh

print -P "\e]0;jucyaudio\a"

export PATH="$PATH:/usr/local/bin"
export PATH="$PATH:/System/Cryptexes/App/usr/bin"
export PATH="$PATH:/var/run/com.apple.security.cryptexd/codex.system/bootstrap/usr/local/bin"
export PATH="$PATH:/var/run/com.apple.security.cryptexd/codex.system/bootstrap/usr/bin"
export PATH="$PATH:/var/run/com.apple.security.cryptexd/codex.system/bootstrap/usr/appleinternal/bin"
export PATH="$PATH:/Library/Apple/usr/bin"
export PATH="$PATH:/usr/local/share/dotnet"
export PATH="$PATH:~/.dotnet/tools"
export PATH="$PATH:/usr/local/go/bin"
export PATH="$PATH:/Users/gersonkurz/Applications/iTerm.app/Contents/Resources/utilities"
export PATH="$PATH:/Users/gersonkurz/Library/Application Support/JetBrains/Toolbox/scripts"
export PATH="$PATH:/Applications/Visual Studio Code.app/Contents/Resources/app/bin"
export PATH="$PATH:/Applications/Scite.app/Contents/MacOS"

# Keep the shell open
exec $SHELL