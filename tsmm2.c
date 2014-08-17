/*
 * Copyright (C) 2012, 2014 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <getopt.h>
#include <cairo/cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <pthread.h>

#ifndef MAX
#define MAX(A,B) ( (A) < (B) ? (B) : (A) )
#endif
#ifndef MIN
#define MIN(A,B) ( (A) < (B) ? (A) : (B) )
#endif

#ifdef CUSTOM_PNG_WRITER
#include <zlib.h>
#include <png.h>
#endif

#ifndef FONTFILE
#define FONTFILE "DroidSansMono"
#endif

#ifndef VERSION
#define VERSION "0.2"
#endif

#define DIRSEP '/'

static PangoFontDescription *font_desc;

/*** part one: timecode functions */

typedef struct Rational {
	int num;
	int den;
} Rational;

typedef struct TimecodeRate {
	Rational fps;
	uint8_t drop; ///< 1: use drop-frame timecode (only valid for 30000/1001 or 2997/100)
} TimecodeRate;

typedef struct TimecodeTime {
	int32_t hour; ///< timecode hours 0..24
	int32_t minute; ///< timecode minutes 0..59
	int32_t second; ///< timecode seconds 0..59
	int32_t frame; ///< timecode frames 0..fps
} TimecodeTime;

static int format_tc (char *p, TimecodeRate *tr, TimecodeTime *tc) {
	return sprintf (p, "%02d:%02d:%02d%c%02d",
			tc->hour,
			tc->minute,
			tc->second,
			tr->drop ? ';' : ':',
			tc->frame);
}

static void sample_to_timecode (TimecodeTime * const t, TimecodeRate const * const r, const double samplerate, const int64_t sample) {
	const double  fps_d = (double)((r)->fps.num) / (double)((r)->fps.den);
	const int64_t fps_i = ceil (fps_d);

	if (r->drop) {
		int64_t frameNumber = floor (sample * fps_d / samplerate);

		/* there are 17982 frames in 10 min @ 29.97df */
		const int64_t D = frameNumber / 17982;
		const int64_t M = frameNumber % 17982;

		frameNumber +=  18 * D + 2 * ((M - 2) / 1798);

		t->frame  =    frameNumber % 30;
		t->second =   (frameNumber / 30) % 60;
		t->minute =  ((frameNumber / 30) / 60) % 60;
		t->hour   = (((frameNumber / 30) / 60) / 60);

	} else {
		double timecode_frames_left_exact;
		int64_t timecode_frames_left;
		const double frames_per_timecode_frame = samplerate / fps_d;
		const int64_t frames_per_hour = (int64_t)(3600 * fps_i * frames_per_timecode_frame);

		t->hour = sample / frames_per_hour;
		double sample_d = sample % frames_per_hour;

		timecode_frames_left_exact = sample_d / frames_per_timecode_frame;

		timecode_frames_left = (int64_t) floor (timecode_frames_left_exact);

		t->minute = timecode_frames_left / (fps_i * 60);
		timecode_frames_left = timecode_frames_left % (fps_i * 60);
		t->second = timecode_frames_left / fps_i;
		t->frame  = timecode_frames_left % fps_i;
	}
}

static void framenumber_to_timecode (TimecodeTime * const t, TimecodeRate const * const r, const int64_t frameno) {
	sample_to_timecode (t, r, r->fps.num / (double)r->fps.den, frameno);
}


/*** part two: color test screen */

static void triangle (cairo_t* cr, const float x, const float y, const float dir, const float scale) {
	cairo_save (cr);
	cairo_set_line_width (cr, 1.0);
	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.9);
	cairo_translate (cr, x, y);
	cairo_rotate (cr, dir);
	cairo_move_to (cr,  0.0, 32.0 * scale);
	cairo_line_to (cr,  8 * scale, 0);
	cairo_line_to (cr, -8 * scale, 0);
	cairo_close_path (cr);
	cairo_fill (cr);
	cairo_restore (cr);
}

