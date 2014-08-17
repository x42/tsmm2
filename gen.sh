#!/bin/bash

#target dir
export DESTDIR=$HOME/tmp/tsmm2

#temp dirs -- `mktemp -d` ?!
export PICDIR=/tmp/tsmm2
export SNDDIR=/tmp

export FFMPEG=ffmpeg
export SOX=sox

export FFMPEG_OPTS="-v 16 -strict -2 -pix_fmt yuv420p -y"
export FFMPEG_WEBM="-qmin 0 -qmax 28 -crf 10 -b:v 250K"
export FFMPEG_MP4="-preset veryslow"
export FFMPEG_AVI="-vcodec mjpeg -intra -g 1 -qscale 8"

export TSMM2_OPTS="-j4"
export DURATION=60  # in seconds
export HEIGHT=360   # px height

################################################################################

function genvid
{
	# $1: outfile
	# $2: fps
	# $3: audio-framelen
	# $4: audio-framelen
	echo "FPS: $2"
	SNDLEN=$(( $DURATION - 2 ))
	rm -rf "${PICDIR}"
	mkdir -p "${SNDDIR}"
	trap 'rm -f "${SNDDIR}/silence1.wav" "${SNDDIR}/sin1k.wav" "${SNDDIR}/silence.wav" "${SNDDIR}/onesec.wav" "${SNDDIR}/soundtrack.wav"; rm -rf "${PICDIR}"' exit
	./tsmm2 -p -f $2 -H ${HEIGHT} -d ${DURATION} ${TSMM2_OPTS} "${PICDIR}"
	$SOX -n -r 48000 -c 2 -b16 "${SNDDIR}/silence1.wav" trim 0.0 1.0
	$SOX -n -r 48000 -c 2 -b16 "${SNDDIR}/sin1k.wav" synth $3 sine 1000 gain -18 fade 0 0 48s 0
	$SOX -n -r 48000 -c 2 -b16 "${SNDDIR}/silence.wav" trim 0.0 $4
	$SOX "${SNDDIR}/sin1k.wav" "${SNDDIR}/silence.wav" "${SNDDIR}/onesec.wav"
	$SOX -V1 "${SNDDIR}/onesec.wav" -t wav - repeat ${SNDLEN} | $SOX "${SNDDIR}/silence1.wav" -t wav - "${SNDDIR}/soundtrack.wav"
	rm "${SNDDIR}/silence1.wav" "${SNDDIR}/sin1k.wav" "${SNDDIR}/silence.wav" "${SNDDIR}/onesec.wav"
	echo "Encoding video.."
	$FFMPEG -r $2 -i "${PICDIR}/t%08d.png" -i "${SNDDIR}/soundtrack.wav" -shortest ${FFMPEG_OPTS} ${FFMPEG_MP4} "$1.mp4"
	$FFMPEG -r $2 -i "${PICDIR}/t%08d.png" -i "${SNDDIR}/soundtrack.wav" -shortest ${FFMPEG_OPTS} ${FFMPEG_WEBM} "$1.webm"
	#$FFMPEG -r $2 -i "${PICDIR}/t%08d.png" -i "${SNDDIR}/soundtrack.wav" -shortest ${FFMPEG_OPTS} ${FFMPEG_AVI} "$1.avi"
	rm "${SNDDIR}/soundtrack.wav"
	rm -rf "${PICDIR}"
	ls -lh "$1.webm" "$1.mp4"
}

mkdir -p ${DESTDIR}
set -e

genvid ${DESTDIR}/tsmm2_${HEIGHT}_23.976 "24000/1001" 2002s 46046s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_24.976 "25000/1001" 1922s 46126s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_24 "24/1" 2000s 46000s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_25 "25/1" 1920s 46080s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_29.97df "30000/1001" 1602s 46446s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_30 "30/1" 1600s 46400s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_59.94 "60000/1001" 801s 47247s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_60 "60/1" 800s 47200s
