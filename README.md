Time Stamp Movie Maker
======================

This tool is intended to create reference video test patterns with on-screen
timecode to ensure technical quality of production.

Tsmm2 can generate color bars in SMPTE ECR 1-1978 and SMPTE RP 219:2002 style,
line patterns and add a frame number + timecode overlay. The configuration
if flexible and the geometry of the resulting video variable.

Despite the name, tsmm2 only creates a video frame sequence which can then
be encoded into a movie-file using e.g ffmpeg or mencoder or similar tools.
This allows to derive multiple format/codec combinations from the same image
sequence. Upon completion, tsmm2 suggests a simple ffmpeg encode command.

Examples
--------

![screenshot](https://raw.github.com/x42/tsmm2/master/examples/tsmm2.png "Overview of different formats")

Short .webm and .mp4 snippets are available in the examples folder.
A complete collection of pre-rendered movies is in the making.

*   [25fps, 640x360, 10sec, vpx8/vorbis/webm](https://raw.github.com/x42/tsmm2/master/examples/tsmm2_360_25.webm)
*   [25fps, 640x360, 10sec, x264/aac/mpeg4](https://raw.github.com/x42/tsmm2/master/examples/tsmm2_360_25.mp4)

Installation and Usage
----------------------

```bash
  git clone git://github.com/x42/tsmm2.git
  cd tsmm2
  make
  sudo make install PREFIX=/usr
  
  # test run
  tsmm2 -v -f 30/1 -H 720 -d 60 /tmp/tsmm2
  ffmpeg -r 30/1 -i /tmp/tsmm2/t%08d.png /tmp/tsmm2.mp4
```

For details please see the included man-page or run `tsmm2 --help`.

The source-code includes a small shell script `gen.sh` which
exemplifies batch video creation as well as generating and
multiplexing a 1KHz tone.

Tsmm2 only provides consistent numbered frames and timecode. The
accuracy of the actual test-video depends on video-encoder and
settings used to encode the video. Freedom from defects depends
on the complete tool-chain. Please refrain from publishing 
generated videos that are not verified to be correct or include
settings and version of the tools used to create the video to
document specific behaviour.


Thanks
------

To Martin Schmalohr and the amazing collection of files at
http://av-standard.irt.de/wiki/index.php/Referenzclips which lend themselves
as inspiration for the display.