static void smpte78 (cairo_t* cr, const float w, const float h) {
	/* SMPTE ECR 1-1978 approx as image -
	 * true 1-1978 is impossible as image due to 'sub-blacks'
	 */
	const float sy1 = h / 3.5;

	float x0 = 0;
	float y0 = 0;
	float x1 = w / 7;
	float y1 = ceil(sy1 * 2.25);

	// main color stripes
	cairo_set_source_rgba (cr, .75, .75, .75, 1.0); // gray
	cairo_rectangle (cr, rint (x0 + 0 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .75, .75, .00, 1.0); // yellow
	cairo_rectangle (cr, rint (x0 + 1 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .00, .75, .75, 1.0); // cyan
	cairo_rectangle (cr, rint (x0 + 2 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .00, .75, .00, 1.0); // green
	cairo_rectangle (cr, rint (x0 + 3 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .75, .00, .75, 1.0); // magenta
	cairo_rectangle (cr, rint (x0 + 4 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .75, .00, .00, 1.0); // red
	cairo_rectangle (cr, rint (x0 + 5 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .00, .00, .75, 1.0); // blue
	cairo_rectangle (cr, floor (x0 + 6 * x1), y0, ceil (x1), y1);
	cairo_fill (cr);

	// inverse colors
	y0 = floor(sy1 * 2.25);
	y1 = ceil(sy1 * 0.25);
	cairo_set_source_rgba (cr, .00, .00, .75, 1.0); // blue
	cairo_rectangle (cr, rint (x0 + 0 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .075, .075, .075, 1.0); // almost black
	cairo_rectangle (cr, rint (x0 + 1 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .75, .0, .75, 1.0); // magenta
	cairo_rectangle (cr, rint (x0 + 2 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .075, .075, .075, 1.0); // almost black
	cairo_rectangle (cr, rint (x0 + 3 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .00, .75, .75, 1.0); // cyan
	cairo_rectangle (cr, rint (x0 + 4 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .075, .075, .075, 1.0); // almost black
	cairo_rectangle (cr, rint (x0 + 5 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .75, .75, .75, 1.0); // gray
	cairo_rectangle (cr, floor (x0 + 6 * x1), y0, ceil (x1), y1);
	cairo_fill (cr);

	// bottom row
	y0 = floor(sy1 * 2.5);
	y1 = ceil(h - y0);

	// saturated color blocks
	x0 = floor(x0 + 5 * x1);
	x1 = x0 / 4;
	x0 = 0;
	cairo_set_source_rgba (cr, .00, .13, .30, 1.0); // dark blue
	cairo_rectangle (cr, rint (x0 + 0 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 1.0); // really white
	cairo_rectangle (cr, rint (x0 + 1 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .20, .00, .42, 1.0); // violet
	cairo_rectangle (cr, rint (x0 + 2 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .075, .075, .075, 1.0); // almost black
	cairo_rectangle (cr, rint (x0 + 3 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);

	// bottom right-end blacks
	x0 = floor(x0 + 4 * x1);
	x1 = w / 21.;
	cairo_set_source_rgba (cr, .04, .04, .04, 1.0); // nearly black
	cairo_rectangle (cr, rint (x0 + 0 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .075, .075, .075, 1.0); // almost black
	cairo_rectangle (cr, rint (x0 + 1 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .11, .11, .11, 1.0); // coffee black
	cairo_rectangle (cr, floor (x0 + 2 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);

	x0 = 0;
	x1 = w / 7.;
	cairo_set_source_rgba (cr, .075, .075, .075, 1.0); // almost black
	cairo_rectangle (cr, floor (x0 + 6 * x1), y0, ceil (x1), y1);
	cairo_fill (cr);
}

static void smpte02 (cairo_t* cr, const float w, const float h) {
	/* SMPTE RP 219:2002 */
	const float mb = (h * 2 / 3);
	const float lb = (h * 3 / 4);

	float x0, y0;
	float x1, y1;

	y0 = 0;
	x1 = w / 8;
	y1 = ceil (mb - (lb - mb));

	// left - side gray
	cairo_set_source_rgba (cr, .41, .41, .41, 1.0);
	cairo_rectangle (cr, 0, y0, ceil (x1 + 1), y1);
	cairo_fill (cr);

	x0 = w / 8;
	x1 = w * 3 / 28;
	// main color stripes
	cairo_set_source_rgba (cr, .75, .75, .75, 1.0); // gray
	cairo_rectangle (cr, rint (x0 + 0 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .75, .75, .00, 1.0); // yellow
	cairo_rectangle (cr, rint (x0 + 1 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .00, .75, .75, 1.0); // cyan
	cairo_rectangle (cr, rint (x0 + 2 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .00, .75, .00, 1.0); // green
	cairo_rectangle (cr, rint (x0 + 3 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .75, .00, .75, 1.0); // magenta
	cairo_rectangle (cr, rint (x0 + 4 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .75, .00, .00, 1.0); // red
	cairo_rectangle (cr, rint (x0 + 5 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .00, .00, .75, 1.0); // blue
	cairo_rectangle (cr, rint (x0 + 6 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);

	// right - side gray
	x1 = w / 8;
	cairo_set_source_rgba (cr, .41, .41, .41, 1.0);
	cairo_rectangle (cr, ceil(w - x1), y0, ceil(x1), y1);
	cairo_fill (cr);

	y0 = floor (mb - (lb - mb));
	y1 = ceil (lb - mb);
	x0 = 0;
	x1 = ceil(1 + w / 8);

	// left column
	cairo_set_source_rgba (cr, .000, 1.00, 1.00, 1.0);
	cairo_rectangle (cr, x0, y0, x1, y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, 1.00, 1.00, .000, 1.0);
	cairo_rectangle (cr, x0, y0 + y1, x1, y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .175, .175, .175, 1.0);
	cairo_rectangle (cr, x0, lb, x1, ceil (h - lb));
	cairo_fill (cr);

	// gradient bar
	x0 = w / 8;
	x1 = w * 3 / 28;

	// fixed colors 2nd col
	cairo_set_source_rgba (cr, .000, .125, .300, 1.0); // CHECK
	cairo_rectangle (cr, rint(x0), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .200, .000, .415, 1.0); // CHECK
	cairo_rectangle (cr, rint(x0), y0 + y1, ceil (x1 + 1), y1);
	cairo_fill (cr);

	// gray above gradient
	cairo_set_source_rgba (cr, .75, .75, .75, 1.0);
	cairo_rectangle (cr, rint(x0 + x1), y0, ceil (1 + 6 * x1), y1);
	cairo_fill (cr);

	// gradient
	cairo_pattern_t * pat = cairo_pattern_create_linear (x0 + x1, 0, ceil (1 + 6 * x1), 0);
	cairo_pattern_add_color_stop_rgba (pat, 0.0, .020, .020, .020, 1.0);
	cairo_pattern_add_color_stop_rgba (pat, 1.0,  1.0,  1.0,  1.0, 1.0);
	cairo_set_source (cr, pat);
	cairo_rectangle (cr, x0 + x1, floor (y0 + y1), ceil (1 + 6 * x1), y1);
	cairo_fill (cr);
	cairo_pattern_destroy (pat);

	// bottom row left half
	x0 = w / 8;
	y0 = floor (lb);
	y1 = ceil (h - lb);
	x1 = w * 3 / 56; // 1/2 sub-spacing

	cairo_set_source_rgba (cr, .020, .020, .020, 1.0);
	cairo_rectangle (cr, x0, y0, ceil (3 * x1 + 1), y1);
	cairo_fill (cr);

	cairo_set_source_rgba (cr, 1.00, 1.00, 1.00, 1.0);
	cairo_rectangle (cr, rint (x0 + 3 * x1), y0, ceil (4 * x1), y1);
	cairo_fill (cr);

	// bottom row right half
	cairo_set_source_rgba (cr, .020, .020, .020, 1.0);
	cairo_rectangle (cr, rint (x0 + 7 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);

	x0 = x0 + 8 * x1;
	x1 = w * 3 / 84; // 1/3 sub-spacing from now
	// continue
	cairo_rectangle (cr, rint (x0 + 0 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .000, .000, .000, 1.0);
	cairo_rectangle (cr, rint (x0 + 1 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .020, .020, .020, 1.0);
	cairo_rectangle (cr, rint (x0 + 2 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .040, .040, .040, 1.0);
	cairo_rectangle (cr, rint (x0 + 3 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .020, .020, .020, 1.0);
	cairo_rectangle (cr, rint (x0 + 4 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .050, .050, .050, 1.0);
	cairo_rectangle (cr, rint (x0 + 5 * x1), y0, ceil (x1 + 1), y1);
	cairo_fill (cr);

	cairo_set_source_rgba (cr, .020, .020, .020, 1.0);
	cairo_rectangle (cr, rint (x0 + 6 * x1), y0, ceil (3 * x1 + 1), y1);
	cairo_fill (cr);

	// right column
	y0 = floor (mb - (lb - mb));
	y1 = ceil (lb - mb);
	x1 = ceil(w / 8);
	x0 = ceil(w * 7 / 8);
	cairo_set_source_rgba (cr, .000, .000, 1.00, 1.0);
	cairo_rectangle (cr, x0, y0, x1, y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, 1.00, .000, .000, 1.0);
	cairo_rectangle (cr, x0, y0 + y1, x1, y1);
	cairo_fill (cr);
	cairo_set_source_rgba (cr, .175, .175, .175, 1.0);
	cairo_rectangle (cr, x0, lb, x1, ceil (h - lb));
	cairo_fill (cr);
}

static void testscreen (cairo_t* cr, const float w, const float h, uint8_t mode) {
	int i,j;
	float x0, y0;
	float x1, y1;

	const float cx = w * .5;
	const float cy = h * .5;
	const float cxp = cx + .5;
	const float cyp = cy + .5;

	cairo_set_source_rgba (cr, .45, .45, .45, 1.0);
	cairo_rectangle (cr, 0, 0, w, h);
	cairo_fill (cr);

	cairo_set_source_rgba (cr, .38, .38, .38, 1.0);
	cairo_set_line_width (cr, 1.0);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
	for (i = 0; i < h/16 ; ++i) {
		cairo_rectangle (cr, .5 + i * 8, .5 + i * 8, (w - 16 * i), (h - 16 * i));
		cairo_stroke (cr);
	}

	// inner main bounds
	const float i_x0 = rint(w / 8.);
	const float i_x1 = rint(w * 7. / 8.);
	const float i_y0 = rint(h / 12.);
	const float i_y1 = rint(h * 11. / 12.);

	// separators
	const float sx1  = (i_x1 - i_x0) / 6.;
	const float sy1  = (i_y1 - i_y0) / 5.;


	cairo_save(cr);

	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 1.0);
	cairo_rectangle (cr, i_x0, i_y0, i_x1 - i_x0, i_y1 - i_y0);
	cairo_clip_preserve (cr);
	cairo_fill (cr);


	// top-row vertical lines
	cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);

	cairo_set_line_width (cr, 1.0);
	x0 = rint (i_x0 + sx1) + .5;
	for (i = 1; i < sx1; i += 2) {
		cairo_move_to (cr, x0 + i, i_y0);
		cairo_line_to (cr, x0 + i, i_y0 + sy1);
		cairo_stroke (cr);
	}
	cairo_set_line_width (cr, 2.0);
	x0 += .5;
	for (; i < 2 * sx1; i += 4) {
		cairo_move_to (cr, x0 + i, i_y0);
		cairo_line_to (cr, x0 + i, i_y0 + sy1);
		cairo_stroke (cr);
	}
	cairo_set_line_width (cr, 3.0);
	x0 += .5;
	for (; i < 3 * sx1; i += 6) {
		cairo_move_to (cr, x0 + i, i_y0);
		cairo_line_to (cr, x0 + i, i_y0 + sy1);
		cairo_stroke (cr);
	}
	cairo_set_line_width (cr, 4.0);
	x0 += .5;
	for (; i < 4 * sx1; i += 8) {
		cairo_move_to (cr, x0 + i, i_y0);
		cairo_line_to (cr, x0 + i, i_y0 + sy1);
		cairo_stroke (cr);
	}
	cairo_set_line_width (cr, 5.0);
	x0 += .5;
	for (; i < 5 * sx1 + 8; i += 8) {
		cairo_move_to (cr, x0 + i, i_y0);
		cairo_line_to (cr, x0 + i, i_y0 + sy1);
		cairo_stroke (cr);
	}

	// left-column horizontal stripes
	cairo_set_line_width (cr, 1.0);
	y0 = rint (i_y0 + sy1) + .5;
	for (i = 1; i < sy1; i += 2) {
		cairo_move_to (cr, i_x0      , y0 + i);
		cairo_line_to (cr, i_x0 + sx1, y0 + i);
		cairo_stroke (cr);
	}
	cairo_set_line_width (cr, 2.0);
	y0 += .5;
	for (; i < 2 * sy1; i += 4) {
		cairo_move_to (cr, i_x0      , y0 + i);
		cairo_line_to (cr, i_x0 + sx1, y0 + i);
		cairo_stroke (cr);
	}
	cairo_set_line_width (cr, 3.0);
	y0 += .5;
	for (; i < 3 * sy1; i += 6) {
		cairo_move_to (cr, i_x0      , y0 + i);
		cairo_line_to (cr, i_x0 + sx1, y0 + i);
		cairo_stroke (cr);
	}
	cairo_set_line_width (cr, 4.0);
	y0 += .5;
	for (; i < i_y1 + 8; i += 8) {
		cairo_move_to (cr, i_x0      , y0 + i);
		cairo_line_to (cr, i_x0 + sx1, y0 + i);
		cairo_stroke (cr);
	}

	// top-left hash
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_width (cr, 1.1);
	x0 = rint (i_x0) + .5;
	y0 = rint (i_y0) + .5;
	for (i = 0; i < sx1; ++i) {
		for (j = 0; j < sy1; ++j) {
			if ((i + j) % 2 == 1) continue;
			cairo_move_to (cr, x0 + i, y0 + j);
			cairo_close_path (cr);
			cairo_stroke (cr);
		}
	}

	if (mode & 1) {
		// top-row whites
		x0 = i_x0 + sx1;
		x1 = (i_x1 - i_x0 - sx1) / 33.;
		y0 = i_y0 + sy1;
		y1 = sy1 * .5;
		for (i = 0; i <= 32; ++i) {
			const float col = (32 - i) / 32.;
			cairo_set_source_rgba (cr, col, col, col, 1.0);
			cairo_rectangle (cr, floor (x0 + i * x1), y0, ceil (x1 + 1), y1);
			cairo_fill (cr);
		}

		cairo_save (cr);
		cairo_translate (cr, x0, i_y0 + sy1 * 1.5);
		if (mode & 2)
			smpte02 (cr, (i_x1 - i_x0 - sx1), sy1 * 3.5);
		else
			smpte78 (cr, (i_x1 - i_x0 - sx1), sy1 * 3.5);
		cairo_restore (cr);
	}
	else
	{
		cairo_save (cr);
		cairo_translate (cr, i_x0, i_y0);
		if (mode & 2)
			smpte02 (cr, (i_x1 - i_x0),  (i_y1 - i_y0));
		else
			smpte78 (cr, (i_x1 - i_x0),  (i_y1 - i_y0));
		cairo_restore (cr);
	}

	cairo_restore(cr);


	// TOP Layer -- image bounds
	float cross_len = h / 20.0;
	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 1.0);
	cairo_set_line_width (cr, 1.5);
	cairo_move_to (cr, cxp - cross_len, cyp);
	cairo_line_to (cr, cxp + cross_len, cyp);
	cairo_stroke (cr);
	cairo_move_to (cr, cxp, cyp - cross_len);
	cairo_line_to (cr, cxp, cyp + cross_len);
	cairo_stroke (cr);

	float arrowscale = h / 720.;
	triangle (cr, cx, h - 32 * arrowscale, 0, arrowscale);
	triangle (cr, cx, h - 64 * arrowscale, 0, arrowscale);
	triangle (cr, cx, h - 96 * arrowscale, 0, arrowscale);

	triangle (cr, cx, 32 * arrowscale, M_PI, arrowscale);
	triangle (cr, cx, 64 * arrowscale, M_PI, arrowscale);
	triangle (cr, cx, 96 * arrowscale, M_PI, arrowscale);

	triangle (cr, 32 * arrowscale, cy, M_PI / 2, arrowscale);
	triangle (cr, 64 * arrowscale, cy, M_PI / 2, arrowscale);
	triangle (cr, 96 * arrowscale, cy, M_PI / 2, arrowscale);

	triangle (cr, w - 32 * arrowscale, cy, M_PI * 3 / 2, arrowscale);
	triangle (cr, w - 64 * arrowscale, cy, M_PI * 3 / 2, arrowscale);
	triangle (cr, w - 96 * arrowscale, cy, M_PI * 3 / 2, arrowscale);
}

/*** part three: render Timecode on test-screen */

static void write_text (cairo_t* cr,
		const char *txt,
		const float x, const float y, const int align)
{
	int tw, th;
	cairo_save (cr);
	PangoLayout * pl = pango_cairo_create_layout (cr);

	pango_layout_set_font_description (pl, font_desc);

	pango_layout_set_text (pl, txt, -1);
	pango_layout_get_pixel_size (pl, &tw, &th);
	cairo_translate (cr, x, y);

	switch (align) {
		case 1: // right + middle
			cairo_translate (cr, -tw, -th / 2.0);
			break;
		case 0: // left + middle
			cairo_translate (cr, 0, -th / 2.0);
			break;
		case -1: // center + middle
			cairo_translate (cr, -tw / 2.0, -th / 2.0);
			break;
	}

	pango_cairo_layout_path (cr, pl);
	cairo_set_line_width (cr, 2.5);
	cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.8);
	cairo_stroke_preserve (cr);
	cairo_set_line_width (cr, 0.5);
	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.8);
	cairo_stroke_preserve (cr);
	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 1.0);
	cairo_fill (cr);
	g_object_unref (pl);
	cairo_restore (cr);
	cairo_new_path (cr);
}

static void annotate (cairo_t* cr,
		const float w, const float h,
		TimecodeRate *r, const char *text)
{
	char tmp[64];

	const float i_y0 = h / 12.;
	const float i_x0 = w / 8.;
	const float i_x1 = w * 7. / 8.;
	const float sx1  = (i_x1 - i_x0) / 6.;
	const float x0 = sx1 * .25;

	sprintf (tmp, "%.0fx%.0f", w, h);
	write_text (cr, tmp, x0, i_y0 * .5, 0);

	sprintf (tmp, "%.3f fps", (r->fps.num / (float)r->fps.den));
	write_text (cr, tmp, w - x0, i_y0 *.5, 1);

	if (strlen (text) > 0) {
		write_text (cr, text, w * .5, i_y0 * .5, -1);
	}
}

static void splash (cairo_t* cr,
		const float w, const float h,
		TimecodeRate *r, int64_t fn_start, int64_t fn_end,
		const char *title)
{
	TimecodeTime tc;
	char tmp[64];
	char tcs[13], tce[13];

	framenumber_to_timecode (&tc, r, fn_start);
	format_tc (tcs, r, &tc);
	framenumber_to_timecode (&tc, r, fn_end -1);
	format_tc (tce, r, &tc);

	const float x0 = w/2;
	const float y0 = h/2;

	int ln = h/11;
	int lo = strlen (title) > 0 ? 0 : h/22;

	sprintf (tmp, "Start: %s", tcs);
	write_text (cr, tmp, x0, y0 - ln + lo, -1);

	sprintf (tmp, "End:   %s", tce);
	write_text (cr, tmp, x0, y0 + lo, -1);

	if (strlen (title) > 0) {
		write_text (cr, title, x0, y0 + ln, -1);
	}
}


static void timecode (cairo_t* cr,
		const float w, const float h,
		TimecodeRate *r,
		int64_t fn
		)
{

	int64_t i;
	char tmp[64];

	const float cx = w * .5;
	const float cy = h * .5;
	const float i_x0 = w / 8.;
	const float i_y0 = h / 12.;
	const float i_x1 = w * 7. / 8.;
	const float i_y1 = h * 11. / 12.;
	const float sx1  = (i_x1 - i_x0) / 6.;
	const float sy1  = (i_y1 - i_y0) / 5.;

	float x0, x1;
	float y0, y1;

	int tcn = ceil (r->fps.num / (double)r->fps.den);
	int tcm = 1;
	if (tcn < 40) {
		tcn *= 2;
		tcm = 2;
	}
	// TIME CIRCLE
	const float c_rad = h / 2.9;
	float rad = M_PI * c_rad / (tcn * 6. / 5.);
	for (i = 0; i < tcn; ++i) {
		cairo_save (cr);
		const float col = (tcn - i) / (float) tcn;
		cairo_set_source_rgba (cr, col, col, col, .4);
		cairo_translate (cr, cx, cy);
		cairo_rotate (cr, 2 * M_PI * ((i + 1 + tcm * fn) % tcn)  / (float)tcn);
		cairo_translate (cr, 0, -c_rad);
		cairo_arc (cr, 0, 0, rad, 0, 2 * M_PI);
		cairo_fill (cr);
		cairo_restore (cr);
	}

	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.5);
	cairo_arc (cr, cx, cy, c_rad, 0, 2 * M_PI);
	cairo_set_line_width (cr, 1.2);
	cairo_stroke (cr);

	// b/w box to indicate frame progression
	y0 = i_y1 - sy1 * .5;
	y1 = sy1 * .3;
	x0 = sx1 * .25;
	x1 = sx1 * .5;

	for (i = 0; i < 2; ++i) {
		if ((i + fn) % 2) {
			cairo_set_source_rgba (cr, 0, 0, 0, 0.7);
		} else {
			cairo_set_source_rgba (cr, 1, 1, 1, 0.7);
		}
		const float r = rint (x0 + (i + 1) * x1) - rint (x0 + i * x1);
		cairo_rectangle (cr, rint (x0 + i * x1), y0, r, ceil (y1));
		cairo_fill (cr);
	}

	y0 = i_y1 - sy1 * .8;
	x1 = sx1 * .25;
	for (i = 0; i < 4; ++i) {
		if ((4 + fn - i) % 4) {
			cairo_set_source_rgba (cr, 0, 0, 0, 0.7);
		} else {
			cairo_set_source_rgba (cr, 1, 1, 1, 0.7);
		}
		const float r = rint (x0 + (i + 1) * x1) - rint (x0 + i * x1);
		cairo_rectangle (cr, rint (x0 + i * x1), y0, r, ceil (y1));
		cairo_fill (cr);
	}

	// timecode & framenumber
	sprintf (tmp, "%"PRId64, fn);
	write_text (cr, tmp, x0, i_y1 + 4, 0);

	TimecodeTime tc;
	framenumber_to_timecode (&tc, r, fn);
	format_tc (tmp, r, &tc);
	write_text (cr, tmp, w - x0, i_y1 + 4, 1);
}

#ifdef CUSTOM_PNG_WRITER
/*** custom png writer
 * zlib deflate in cairo_surface_write_to_png() is the performance bottleneck
 * also, the image is known to be flat - no alpha layer.
 */
#if 0 // replacement for png_set_bgr()
static void convert_to_rgb (png_structp png, png_row_infop row_info, png_bytep data) {
	unsigned int i, j;
	for (i = 0; i < row_info->rowbytes; i += 4, j += 3) {
		uint8_t *b = &data[i];
		uint32_t p;

		memcpy (&p, &data[i], sizeof (uint32_t));

		b[0] = (p & 0xff0000) >> 16;
		b[1] = (p & 0x00ff00) >>  8;
		b[2] = (p & 0x0000ff) >>  0;
	}
}
#endif

static int write_png (cairo_surface_t *cs, const char *filename, int compression) {
	int i;
	int rv = 0;
	unsigned char * img_data;
	FILE *x;
	const int w = cairo_image_surface_get_width (cs);
	const int h = cairo_image_surface_get_height (cs);
	const int s = cairo_image_surface_get_stride (cs);

	if (cairo_image_surface_get_format (cs) != CAIRO_FORMAT_ARGB32) {
		fprintf (stderr, "unsupported image format\n");
		return -1;
	}

	cairo_surface_flush (cs);
	img_data = cairo_image_surface_get_data (cs);

	png_byte ** rows = NULL;
	png_struct *png;
	png_info *info;

	rows = malloc (h * sizeof (png_byte*));
	if (!rows) {
		return -1;
	}

	for (i = 0; i < h; ++i)
		rows[i] = (png_byte *) img_data + i * s;


	png = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		rv = 1;
		goto BAIL1;
	}
	info = png_create_info_struct (png);
	if (!info) {
		rv = 1;
		goto BAIL2;
	}

	if (setjmp (png_jmpbuf (png))) {
		rv = 1;
		goto BAIL2;
	}

	if (!(x = fopen (filename, "wb"))) {
		rv = 1;
		goto BAIL2;
	}

	png_init_io (png, x);

	if (compression >= 0 && compression <= 9)
		png_set_compression_level (png, compression);

	png_set_IHDR (png, info, w, h, 8, PNG_COLOR_TYPE_RGB,
			PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT,
			PNG_FILTER_TYPE_DEFAULT);


	// explicit white balance
	png_color_16 white;
	white.gray = (1 << 8) - 1;
	white.red = white.blue = white.green = white.gray;
	png_set_bKGD (png, info, &white);

	png_write_info (png, info);
#if 0
	png_set_write_user_transform_fn (png, convert_to_rgb);
#else
	png_set_bgr(png);
#endif
	png_set_filler (png, 0xff, PNG_FILLER_AFTER);
	png_write_image (png, rows);
	png_write_end (png, info);

	fclose (x);
BAIL2:
	png_destroy_write_struct (&png, &info);
BAIL1:
	free (rows);
	return rv;
}
#endif

/*** thread worker */

static pthread_mutex_t  cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  thr_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int64_t frame_cnt;
static volatile int     run_cnt;

typedef struct workNfo {
	pthread_t self;
	float w;
	float h;
	int64_t wk_start;
	int64_t wk_end;
	int64_t fn_start;
	int64_t fn_end;
	TimecodeRate *rate;
	cairo_surface_t *bg;
	const char * title_text;
	const char * destdir;
	const char * nameprefix;
	int compression;
} workNfo;

static void * worker (void *arg) {
	workNfo const * const n = (workNfo const * const) arg;
	int64_t i = 0;
	char filename[1024] = "";
	cairo_surface_t * ct;
	cairo_t* cr;

	//localize variables
	const float w = n->w;
	const float h = n->h;
	const int64_t fn_start = n->fn_start;
	const int64_t wk_start = n->wk_start;
	const int64_t wk_end   = n->wk_end;
	const int compression = n->compression;

	ct = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, w, h);
	cr = cairo_create (ct);

	for (i = wk_start; i < wk_end; ++i) {
		cairo_set_source_surface (cr, n->bg, 0, 0);
		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint (cr);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		timecode (cr, w, h, n->rate, i + fn_start);

		if (i == 0 && wk_start == 0) {
			splash (cr, w, h, n->rate, n->fn_start, n->fn_end, n->title_text);
		}

		sprintf (filename, "%s/%s%08"PRId64".png", n->destdir, n->nameprefix, i);
#ifdef CUSTOM_PNG_WRITER
		if (write_png (ct, filename, compression))
#else
		if (cairo_surface_write_to_png (ct, filename))
#endif
		{
			fprintf (stderr, "Writing to '%s' failed\n", filename);
			break;
		}
		pthread_mutex_lock (&cnt_mutex);
		++frame_cnt;
		pthread_mutex_unlock (&cnt_mutex);
	}

	cairo_destroy (cr);
	cairo_surface_destroy (ct);

	pthread_mutex_lock (&thr_mutex);
	--run_cnt;
	pthread_mutex_unlock (&thr_mutex);

	pthread_exit (0);
	return NULL;
}

/*** main application code and helpers */

static int test_dir (char *d) {
	struct stat s;
	int result = stat (d, &s);
	if (result != 0) return 1;
	if (!S_ISDIR(s.st_mode)) return 1; /* is not a directory file */
	if (s.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))  return 0; /* is writeable */
	return 1;
}


static void usage (int status) {
	printf ("tsmm2 - time stamped movie maker.\n\n");
	printf ("Usage: tsmm2 [ OPTIONS ] <dirname>\n\n");
	printf ("Options:\n\
  -a, --aspect-ratio <num>[/den]\n\
                            set aspect ratio (default 16:9)\n\
                            as SAR = 1, this defines the image width\n\
  -b, --no-border           do not render border nor alignment markers\n\
  -c, --color-only          do not render stripe patterns\n\
  -C, --compression <c>     PNG/zlib compression level (0-9)\n\
                            0: no compression, 1: fastest, 9: best\n\
  -d, --duration <sec>      set duration in seconds (default: 5)\n\
  -f, --fps <num>[/den]     set frame-rate (default: 25/1)\n\
  -F, --font <name>         font for timecode and info\n\
                            default: DroidSansMono\n\
  -h, --help                display this help and exit\n\
  -H, --height <px>         specify image height (default: 360)\n\
  -j, --concurrency <n>     number of parallel jobs (default: 2)\n\
  -n, --name-prefix <txt>   filename prefix (default: 't')\n\
  -p, --progress            report progress\n\
  -s, --start-frame <fn>    specify timecode start frame number\n\
                            (default: 0)\n\
  -S, --smpte-hdv           Use SMPTE RP 219:2002 color bars instead\n\
                            of SMPTE ECR 1-1978\n\
  -T, --title-text <txt>    Specify some text to appear on the first\n\
                            frame. Default: URL to this app.\n\
  -v, --verbose             print info and report progress\n\
  -V, --version             print version information and exit\n\
\n");
/*-------------------------------------------------------------------------------|" */
	printf ("\n\
This tool is intended to create reference video test patterns with on-screen\n\
timecode to ensure technical quality of production.\n\
\n\
Tsmm2 can generate color bars in SMPTE ECR 1-1978 and SMPTE RP 219:2002 style,\n\
line patterns and add a frame number + timecode overlay. The configuration\n\
if flexible and the geometry of the resulting video variable.\n\
\n\
Despite the name, tsmm2 only creates a video frame sequence which can then\n\
be encoded into a video-file using e.g ffmpeg or mencoder or similar tools.\n\
This allows to derive multiple format/codec combinations from the same image\n\
sequence. Upon completion, tsmm2 suggests a simple ffmpeg encode command.\n\
\n\
Common frame-heights for 16:9 aspect:\n\
 1080, 720, 540, 360, 288, 270, 180\n\
Common frame-heights for 4:3 aspect:\n\
 576, 240\n\
Standard Framerates:\n\
 60/1, 50/1, 30/1, 25/1, 24/1\n\
 60000/1001, 30000/1001, 25000/1001, 24000/1001\n\
(29.97 ie 30000/1001 always used drop-frame timecode)\n\
\n\
The speed limiting factor of this tool is PNG compression. When built with\n\
zlib/png support, the -C option provides some control over this. Since the\n\
images are encoded in a subsequent step, reducing PNG compression to a minimum\n\
is highly recommended and also the default (-C 1).\n\
Disabling compression completely (-C 0) will only result in a marginal speed\n\
improvement compared to -C 1 and result in huge files.\n\
libcairo's default (when this tool is built without zlib/png support) is -C 6.\n\
\n\
Examples:\n\
 mkdir /tmp/tsmm2;\n\
 tsmm2 -v -f 30/1 -H 720 -d 300 /tmp/tsmm2;\n\
 ffmpeg -r 30/1 -i /tmp/tsmm2/t%%08d.png /tmp/tsmm2.mp4\n\
\n");
	printf ("Report bugs to Robin Gareus <robin@gareus.org>\n"
	        "Website and tracker: <https://github.com/x42/tsmm2>\n");
	exit (status);
}

static struct option const long_options[] =
{
	{"aspect-ratio", required_argument, 0, 'a'},
	{"no-border",    no_argument, 0, 'b'},
	{"color-only",   no_argument, 0, 'c'},
	{"compression",  required_argument, 0, 'C'},
	{"duration",     required_argument, 0, 'd'},
	{"fps",          required_argument, 0, 'f'},
	{"font",         required_argument, 0, 'F'},
	{"help",         no_argument, 0, 'h'},
	{"height",       required_argument, 0, 'H'},
	{"concurrency",  required_argument, 0, 'j'},
	{"name-prefix",  required_argument, 0, 'n'},
	{"progress",     no_argument, 0, 'p'},
	{"start-frame",  required_argument, 0, 's'},
	{"smpte-hdv",    no_argument, 0, 'S'},
	{"frame-text",   required_argument, 0, 't'},
	{"title-text",   required_argument, 0, 'T'},
	{"verbose",      no_argument, 0, 'v'},
	{"version",      no_argument, 0, 'V'},
	{NULL, 0, NULL, 0}
};

int main (int argc, char **argv) {
	int i;
	uint8_t verbose = 0;
	uint8_t mode = 1; // 0x1: add stripes, 0x2: use HDV/219:2002, 0x04: no border
	float w, h;
	int64_t fn_start, fn_end;
	double duration; // seconds
	char destdir[1024];
	char frame_text[128] = "";
	char title_text[128] = "http://gareus.org/t/tsmm2";
	char fontname[120] = FONTFILE;
	char nameprefix[32] = "t";
	char font[128];
	TimecodeRate rate;
	Rational aspect;
#ifdef CUSTOM_PNG_WRITER
	int compression = Z_BEST_SPEED; // Z_BEST_COMPRESSION
#endif
	int jobs;

	/* defaults */
	destdir[0] = '\0';
	rate.fps.num = 25;
	rate.fps.den = 1;
	rate.drop = 0;
	aspect.num = 16;
	aspect.den = 9;
	h = 360;
	fn_start = 0;
	duration = 5.0;
	jobs = 2;

	int c;
	while ((c = getopt_long (argc, argv,
			   "a:" /* aspect */
			   "b"  /* no-border */
			   "c"  /* color-only */
			   "C:" /* compression */
			   "f:" /* fps */
			   "F:" /* font */
			   "d:" /* duration */
			   "h"  /* help */
			   "H:" /* height */
			   "j:" /* concurrency */
			   "n:" /* name-prefix */
			   "p"  /* progress */
			   "s:" /* start-frame */
			   "S"  /* smpte-hdv */
			   "t:" /* frame-text */
			   "T:" /* title-text */
			   "v"  /* verbose */
			   "V", /* version */
			   long_options, (int *) 0)) != EOF)
	{
		switch (c) {
			case 'a':
				{
					aspect.num = atoi (optarg);
					aspect.den = 1;
					char *tmp = strchr (optarg, ':');
					if (tmp)
						aspect.den = atoi (++tmp);
					else if ((tmp = strchr (optarg, '/')))
						aspect.den = atoi (++tmp);
				}
				break;

			case 'b':
				mode |= 4;
				break;

			case 'c':
				mode &= ~1;
				break;

			case 'C':
#ifdef CUSTOM_PNG_WRITER
				compression = atoi (optarg);
#else
				fprintf (stderr, "zlib/png is not supported in this version, -C ignored.\n");
#endif
				break;

			case 'd':
				duration = atof (optarg);
				break;

			case 'f':
				{
					rate.fps.num = atoi (optarg);
					rate.fps.den = 1;
					char *tmp = strchr (optarg, '/');
					if (tmp) rate.fps.den=atoi (++tmp);
				}
				break;

			case 'F':
				strncpy (fontname, optarg, sizeof(fontname));
				fontname[sizeof(fontname) -1 ] = '\0';
				break;

			case 'H':
				h = atoi (optarg);
				break;

			case 'j':
				jobs = atoi (optarg);
				break;

			case 'n':
				strncpy (nameprefix, optarg, sizeof(nameprefix));
				nameprefix[sizeof(nameprefix) -1 ] = '\0';
				break;

			case 'p':
				verbose |= 2;
				break;

			case 's':
				fn_start = atoi (optarg);
				break;

			case 'S':
				mode |= 2;
				break;

			case 't':
				strncpy (frame_text, optarg, sizeof(frame_text));
				frame_text[sizeof(frame_text) -1 ] = '\0';
				break;

			case 'T':
				strncpy (title_text, optarg, sizeof(title_text));
				title_text[sizeof(title_text) -1 ] = '\0';
				break;

			case 'v':
				verbose |= 3;
				break;

			case 'V':
				printf ("tsmm2 version %s\n\n", VERSION);
				printf ("Copyright (C) GPL 2012,2014 Robin Gareus <robin@gareus.org>\n");
				printf ("This is free software; see the source for copying conditions.  There is NO\n");
				printf ("warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");
				exit (0);

			case 'h':
				usage (0);

			default:
				usage (EXIT_FAILURE);
		}
	}

	if (optind >= argc) {
		usage (EXIT_FAILURE);
	}

	strncpy (destdir, argv[optind], sizeof(destdir));
	destdir[sizeof(destdir) -1 ] = '\0';

	// sanity checks, part one
	if (aspect.num < 1 || aspect.den < 1) {
		fprintf (stderr, "Error: Invalid aspect ratio %d : %d\n", aspect.num, aspect.den);
		return -1;
	}
	if (rate.fps.num < 1 || rate.fps.den < 1) {
		fprintf (stderr, "Error: Invalid frame-rate %d / %d\n", rate.fps.num, rate.fps.den);
		return -1;
	}
	if (rate.fps.num / (float)rate.fps.den < 1.0) {
		fprintf (stderr, "Error: Frame-rate %d / %d is less than 1.0 fps\n", rate.fps.num, rate.fps.den);
		return -1;
	}
	if (strlen (destdir) < 1) {
		fprintf (stderr, "Error: No destination dir is given\n");
		return -1;
	}
	if (test_dir (destdir)) {
		if (verbose & 1) {
			printf ("Note: Destination dir does not exists.\n");
			printf ("Note: Trying to create dir '%s'\n", destdir);
		}
		mkdir (destdir, 0755);
	}
	if (test_dir (destdir)) {
		fprintf (stderr, "Error: Destination dir does not exists or lacks write permissions.\n");
		return -1;
	}
	if (strlen (destdir) > 1 && destdir[strlen (destdir) - 1] == DIRSEP) {
		destdir[strlen (destdir) - 1] = '\0';
	}

	// derive values
	h = rintf (h);
	w = rintf (h * aspect.num / (float)aspect.den);
	fn_end = fn_start + rintf (duration * (float)rate.fps.num / (float)rate.fps.den);

	if (rintf (100. * rate.fps.num / (float)rate.fps.den) == 2997) {
		rate.drop = 1;
	}
	jobs = MAX(1, MIN(jobs, fn_end - fn_start));

	// sanity checks, part two

	if (h < 80 || w < 80) {
		fprintf (stderr, "Error: Geometry is too small: %.0f x %.0f\n", w, h);
		return -1;
	}
	if (fn_end <= fn_start) {
		fprintf (stderr, "Error: Zero duration, no frames to write.\n");
		return -1;
	}

	// all systems go...
	if (verbose & 1) {
		char tcs[13], tce[13];
		TimecodeTime tc;
		printf ("* Geometry:    %.0f x %.0f px\n", w, h);
		printf ("* Framerate:   %d / %d (%.3f) %s timecode\n",
				rate.fps.num, rate.fps.den,
				(rate.fps.num / (double)rate.fps.den),
				rate.drop ? "drop-frame" : "non-drop-frame");
		framenumber_to_timecode (&tc, &rate, fn_start);
		format_tc (tcs, &rate, &tc);
		framenumber_to_timecode (&tc, &rate, fn_end -1);
		format_tc (tce, &rate, &tc);
		printf ("* Timecode:    %s -> %s\n", tcs, tce);
		printf ("* File first:  %s/%s%08d.png\n", destdir, nameprefix, 0);
		printf ("* File last:   %s/%s%08"PRId64".png\n", destdir, nameprefix, (fn_end - fn_start - 1));
		printf ("* Concurrency: %d\n", jobs);
	}

	snprintf (font, 128, "%s %d", fontname, MAX(6, (int)rint (h/22)));
	font_desc = pango_font_description_from_string (font);

	if (verbose & 2) {
		printf ("progress: %5.1f%%\r", 0.f);
		fflush (stdout);
	}

	// create static test-screen
	cairo_surface_t * cs = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, w, h);
	cairo_t* cr = cairo_create (cs);
	if (mode & 4) {
		if (mode & 2)
			smpte02 (cr, w, h);
		else
			smpte78 (cr, w, h);
	} else {
		testscreen (cr, w, h, mode);
	}
	annotate (cr, w, h, &rate, frame_text);
	cairo_destroy (cr);

	// render timecode

	int64_t spl = (fn_end - fn_start) / jobs;
	workNfo *nfo = malloc (jobs * sizeof(workNfo));

	int64_t off = 0;;
	for (i = 0; i < jobs; ++i) {
		nfo[i].w = w;
		nfo[i].h = h;
		nfo[i].rate = &rate;
		nfo[i].bg = cs;
		nfo[i].title_text = title_text;
		nfo[i].destdir = destdir;
		nfo[i].nameprefix = nameprefix;
		nfo[i].compression = compression;
		nfo[i].fn_start = fn_start;
		nfo[i].fn_end = fn_end;

		nfo[i].wk_start = off;
		off += spl;
		nfo[i].wk_end = (i == jobs - 1) ? (fn_end - fn_start) : (off);
	}

	frame_cnt = -1;
	run_cnt = 0;

	for (i = 0; i < jobs; ++i) {
		pthread_mutex_lock (&thr_mutex);
		++run_cnt;
		pthread_mutex_unlock (&thr_mutex);
		if (pthread_create (&nfo[i].self, NULL, worker, (void*) &nfo[i])) {
			fprintf (stderr, "Fatal error: Cannot start thread.\n");
			exit (1);
		}
	}

	while (run_cnt > 0) {
		usleep (250);
		if (verbose & 2 && frame_cnt > 0) {
			const float pr = 100.f * frame_cnt / (fn_end - fn_start - 1);
			printf ("progress: %5.1f%%\r", pr);
			fflush (stdout);
		}
	}

	for (i = 0; i < jobs; ++i) {
		pthread_join (nfo[i].self, NULL);
	}
	free (nfo);

	cairo_surface_destroy (cs);
	pango_font_description_free (font_desc);

	if (verbose & 2) {
		printf ("progress: %5.1f%%\n", 100.f * frame_cnt / (fn_end - fn_start - 1));
	}

	if (verbose & 1) {
		char filename[1024] = "";
		sprintf (filename, "%s/%s%08"PRId64".png", destdir, nameprefix, frame_cnt);
		printf ("* Wrote %"PRId64" files. Last '%s'\n", frame_cnt, filename);
		printf (
				"* Encode movie with e.g.\n"
				" ffmpeg -r %d/%d -i %s/%s%%08d.png -qscale:v 0 %s.avi\n",
				rate.fps.num, rate.fps.den, destdir, nameprefix, destdir);
	}

#if 0 // suggest audio if duration > 2 sec
	const int sr = rint (48000. * (float)rate.fps.den * rint (rate.fps.num / (float) rate.fps.den) / (float)rate.fps.num);
	const int ds = rint (48000. * rate.fps.den / (float) rate.fps.num);
	const int dr = sr - ds;
	printf (
			" sox -n -r %d -c 2 -b16 silence1.wav trim 0.0 1.0 # prefix, 1st second\n"
			" sox -n -r %d -c 2 -b16 sin1k.wav synth %ds sine 1000 gain -18 fade 0 0 24s\n"
			" sox -n -r %d -c 2 -b16 silence.wav trim 0.0 %ds\n"
			" sox sin1k.wav silence.wav onesec.wav\n"
			" sox onesec.wav soundtrack1.wav repeat %d\n"
			" sox silence1.wav soundtrack1.wav soundtrack.wav\n"
			" rm silence1.wav sin1k.wav silence.wav onesec.wav soundtrack1.wav\n"
			,sr
			,sr, ds
			,sr, dr
			, (int)floor ((fn_end - fn_start) / ceil (rate.fps.num / (float)rate.fps.den) - 2)
			);
	printf (
			" ffmpeg -r %d/%d -i %s/%s%%08d.png -i soundtrack.wav -shortest -preset placebo -pix_fmt yuv420p -strict -2 %s.mp4\n"
			" rm soundtrack.wav\n"
			" rm -rf %s/\n"
			, rate.fps.num, rate.fps.den, destdir, nameprefix, destdir, destdir);

#endif
	return 0;
}
