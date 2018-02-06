/*
 * Copyright (C) 2003-2017 The Music Player Daemon Project
 * http://www.musicpd.org
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
 
#include "config.h"
#include "SourceInternal.hxx"
#include "util/Macros.hxx"
#include "AudioFormat.hxx"

#include <assert.h>

namespace Dms {

const char *channel_uri_tbl[] = {
	"master",
	"optical",
	"coaxial",
	"bluetooth",
	"line1",
	"line2",
	"line3",
};

const char *
channel_to_str(channel_t c)
{
	assert(c < ARRAY_SIZE(channel_uri_tbl));

	return channel_uri_tbl[(int)c];
}


static const unsigned samplerate_tbl[] = {
	0,
	44100,
	48000,
	88200,
	96000,
	176400,
	192000,
	352800,
	384000,
	705600,
	768000,
	44100*64/8,
	44100*128/8,
	44100*256/8,
	44100*512/8,
	0,
};

rate_t
format_to_rate(const AudioFormat &format)
{
	switch (format.format) {
	case SampleFormat::DSD:
		for (unsigned i=RATE_DSD_MIN;i<=RATE_DSD_MAX;i++) {
			if (format.sample_rate <= samplerate_tbl[i]) {
				return (rate_t)i;
			}
		}
		break;
	default:
		for (unsigned i=RATE_PCM_MIN;i<=RATE_PCM_MAX;i++) {
			if (format.sample_rate <= samplerate_tbl[i]) {
				return (rate_t)i;
			}
		}
		break;
	}

	return RATE_NONE;
}

unsigned
rate_to_unsigned(rate_t r)
{
	assert(r < ARRAY_SIZE(samplerate_tbl));

	return samplerate_tbl[r];
}


}
