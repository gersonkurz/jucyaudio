# `jucyaudio` - Open-source JUCE based Audio Player / Mix-Editor / Library Manager

`jucyaudio` is an open-source project to build a powerful and intuitive audio application for macOS (mostly Apple Silicon because I don't have access to Intel these days) and Windows. It's designed for music enthusiasts, DJs, and anyone with a large local digital audio collection who needs sophisticated tools to manage, curate, and enjoy their music.

## Why?

Coming from Windows to the Mac, I was surprised that nothing really fit *my* bill. Apple Music is really about streaming, not about organizing large local libraries. Cog is not what I was looking for, either, when you're a lifelong gold-member of MediaMonkey. I'm a heavy bandcamp user and wasn't served well enough by the Mac ecosystem. So, I built my own. And since I'm cheap, it's GPL.

## Key Goals & Intended Features

### Robust Music Library Management

*   **Effortless Organization:** Handle vast music libraries (primarily MP3s, with flexibility for other formats) with speed and stability. I am a bandcamp guy - check out my bandcamp page to understand why I am talking "vast music libraries", LOL.
*   **Rich Metadata:** Leverage and manage extensive metadata including ID3 tags, BPM, musical key, ratings, play counts, user-defined tags, and detailed filesystem information.
*   **Powerful Discovery:** Implement advanced filtering, sorting, and querying tools to help users find the perfect track or set of tracks quickly.
*   **Persistent Knowledge:** User ratings and categorizations will be central, building a personalized understanding of the library over time.

### Intuitive Curation Workflow:

Well, 'Intuitive' for me, anyway.

*   **Structured Collection Navigation:** Provide a dedicated workflow for sifting through large new acquisitions or existing parts of a collection.
*   **Candidate Identification & Rating:** Easily identify promising tracks, assign global ratings, and make decisions on their potential use.
*   **Working Sets:** Introduce the concept of flexible "working sets" – temporary collections of tracks for evaluation or project building, without altering the main library structure.
*   **Mix Project Assembly:** Streamline the process of selecting tracks from working sets or the main library and assembling them into "Mix Projects."

### Creative Music Mixing Capabilities:

*   **Playlist-Driven Mixing:** Accept curated playlists or "Mix Projects" as input.
*   **Automated & Assisted Transitions:** Provide tools for smooth, automated, or user-assisted transitions between tracks, utilizing metadata like BPM and potentially key information for harmonic mixing.
*   **Non-Destructive Project Editing:** Allow users to define and refine mixes, adjust transitions, and experiment without altering the original audio files.
*   **High-Quality Export:** Enable exporting finished mixes to common audio formats.

### Modern & Performant Architecture:

*   **Cross-Platform:** Primarily targeting macOS (with full Apple Silicon support) and Windows.
*   **Efficient Core:** Built with modern C++20 for performance and a solid foundation.
*   **User-Friendly Interface:** Utilizing the Juce framework for a responsive and native-feeling graphical user interface.

## Who is `jucyaudio` for?

*   Users with extensive digital music collections looking for better ways to manage and explore them. If you're a streaming guy, go away.
*   DJs (hobbyist to professional) seeking a tool to prepare sets, analyze tracks, and plan mixes.
*   Music curators who want to build themed playlists or broadcast-ready segments.
*   Anyone passionate about organizing and creatively interacting with their audio library.

## Current Status & Roadmap

`jucyaudio` is currently under active development. Key foundational elements of the library management, metadata scanning (including BPM and beat detection), and the database backend are in place. I am actively working on refining the curation workflow and implementing the core mixing engine functionalities.

## Contributing

As an open-source project, I welcome interest and future contributions. But I haven't thought of a model yet. For now, the project is primarily being developed by me, myself and I.

## Licensing

`jucyaudio` is licensed under the GPL. Please see the `LICENSE` file for more details. I basically had to, I'm normally more of a BSD guy, but JUCE has two licence options: be rich or be GPL, so I chose the latter. Gotta keep my money together to buy more music, LOL.

## How to support me

I have a wishlist over at https://bandcamp.com/gersonk, I think you know what to do about that :) 
