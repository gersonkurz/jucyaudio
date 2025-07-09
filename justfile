delete-jucyaudio-files:
    find ~/Library/jucyaudioApp_Dev -maxdepth 1 -type f -name 'jucyaudio_*' -delete
    echo "Deleted all jucyaudio files in ~/Library/jucyaudioApp_Dev"
