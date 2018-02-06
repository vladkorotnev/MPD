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
 
#pragma once

#include "Compiler.h"

struct AudioFormat;

namespace Dms {

typedef enum DMS_CHANNEL {
	CHANNEL_MASTER,
	CHANNEL_OPTICAL,
	CHANNEL_COAXIAL,
	CHANNEL_BLUETOOTH,
	CHANNEL_LINE_1,
	CHANNEL_LINE_2,
	CHANNEL_LINE_3,
	CHANNEL_MAX,
} channel_t;

const char *channel_to_str(channel_t c);

typedef enum DMS_SOURCE {
	SOURCE_NONE,
// main
// usb
	SOURCE_USB_START,
	SOURCE_SD = SOURCE_USB_START,
	SOURCE_USB_1,
	SOURCE_USB_2,
	SOURCE_USB_3,
// network
	SOURCE_NETWORK_START,
	SOURCE_SMB = SOURCE_NETWORK_START,
	SOURCE_UPNP,
	SOURCE_PHONE,
// renderer
	SOURCE_RENDERER_START,
	SOURCE_DLNA = SOURCE_RENDERER_START,
	SOURCE_AIRPLAY,

// internet
	SOURCE_INTERNET_START,
	SOURCE_INTERNET = SOURCE_INTERNET_START,

// source
	SOURCE_SOURCE_START,
	SOURCE_USB_SOURCE = SOURCE_SOURCE_START,
	SOURCE_NETWORK_SOURCE,
	SOURCE_INTERNET_SOURCE,

// dac
	SOURCE_DAC_START,
	SOURCE_COAXIAL = SOURCE_DAC_START,
	SOURCE_OPTICAL,
	SOURCE_BLUETOOTH,
	SOURCE_LINE_1,
	SOURCE_LINE_2,
	SOURCE_LINE_3,
	SOURCE_MAX,

// startup only
	SOURCE_LAST_USED_START,
	SOURCE_NETWORK_LAST_USED = SOURCE_LAST_USED_START,
	SOURCE_INTERNET_LAST_USED,
} source_t;

enum DMS_RATE {
	RATE_BYPASS,
	RATE_44K1,
	RATE_MIN = RATE_44K1,
	RATE_PCM_MIN = RATE_44K1,
	RATE_48K,
	RATE_88K2,
	RATE_96K,
	RATE_176K4,
	RATE_192K,
	RATE_AES_MAX = RATE_192K,
	RATE_352K8,
	RATE_384K,
	RATE_705K6,
	RATE_768K,
	RATE_PCM_MAX = RATE_768K,
	RATE_DSD64,
	RATE_DSD_MIN = RATE_DSD64,
	RATE_DSD128,
	RATE_DSD256,
	RATE_MAX = RATE_DSD256,
	RATE_DSD512,
	RATE_DSD_MAX = RATE_DSD512,
	RATE_NONE,
};
typedef enum DMS_RATE rate_t;
typedef enum DMS_RATE src_t;

rate_t format_to_rate(const AudioFormat &format);

unsigned rate_to_unsigned(rate_t r);

}

