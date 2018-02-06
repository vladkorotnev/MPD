/*
 * Copyright (C) 2003-2015 The Music Player Daemon Project
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

#include "AudioFormat.hxx"

#include <assert.h>
#include <stdio.h>

void
AudioFormat::ApplyMask(AudioFormat mask)
{
	assert(IsValid());
	assert(mask.IsMaskValid());

	if (mask.sample_rate != 0)
		sample_rate = mask.sample_rate;

	if (mask.format != SampleFormat::UNDEFINED)
		format = mask.format;

	if (mask.channels != 0)
		channels = mask.channels;

	assert(IsValid());
}

const char *
sample_format_to_string(SampleFormat format)
{
	switch (format) {
	case SampleFormat::UNDEFINED:
		return "?";

	case SampleFormat::S8:
		return "8";

	case SampleFormat::S16:
		return "16";

	case SampleFormat::S24_P32:
		return "24";

	case SampleFormat::S32:
		return "32";

	case SampleFormat::FLOAT:
		return "16";

	case SampleFormat::DOUBLE:
		return "64";

	case SampleFormat::DSD:
		return "dsd";

#ifdef USE_ALSA_DOP
	case SampleFormat::DOP64:
		return "dop64";
	case SampleFormat::DOP128:
		return "dop128";
	case SampleFormat::DOP256:
		return "dop256";
	case SampleFormat::DOP512:
		return "dop512";
#endif
	}

	/* unreachable */
	assert(false);
	gcc_unreachable();
}

const char *
audio_format_to_string(const AudioFormat af,
		       struct audio_format_string *s)
{
	assert(s != nullptr);

	snprintf(s->buffer, sizeof(s->buffer), "%u:%s:%u",
		 af.sample_rate,
		 sample_format_to_string(af.format2 != SampleFormat::UNDEFINED ? af.format2 : af.format),
		 af.channels);

	return s->buffer;
}

snd_pcm_format_t
get_bitformat(SampleFormat sample_format)
{
	switch (sample_format) {
	case SampleFormat::UNDEFINED:
		return SND_PCM_FORMAT_UNKNOWN;

	case SampleFormat::DSD:
#ifdef HAVE_ALSA_DSD
		return SND_PCM_FORMAT_DSD_U8;
#else
		return SND_PCM_FORMAT_UNKNOWN;
#endif

	case SampleFormat::S8:
		return SND_PCM_FORMAT_S8;

	case SampleFormat::S16:
		return SND_PCM_FORMAT_S16;

	case SampleFormat::S24_P32:
		return SND_PCM_FORMAT_S24;

	case SampleFormat::S32:
		return SND_PCM_FORMAT_S32;

	case SampleFormat::FLOAT:
	case SampleFormat::DOUBLE:
		return SND_PCM_FORMAT_FLOAT;

#ifdef USE_ALSA_DOP
	case SampleFormat::DOP64:
		return SND_PCM_FORMAT_DOP64;

	case SampleFormat::DOP128:
		return SND_PCM_FORMAT_DOP128;

	case SampleFormat::DOP256:
		return SND_PCM_FORMAT_DOP256;

	case SampleFormat::DOP512:
		return SND_PCM_FORMAT_DOP512;
#endif
	}

	assert(false);
	gcc_unreachable();
}
