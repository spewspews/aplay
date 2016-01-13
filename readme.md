Seeking Audio Player
======

This creates a simple graphical interface to seek
and pause audio files. Usage is:

```
	aplay [-C codec] [-a audiodevice] file
```

The default codec is `/bin/audio/mp3dec` and the
default audio device is `/dev/audio`.

The keyboard interface is:

1. q and del quit.
2. Space pauses and unpauses.

Clicking anywhere seeks to that position. You can
hold down the mouse and adjust the position.
