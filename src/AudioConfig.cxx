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

#include "config.h"
#include "AudioConfig.hxx"
#include "AudioFormat.hxx"
#include "AudioParser.hxx"
#include "config/Param.hxx"
#include "config/ConfigGlobal.hxx"
#include "config/ConfigOption.hxx"
#include "util/Error.hxx"
#include "system/FatalError.hxx"

static AudioFormat configured_audio_format;

AudioFormat
getOutputAudioFormat(AudioFormat inAudioFormat)
{
	AudioFormat out_audio_format = inAudioFormat;
	out_audio_format.ApplyMask(configured_audio_format);
	if (!out_audio_format.IsDSDOrDoP() &&
		out_audio_format.sample_rate == 705600 &&
		out_audio_format.format != SampleFormat::S24_P32) {
		out_audio_format.format = SampleFormat::S24_P32;
	}
	if (inAudioFormat.format == SampleFormat::DSD) {
		switch (inAudioFormat.sample_rate) {
		case 44100 * 64 / 8:
		case 44100 * 128 / 8:
		case 44100 * 256 / 8:
		case 44100 * 512 / 8:
			out_audio_format.format = SampleFormat::FLOAT;
			out_audio_format.sample_rate = 352800;
			out_audio_format.format2 = inAudioFormat.format;
			out_audio_format.sample_rate2 = inAudioFormat.sample_rate;
			break;
		default:
			break;
		}
	}
	return out_audio_format;
}

void initAudioConfig(void)
{
	const struct config_param *param = config_get_param(ConfigOption::AUDIO_OUTPUT_FORMAT);

	if (param == nullptr)
		return;

	Error error;
	if (!audio_format_parse(configured_audio_format, param->value.c_str(),
				true, error))
		FormatFatalError("error parsing line %i: %s",
				 param->line, error.GetMessage());
}
