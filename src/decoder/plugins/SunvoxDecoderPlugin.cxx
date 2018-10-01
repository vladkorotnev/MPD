/*
 * Copyright 2003-2017 The Music Player Daemon Project
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
#include "SunvoxDecoderPlugin.hxx"
#include "../DecoderAPI.hxx"
#include "input/InputStream.hxx"
#include "tag/Handler.hxx"
#include "util/WritableBuffer.hxx"
#include "util/Domain.hxx"
#include "util/RuntimeError.hxx"
#include "Log.hxx"

#include "sunvox.h"

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#define SUNVOX_PLAY_SLOT 0
#define SUNVOX_SCAN_SLOT 1

int g_sv_channels_num = 2; //1 - mono; 2 - stereo
int g_sv_buffer_size = 4096; //Audio buffer size (number of frames)

static constexpr size_t SUNVOX_PREALLOC_BLOCK = 256 * 1024;
static constexpr offset_type SUNVOX_FILE_LIMIT = 100 * 1024 * 1024;

static constexpr Domain sun_domain("sunvox");
static int sunvox_sample_rate = 44100;

static bool
sunvox_decoder_init(const ConfigBlock &block)
{
	LogWarning(sun_domain, "Init sunvox plugin...");
	
	/*if ( sv_load_dll() ) {
		LogWarning(sun_domain, "Load sunvox plugin failed");
		throw FormatRuntimeError("Cannot load sunvox library.");
	}*/
	
	sunvox_sample_rate = block.GetBlockValue("svsamplerate", sunvox_sample_rate);
	const unsigned int flags = SV_INIT_FLAG_USER_AUDIO_CALLBACK | SV_INIT_FLAG_AUDIO_INT16;
	 int ver = sv_init( 0, sunvox_sample_rate, 2, flags );
    if( ver >= 0 )
    {
		int major = ( ver >> 16 ) & 255;
		int minor1 = ( ver >> 8 ) & 255;
		int minor2 = ( ver ) & 255;
		printf( "SunVox lib version: %d.%d.%d\n", major, minor1, minor2 );		
	} else {
		LogWarning(sun_domain, "Init sunvox plugin failed");
		throw FormatRuntimeError("Cannot initialize SunVox library.");
	}
	
	return true;
}

static WritableBuffer<uint8_t>
sun_loadfile(DecoderClient *client, InputStream &is)
{
	//known/unknown size, preallocate array, lets read in chunks

	const bool is_stream = !is.KnownSize();

	WritableBuffer<uint8_t> buffer;
	if (is_stream)
		buffer.size = SUNVOX_PREALLOC_BLOCK;
	else {
		const auto size = is.GetSize();

		if (size == 0) {
			LogWarning(sun_domain, "file is empty");
			return nullptr;
		}

		buffer.size = size;
	}

	buffer.data = new uint8_t[buffer.size];

	uint8_t *const end = buffer.end();
	uint8_t *p = buffer.begin();

	while (true) {
		size_t ret = decoder_read(client, is, p, end - p);
		if (ret == 0) {
			if (is.LockIsEOF())
				/* end of file */
				break;

			/* I/O error - skip this song */
			delete[] buffer.data;
			buffer.data = nullptr;
			return buffer;
		}

		p += ret;
		if (p == end) {
			if (!is_stream)
				break;

			delete[] buffer.data;
			buffer.data = nullptr;
			return buffer;
		}
	}

	buffer.size = p - buffer.data;
	return buffer;
}

static bool 
LoadSunVoxFile(DecoderClient *client, InputStream &is, int slot)
{
	const auto buffer = sun_loadfile(client, is);
	if (buffer.IsNull()) {
		LogWarning(sun_domain, "SunVox library couldn't load stream");
		return false;
	}
	bool f = true;
	
	if( sv_load_from_memory( slot, buffer.data, buffer.size ) == 0 )
	    LogDefault(sun_domain,  "SunVox Loaded Song." );
	else {
	    LogWarning(sun_domain, "Cannot load Sunvox Song");
	    f = false;
	}
	
	delete[] buffer.data;
	return f;
}

