Time Stamp Movie Maker
======================

This tool is intended to create reference video test patterns with on-screen
timecode to ensure technical quality of production.

Despite the name, tsmm actually only creates a video frame sequence which can
be encoded into a movie-file using e.g ffmpeg or mencoder or similar tools.
Upon completion, tsmm2 suggests a ffmpeg encode command.


Examples
--------

![screenshot](https://raw.github.com/x42/tsmm2/master/examples/tsmm2.png "Overview of different formats")

Short .webm and .mp4 example snippets are available in the examples folder.
A complete collection of pre-rendered movies is in the making.

*   [25fps, 640x360, 10sec, vpx8/vorbis/webm](https://raw.github.com/x42/tsmm2/master/examples/tsmm2_360_25.webm)
*   [25fps, 640x360, 10sec, x264/aac/mpeg4](https://raw.github.com/x42/tsmm2/master/examples/tsmm2_360_25.mp4)

Install and Usage
-----------------

```bash
  git clone git://github.com/x42/tsmm2.git
  cd tsmm2
  make
  sudo make install PREFIX=/usr
  
  # test run
  tsmm2 -v -f 30/1 -H 720 -d 60 /tmp/tsmm2
  ffmpeg -r 30/1 -i /tmp/tsmm2/t%08d.png /tmp/tsmm2s.mp4
```

For details please see the included man-page or run `tsmm --help`.

The source-code includes a shell script `gen.sh` which also includes
a 1KHz tone generator and exemplifies batch creation.


Thanks
------

To Martin Schmalohr and the amazing collection of files at
http://av-standard.irt.de/wiki/index.php/Referenzclips which lend themselves
as inspiration for the display.
