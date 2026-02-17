# JucyAudio

**First public release: v1.0.0 - December 23, 2025**

**Website: [jucyaudio.com](https://jucyaudio.com)**

An open-source audio player, mix editor, and library manager for macOS (Apple Silicon) and Windows. Built for music enthusiasts, DJs, and anyone with a large local digital audio collection who needs sophisticated tools to manage, curate, and mix their music.

## Why JucyAudio?

Coming from Windows to the Mac, I was surprised that nothing really fit *my* bill. Apple Music is about streaming, not organizing large local libraries. Cog wasn't what I was looking for either, when you're a lifelong MediaMonkey user. I'm a heavy Bandcamp user and wasn't served well enough by the Mac ecosystem. So I built my own. And since I'm cheap, it's GPL.

## Features

### Music Library Management
- Handle vast music libraries (MP3, FLAC, WAV, and more) with speed and stability
- Rich metadata support: ID3 tags, automatic BPM detection, ratings, play counts
- Powerful filtering, sorting, and search across your entire collection
- Hierarchical folder navigation with instant response times

### Curation Workflow
- **Working Sets:** Flexible temporary collections for evaluating and organizing tracks
- **Mix Projects:** Assemble tracks from your library into mix projects
- Structured navigation for sifting through large collections

### Mix Editor
- Visual timeline editor for arranging tracks
- Waveform display with cue points
- Crossfade and transition editing
- Built-in equalizer and reverb effects with presets
- High-quality export to MP3 and WAV formats
- Export organization system with folder categories

### Technical
- Cross-platform: macOS (Apple Silicon native) and Windows
- Modern C++20 core with JUCE framework for the UI
- SQLite database with automatic backups
- Multiple theme support (dark/light variants)

## Download

Download the latest release from the [Releases](https://github.com/gersonkurz/jucyaudio/releases) page.

- **macOS:** Download the DMG, drag JucyAudio to Applications
- **Windows:** Download the installer or portable ZIP

## Building from Source

See [docs/build.md](docs/build.md) for detailed build instructions for macOS and Windows.

## Who is JucyAudio for?

- Users with extensive digital music collections looking for better ways to manage and explore them
- DJs (hobbyist to professional) seeking a tool to prepare sets, analyze tracks, and plan mixes
- Music curators who want to build themed playlists or broadcast-ready segments
- Anyone passionate about organizing and creatively interacting with their audio library

If you're a streaming-only person, this probably isn't for you.

## License

JucyAudio is licensed under the GPL. See [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) for JucyAudio's license and all third-party licenses. JUCE offers two license options: commercial or GPL - I chose GPL. Gotta keep my money together to buy more music.

## Support

Found a bug or have a feature request? Open an issue on [GitHub](https://github.com/gersonkurz/jucyaudio/issues).

Want to support the project? Check out my [Bandcamp wishlist](https://bandcamp.com/gersonk) - you know what to do.

## SAST Tools

[PVS-Studio](https://pvs-studio.com/en/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code.

