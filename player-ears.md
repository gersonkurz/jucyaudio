# 7 States

- Silence
- SilenceTrackLoaded
- TrackPlaying
- TrackPaused
- SilenceMixLoaded
- MixPlaying
- MixPaused

## State 1: Silence
- STOP, PLAY and PAUSE are disabled
- when you double-click a track, it starts playing from position 0 -> state TrackPlaying

## State 3: TrackPlaying
- when you hit pause -> transition to state TrackPaused
- when you hit stop -> transition to state SilenceTrackLoaded

## State 2: SilenceTrackLoaded
- similar to State 1, but a track is loaded, so PLAY button is enabled. 
- If you click play -> transition to state TrackPlaying, starting from position 0

## State 4: TrackPaused
- has a position that can be resumed
- Buttons STOP and PAUSE are disabled, PLAY is enabled.
- Tracks can always be resumed
- when you hit play, song is resumed from last position -> Transition to state 3: TrackPlaying

## State 5: SilenceMixLoaded
- STOP and PAUSE are disabled, PLAY is enabled.
- If you click PLAY, mix resumes at the absolute mix position
- When you double-click somewhere, mix resumes at the absolute position

## State 6: MixPlaying
- when you double-click a track in the mix editor, it starts playing from whatever position that is -> state MixPlaying
- STOP and PAUSE are enabled, PLAY is disabled.
- When you hit STOP, you enter state 5: SilenceMixLoaded and position is set to 0
- When you hit PAUSE, you enter state 7: MixPaused

## State 7: MixPaused
- has a position that can be resumed
- Buttons STOP and PAUSE are disabled, PLAY is enabled.
- when you hit play, mix is resumed from last position -> Transition to state 6: MixPlaying

# Notes

## Absolute Timeline Position

- Position should not be relative to a track, but relative to the absolute timeline of the mix. i.e. if the position is 10 minutes, if you delete the track at this positiion and another track comes it, you'll be resuming at "10 minutes" playing that other track.