static void
sun_decode(DecoderClient &client, InputStream &is)
{
	signed int * audio_buffer = (signed int*)malloc( g_sv_buffer_size * 2 * sizeof( signed int ) ); ;
	sv_open_slot( SUNVOX_PLAY_SLOT );
	bool couldLoad = LoadSunVoxFile(&client, is, SUNVOX_PLAY_SLOT);
	if(!couldLoad) {
		LogWarning(sun_domain, "could not decode stream!");
		sv_close_slot( SUNVOX_PLAY_SLOT );
		return;
	}
	static AudioFormat audio_format(sunvox_sample_rate, SampleFormat::S16, 2);
	assert(audio_format.IsValid());
	
	unsigned long frameCount = sv_get_song_length_frames( SUNVOX_PLAY_SLOT );
	unsigned long msecs = ((frameCount*1000) / sunvox_sample_rate);
	//printf("From %lu frames at %i Hz -> %lu msec\n", frameCount, sunvox_sample_rate, msecs);
	client.Ready(audio_format, true /*canSeek*/,
		     SongTime::FromMS(msecs));
		     
	sv_set_autostop(SUNVOX_PLAY_SLOT,1);
	sv_play_from_beginning(SUNVOX_PLAY_SLOT);
		     
	DecoderCommand cmd;
	unsigned int t =0 ;
	do {
		
		 t= sv_get_ticks();
		sv_audio_callback( audio_buffer, g_sv_buffer_size/2, 0, t );
		cmd = client.SubmitData(nullptr,
					audio_buffer, g_sv_buffer_size*2,
					0);
		if (cmd == DecoderCommand::SEEK) {
			
			unsigned int seekpos =  client.GetSeekTime().ToMS()/1000;
			printf("Want to seek at %u", seekpos);
			unsigned int line_pos = 0;
			
			unsigned int ticks_per_sec = sv_get_ticks_per_second()/1000;
			printf(" -> at %u TPS", ticks_per_sec);
			unsigned int ticks_per_line = sv_get_song_tpl( SUNVOX_PLAY_SLOT );
			printf(" and %u TPL", ticks_per_line);
			
			unsigned int lines_per_sec = ticks_per_sec / ticks_per_line;
			printf(" -> LPS: %u", lines_per_sec);
			
			
			line_pos = seekpos * lines_per_sec;
			printf(" -> goto %u\n", line_pos);
			
			sv_rewind( SUNVOX_PLAY_SLOT, line_pos );
			sv_play (SUNVOX_PLAY_SLOT);
			
			client.CommandFinished();
		} 
		if(sv_end_of_song(SUNVOX_PLAY_SLOT) && t > 0) {
				// there may be some buffer left
				usleep( 500000 * g_sv_buffer_size / sunvox_sample_rate);
			break;
		}
	} while (cmd != DecoderCommand::STOP);
	
	sv_stop ( SUNVOX_PLAY_SLOT );
	sv_close_slot( SUNVOX_PLAY_SLOT );
}

static bool
sunvox_scan_stream(InputStream &is, TagHandler &handler) noexcept
{
	sv_open_slot(SUNVOX_SCAN_SLOT);
	bool couldLoad = LoadSunVoxFile(nullptr, is, SUNVOX_SCAN_SLOT);
	if (!couldLoad) {
		sv_close_slot(SUNVOX_SCAN_SLOT);
		return false;
	}
	
	unsigned long frameCount = sv_get_song_length_frames( SUNVOX_SCAN_SLOT );
	unsigned long msecs = ((frameCount*1000) / sunvox_sample_rate);

	handler.OnDuration(SongTime::FromMS(msecs));

	const char *title = sv_get_song_name( SUNVOX_SCAN_SLOT );
	if (title != nullptr)
		handler.OnTag(TAG_TITLE, title);

	sv_close_slot( SUNVOX_SCAN_SLOT );

	return true;
}

static const char *const sun_suffixes[] = {
	"sunvox", /* Really pointless to add other module files -- there are better decoders out there for them */
	nullptr
};

const struct DecoderPlugin sunvox_decoder_plugin = {
	"sunvox",
	sunvox_decoder_init,
	nullptr,
	sun_decode,
	nullptr,
	nullptr,
	sunvox_scan_stream,
	nullptr,
	sun_suffixes,
	nullptr,
};
