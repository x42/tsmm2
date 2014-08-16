#!/bin/bash

# TODO: use a 'make' target for this, parallel execution
# TODO: nice parameters, use mktemp

#target dir
export DESTDIR=$HOME/tmp/tsmm2

#temp dirs
export PICDIR=/tmp/tsmm2
export SNDDIR=~/tmp

#export FFMPEG=ffmpeg
export FFMPEG=ffmpeg_harvid
export SOX=sox

#export FFMPEG_OPTS="-v 16 -preset placebo -strict -2 -pix_fmt yuv420p"
export FFMPEG_OPTS="-v 16 -strict -2 -pix_fmt yuv420p -y"
export EXT="mp4"

export TSMM2_OPTS=""
export DURATION=720  # in seconds
export HEIGHT=360    # px height

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
	trap 'rm -f "${SNDDIR}/silence1.wav" "${SNDDIR}/sin1k.wav" "${SNDDIR}/silence.wav" "${SNDDIR}/onesec.wav" "${SNDDIR}/soundtrack1.wav" "${SNDDIR}/soundtrack.wav"; rm -rf "${PICDIR}"' exit
	./tsmm2 -p -f $2 -H ${HEIGHT} -d ${DURATION} ${TSMM2_OPTS} "${PICDIR}"
	$SOX -n -r 48000 -c 2 -b16 "${SNDDIR}/silence1.wav" trim 0.0 1.0
	$SOX -n -r 48000 -c 2 -b16 "${SNDDIR}/sin1k.wav" synth $3 sine 1000 gain -18 fade 0 0 24s 0
	$SOX -n -r 48000 -c 2 -b16 "${SNDDIR}/silence.wav" trim 0.0 $4
	$SOX "${SNDDIR}/sin1k.wav" "${SNDDIR}/silence.wav" "${SNDDIR}/onesec.wav"
	$SOX "${SNDDIR}/onesec.wav" "${SNDDIR}/soundtrack1.wav" repeat ${SNDLEN}  # duration - 2
	$SOX "${SNDDIR}/silence1.wav" "${SNDDIR}/soundtrack1.wav" "${SNDDIR}/soundtrack.wav" # wastes temp space temporarily.
	rm "${SNDDIR}/silence1.wav" "${SNDDIR}/sin1k.wav" "${SNDDIR}/silence.wav" "${SNDDIR}/onesec.wav" "${SNDDIR}/soundtrack1.wav"
	$FFMPEG -r $2 -i "${PICDIR}/t%07d.png" -i "${SNDDIR}/soundtrack.wav" -shortest ${FFMPEG_OPTS} "$1"
	rm "${SNDDIR}/soundtrack.wav"
	rm -rf "${PICDIR}"
	ls -lh "$1"
}

mkdir -p ${DESTDIR}
set -e

genvid ${DESTDIR}/tsmm2_${HEIGHT}_23.976.${EXT} "24000/1001" 2002s 46046s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_24.976.${EXT} "25000/1001" 1922s 46126s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_24.${EXT} "24/1" 2000s 46000s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_25.${EXT} "25/1" 1920s 46080s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_29.97df.${EXT} "30000/1001" 1602s 46446s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_30.${EXT} "30/1" 1600s 46400s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_59.94.${EXT} "60000/1001" 801s 47247s
genvid ${DESTDIR}/tsmm2_${HEIGHT}_60.${EXT} "60/1" 800s 47200s
