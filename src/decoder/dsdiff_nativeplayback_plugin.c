/*
 * Copyright (C) 2003-2012 The Music Player Daemon Project
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

/* \file
 *
 * This plugin decodes DSDIFF data (SACD) embedded in DFF files and outputs it to
 * DSD-over-USB spec packed PCM
 * Used DSDIFF specs:
 * http://www.sonicstudio.com/pdf/dsd/DSDIFF_1.5_Spec.pdf
 *
 * Adapted from dsdiff_decoder_plugin.c by Jurgen Kramer to output to packed DSD (DSD-over-USB spec from dCS) instead of PCM.
 * Supports outputting to 24-bits or 32-bits PCM sampleformat.
 * Added native DSDIFF (.DFF files) tagging support and support for id3v1 tagformat found in files made with sacdextract
 * Added full seek support
 * Added DSF (.DSF files) support
 */

#include "config.h"
#include "dsdiff_nativeplayback_plugin.h"
#include "decoder_api.h"
#include "audio_check.h"
#include "dsdnative/dsdnative.h"

#include "tag_id3.h"

#include <unistd.h>
#include <stdio.h> /* for SEEK_SET, SEEK_CUR */

#ifdef BLASTER_SR
#include "decoder_control.h"
#include "decoder_internal.h"
#endif

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "dsdiff_native"


#ifdef HAVE_ID3TAG
#include <id3tag.h>
#endif


struct dsdiff_native_id {
	char value[4];
};

struct dsdiff_native_header {
	struct dsdiff_native_id id;
	uint32_t size_high, size_low;
	struct dsdiff_native_id format;
};

struct dsdiff_native_chunk_header {
	struct dsdiff_native_id id;
	uint32_t size_high, size_low;
};

struct dsdiff_native_header_dsf {		/* -- DSF file DSD chunk -- */
	struct dsdiff_native_id id;		/* "DSD " */
	uint32_t size_low, size_high;		/* DSD chunk size, including id = 28 */
	uint32_t fsize_low, fsize_high;		/* Total file size */
	uint32_t pmeta_low, pmeta_high;		/* Pointer to id3v2 metadata, should be at the at of the file */
};

struct dsdiff_native_dsf_fmt_chunk {		/* -- DSF file fmt chunk -- */
	struct dsdiff_native_id id;		/* "fmt " */
	uint32_t size_low, size_high;		/* fmt chunk size, including id, normally 52 */
	uint32_t version;			/* Version of this format = 1 */
	uint32_t formatid;			/* 0: DSD raw */
	uint32_t channeltype;			/* Channel Type, 1 = mono, 2 = stereo, 3 = 3channels, ... etc */
	uint32_t channelnum;			/* Channel number, 1 = mono, 2 = sterep, ... 6 = 6 channels */
	uint16_t sfreq_low, sfreq_high;		/* Sample frequency: 2822400, 5644800 */
	uint32_t bitssample;			/*  Bits per sample 1 or 8 */
	uint32_t scount_low, scount_high;	/* Sample count per channel in bytes */
	uint16_t blkszchl_low, blkszchl_high;	/* Block size per channel = 4096 */
	uint32_t reserved;			/* Reserved, should be all zero */
};

struct dsdiff_native_dsf_data_chunk {
	struct dsdiff_native_id id;
	uint32_t size_low, size_high;		/* 'data' chunk size, includes header (id+size) */
};

struct dsdiff_native_metadata {
	unsigned sample_rate, channels;
	goffset dsdiff_dsddata_chunk_is_offset;
	uint64_t dsdiff_dsddata_chunk_size;
	goffset dsdiff_comt_chunk_is_offset;
	uint64_t dsdiff_comt_chunk_size;
	goffset dsdiff_diin_chunk_is_offset;
	uint64_t dsdiff_diin_chunk_size;
	goffset dsdiff_emid_chunk_is_offset;
	uint64_t dsdiff_emid_chunk_size;
	goffset dsdiff_mark_chunk_is_offset;
	uint64_t dsdiff_mark_chunk_size;
	goffset dsdiff_diar_chunk_is_offset;
	uint64_t dsdiff_diar_chunk_size;
	goffset dsdiff_diti_chunk_is_offset;
	uint64_t dsdiff_diti_chunk_size;
	goffset dsdiff_manf_chunk_is_offset;
	uint64_t dsdiff_manf_chunk_size;
	goffset dsdiff_id3_chunk_is_offset;		/* DFF off-spec 'ID3 ' chunk also used for DSF in-spec ID3 */
	uint64_t dsdiff_id3_chunk_size;			/* ID3v1 used by sacdextract */
	bool fileisdff;					/* true = dff format, false = dsf */
	uint32_t dsdiff_dsf_bitssample;			/* Store DSF format bits per sample (1 or 8) */
};

struct dsdiff_native_artist {
	uint32_t size;
};

struct dsdiff_native_comments_1 {
	uint16_t cyear;
	uint8_t cmonth;
	uint8_t cday;
	uint8_t chour;
	uint8_t cmin;
	uint16_t ctype;
};

struct dsdiff_native_comments_2 {
	uint16_t cref;

};

struct dsdiff_native_comments_3 {
	uint32_t count;
};

struct dsdiff_native_comchunk {
	uint16_t nrcomments;
};

/* Global variables to hold config options 
 * global is not very nice but it works
 * 
 */

static unsigned int dsdsampleformat;	/* either 32 or 24-bit, default is 32-bit */
static unsigned int tagformat;		/* wanted tag format, 0=none, 1=idv3 only, 2=id3 prefered, 3=native */

static bool
dsdiff_native_init(const struct config_param *param)
{
	const char *tagsupport;

//	dsdsampleformat = config_get_block_unsigned(param, "dsdsampleformat", 32);
	dsdsampleformat = 24;

	if ( (dsdsampleformat != 24) && (dsdsampleformat != 32) )
	{
		g_warning("Unsupported dsd sampleformat %d, defaulting to 32-bit!\n", dsdsampleformat);
		dsdsampleformat = 32;
	}

	/* Get wanted tag behavior from mpd config */
	tagsupport = config_get_block_string(param, "tagsupport", NULL);
	if (tagsupport != NULL)
	{
		tagformat = dsdiff_tag_parse(tagsupport);
	}
	else
	{
		tagformat = DSDIFF_TAG_ID3PREF;			/* Prefer ID3 tags by default */
	}
   
    //// to preset reverse table By Eric
    preapre_reverse_table();

	return true;
}

enum dsdiff_tag_type
dsdiff_tag_parse(const char *tagsupport)
{
	assert(tagsupport != NULL);

	if (strcmp(tagsupport, "id3v1only") == 0 || strcmp(tagsupport, "id3only") == 0)
	{
		return DSDIFF_TAG_ID3ONLY;
	}
	else if (strcmp(tagsupport, "id3v1pref") == 0 || strcmp(tagsupport, "id3pref") == 0)
	{
		return DSDIFF_TAG_ID3PREF;
	}
	else if (strcmp(tagsupport, "native") == 0)
	{
		return DSDIFF_TAG_NATIVE;
	}
	else if (strcmp(tagsupport, "none") == 0)
	{
		return DSDIFF_TAG_NONE;
	}
	return DSDIFF_TAG_NONE;						/* Disable tagging if an unsupported/wrong config option is given */
}

static bool
dsdiff_native_id_equals(const struct dsdiff_native_id *id, const char *s)
{
	assert(id != NULL);
	assert(s != NULL);
	assert(strlen(s) == sizeof(id->value));

	return memcmp(id->value, s, sizeof(id->value)) == 0;
}

/**
 * Read the "size" attribute from the specified header, converting it
 * to the host byte order if needed.
 */
G_GNUC_CONST
static uint64_t
dsdiff_native_chunk_size(const struct dsdiff_native_chunk_header *header)
{
	return (((uint64_t)GUINT32_FROM_BE(header->size_high)) << 32) |
		((uint64_t)GUINT32_FROM_BE(header->size_low));
}

static bool
dsdiff_native_read(struct decoder *decoder, struct input_stream *is,
	    void *data, size_t length)
{
	size_t nbytes = decoder_read(decoder, is, data, length);
	return nbytes == length;
}

static bool
dsdiff_native_read_id(struct decoder *decoder, struct input_stream *is,
	       struct dsdiff_native_id *id)
{
	return dsdiff_native_read(decoder, is, id, sizeof(*id));
}

static bool
dsdiff_native_read_chunk_header(struct decoder *decoder, struct input_stream *is,
			 struct dsdiff_native_chunk_header *header)
{
	return dsdiff_native_read(decoder, is, header, sizeof(*header));
}

static bool
dsdiff_native_read_payload(struct decoder *decoder, struct input_stream *is,
		    const struct dsdiff_native_chunk_header *header,
		    void *data, size_t length)
{
	uint64_t size = dsdiff_native_chunk_size(header);
	if (size != (uint64_t)length)
		return false;

	size_t nbytes = decoder_read(decoder, is, data, length);
	return nbytes == length;
}

/**
 * Skip the #input_stream to the specified offset.
 */
static bool
dsdiff_native_skip_to(struct decoder *decoder, struct input_stream *is,
	       goffset offset)
{
	if (is->seekable)
		return input_stream_seek(is, offset, SEEK_SET, NULL);

	if (is->offset > offset)
		return false;

	char buffer[8192];
	while (is->offset < offset)
	{
		size_t length = sizeof(buffer);
		if (offset - is->offset < (goffset)length)
			length = offset - is->offset;

		size_t nbytes = decoder_read(decoder, is, buffer, length);
		if (nbytes == 0)
			return false;
	}

	assert(is->offset == offset);

	return true;
}

/**
 * Skip some bytes from the #input_stream.
 */
static bool
dsdiff_native_skip(struct decoder *decoder, struct input_stream *is,
	    goffset delta)
{
	assert(delta >= 0);

	if (delta == 0)
		return true;

	if (is->seekable)
		return input_stream_seek(is, delta, SEEK_CUR, NULL);

	char buffer[8192];
	while (delta > 0) {
		size_t length = sizeof(buffer);
		if ((goffset)length > delta)
			length = delta;

		size_t nbytes = decoder_read(decoder, is, buffer, length);
		if (nbytes == 0)
			return false;

		delta -= nbytes;
	}

	return true;
}

/**
 * DFF files: Read and parse a "SND" chunk inside "PROP".
 */
static bool
dsdiff_native_read_prop_snd(struct decoder *decoder, struct input_stream *is,
		     struct dsdiff_native_metadata *metadata,
		     goffset end_offset)
{
	struct dsdiff_native_chunk_header header;
	while ((goffset)(is->offset + sizeof(header)) <= end_offset) {
		if (!dsdiff_native_read_chunk_header(decoder, is, &header))
			return false;

		goffset chunk_end_offset =
			is->offset + dsdiff_native_chunk_size(&header);
		if (chunk_end_offset > end_offset)
			return false;

		if (dsdiff_native_id_equals(&header.id, "FS  ")) {			/* Sample Rate Chunk, 3.2.1 dsdiff spec */
			uint32_t sample_rate;
			if (!dsdiff_native_read_payload(decoder, is, &header,
						 &sample_rate,
						 sizeof(sample_rate)))
				return false;

		metadata->sample_rate = GUINT32_FROM_BE(sample_rate);
		if(metadata->sample_rate == 2822400){
		  g_message("Setting up DSD64X sample rate @176.4kHz");

		  metadata->sample_rate = 176400;						/* Hard code to 176400 for packed DSD-over-PCM */
		}
		else if(metadata->sample_rate == 5644800){
		  g_message("Setting up DSD64X sample rate @352.8kHz");
		  metadata->sample_rate = 352800;						/* Hard code to 352800 for packed DSD-over-PCM */
		}
        else
        {
            g_warning("Only support DSD64X or DSD128X");

            return false;
        }

		} else if (dsdiff_native_id_equals(&header.id, "CHNL")) {		/* Channels Chunk, 3.2.2 dsdiff spec */
			uint16_t channels;
			if (dsdiff_native_chunk_size(&header) < sizeof(channels) ||
			    !dsdiff_native_read(decoder, is,
					 &channels, sizeof(channels)) ||
			    !dsdiff_native_skip_to(decoder, is, chunk_end_offset))
				return false;

			metadata->channels = GUINT16_FROM_BE(channels);

			// '16.05.27 Dubby : Only 2 channel DSD support.
			if(metadata->channels != 2)
			{
				g_message("Not suppoted Number of DSD channels [%d]", metadata->channels);
				return false;
			}
		} else if (dsdiff_native_id_equals(&header.id, "CMPR")) {		/* Compression Type Chunk, 3.2.3 dsdiff spec */
			struct dsdiff_native_id type;
			if (dsdiff_native_chunk_size(&header) < sizeof(type) ||
			    !dsdiff_native_read(decoder, is,
					 &type, sizeof(type)) ||
			    !dsdiff_native_skip_to(decoder, is, chunk_end_offset))
				return false;

			if (!dsdiff_native_id_equals(&type, "DSD "))			/* 'DSD ' -> uncompressed DSD audio data */
				return false;						/* only uncompressed DSD audio data is implemented */
		} else {
			/* ignore unknown chunk */
			if (!dsdiff_native_skip_to(decoder, is, chunk_end_offset))
				return false;
		}
	}

	return is->offset == end_offset;
}

/**
 * Read and parse a "PROP" chunk.
 */
static bool
dsdiff_native_read_prop(struct decoder *decoder, struct input_stream *is,
		 struct dsdiff_native_metadata *metadata,
		 const struct dsdiff_native_chunk_header *prop_header)
{
	uint64_t prop_size = dsdiff_native_chunk_size(prop_header);
	goffset end_offset = is->offset + prop_size;

	struct dsdiff_native_id prop_id;
	if (prop_size < sizeof(prop_id) ||
	    !dsdiff_native_read_id(decoder, is, &prop_id))
		return false;

	if (dsdiff_native_id_equals(&prop_id, "SND "))
		return dsdiff_native_read_prop_snd(decoder, is, metadata, end_offset);
	else
		/* ignore unknown PROP chunk */
		return dsdiff_native_skip_to(decoder, is, end_offset);
}

/*
 * 
 * Read and parse additional chunks in the file to be used for metadata 
 * needed for tag support and store their chunk size and position in the stream
 * for later use
 * All additional chunks after the DSD chunk will be processed 
 *
 * Input: 'is' must point to chunk size portion of the DSD data chunk
 *
 * This routine is made flexible, if a file does not adhere to
 * the standard (ie correct order of chunks) it still will find all
 * the available chunks
 */

static bool dsdiff_native_read_metadata_extra(struct decoder *decoder, struct input_stream *is,
		struct dsdiff_native_metadata *metadata, struct dsdiff_native_chunk_header *chunk_header)
{

	/* skip from DSD data to next chunk header */
	if (!dsdiff_native_skip(decoder, is, metadata->dsdiff_dsddata_chunk_size))
		return false;
	if (!dsdiff_native_read_chunk_header(decoder, is, chunk_header))
		return false;

	/* Now process all the remaining chunk headers in the stream and record their position and size */

	while ( is->offset < is->size )
	{
		uint64_t chunk_size = 0;

		/* COMT chunk */
		if (dsdiff_native_id_equals(&chunk_header->id, "COMT"))
		{
			chunk_size = dsdiff_native_chunk_size(chunk_header);			
			metadata->dsdiff_comt_chunk_is_offset = is->offset;
			metadata->dsdiff_comt_chunk_size = chunk_size;					
		}

		/* DIIN chunk */
		if (dsdiff_native_id_equals(&chunk_header->id, "DIIN"))
		{
			chunk_size = dsdiff_native_chunk_size(chunk_header);
			metadata->dsdiff_diin_chunk_is_offset = is->offset;
			metadata->dsdiff_diin_chunk_size = chunk_size;					
			chunk_size = 0;						/* DIIN chunk is directly followed by other chunks */
		}

		/* EMID chunk */
		if (dsdiff_native_id_equals(&chunk_header->id, "EMID"))
		{
			chunk_size = dsdiff_native_chunk_size(chunk_header);
			metadata->dsdiff_emid_chunk_is_offset = is->offset;
			metadata->dsdiff_emid_chunk_size = chunk_size;
		}

		/* MARK chunk */
		if (dsdiff_native_id_equals(&chunk_header->id, "MARK"))
		{
			chunk_size = dsdiff_native_chunk_size(chunk_header);
			metadata->dsdiff_mark_chunk_is_offset = is->offset;
			metadata->dsdiff_comt_chunk_size = chunk_size;
		}

		/* DIAR chunk */
		if (dsdiff_native_id_equals(&chunk_header->id, "DIAR"))
		{
			chunk_size = dsdiff_native_chunk_size(chunk_header);
			metadata->dsdiff_diar_chunk_is_offset = is->offset;
			metadata->dsdiff_diar_chunk_size = chunk_size;
		}

		/* DITI chunk */
		if (dsdiff_native_id_equals(&chunk_header->id, "DITI"))
		{
			chunk_size = dsdiff_native_chunk_size(chunk_header);
			metadata->dsdiff_diti_chunk_is_offset = is->offset;
			metadata->dsdiff_diti_chunk_size = chunk_size;
		}

		/* MANF chunk */
		if (dsdiff_native_id_equals(&chunk_header->id, "MANF"))
		{
			chunk_size = dsdiff_native_chunk_size(chunk_header);
			metadata->dsdiff_manf_chunk_is_offset = is->offset;
			metadata->dsdiff_manf_chunk_size = chunk_size;
		}				

		/* 'ID3 ' chunk, offspec. Used by sacdextract */
		if (dsdiff_native_id_equals(&chunk_header->id, "ID3 "))
		{
			chunk_size = dsdiff_native_chunk_size(chunk_header);
			metadata->dsdiff_id3_chunk_is_offset = is->offset;
			metadata->dsdiff_id3_chunk_size = chunk_size;
		}

		if (chunk_size != 0)					/* Skip to next chunk */
		{
			if (!dsdiff_native_skip(decoder, is, chunk_size))
				break;
		}

		if ( is->offset < is->size )
		{
			if (!dsdiff_native_read_chunk_header(decoder, is, chunk_header))
				return false;		
		}
		chunk_size = 0;
	}

	/* done processing chunk headers */
	return true;
}

/**
 * Read and parse all metadata chunks at the beginning.  Stop when the
 * first "DSD" chunk is seen, and return its header in the
 * "chunk_header" parameter.
 */
static bool
dsdiff_native_read_metadata(struct decoder *decoder, struct input_stream *is,
		     struct dsdiff_native_metadata *metadata,
		     struct dsdiff_native_chunk_header *chunk_header)
{
	uint64_t chunk_size;
	struct dsdiff_native_header header;
	if (!dsdiff_native_read(decoder, is, &header, sizeof(header)) ||
	    !dsdiff_native_id_equals(&header.id, "FRM8") ||
	    !dsdiff_native_id_equals(&header.format, "DSD "))
	{	
		/* It's not a DFF file, check if it is a DSF file */

		if (!dsdiff_native_skip_to(decoder, is, 0)) 		/* Reset to beginning of the stream */
			return false;

		struct dsdiff_native_header_dsf dsfheader;
		if (!dsdiff_native_read(decoder, is, &dsfheader, sizeof(dsfheader)) ||
		    !dsdiff_native_id_equals(&header.id, "DSD "))
			return false;
		
		chunk_size = (((uint64_t)GUINT32_FROM_LE(dsfheader.size_high)) << 32) |
			((uint64_t)GUINT32_FROM_LE(dsfheader.size_low));

		if (chunk_size != 28)
			return false;

		uint64_t file_size = (((uint64_t)GUINT32_FROM_LE(dsfheader.fsize_high)) << 32) |
			((uint64_t)GUINT32_FROM_LE(dsfheader.fsize_low));

		uint64_t metadata_offset = (((uint64_t)GUINT32_FROM_LE(dsfheader.pmeta_high)) << 32) |
			((uint64_t)GUINT32_FROM_LE(dsfheader.pmeta_low));

		metadata->dsdiff_id3_chunk_is_offset = (goffset) metadata_offset;
		if ( metadata_offset != 0 )
			/* There is a id3 tag in the file, calculate its size */
			metadata->dsdiff_id3_chunk_size = file_size - metadata_offset;			/* Potential off by 1 !! */

		/* Now read the 'fmt ' chunk of the DSF file */
		struct dsdiff_native_dsf_fmt_chunk dsf_fmt_chunk;
		if (!dsdiff_native_read(decoder, is, &dsf_fmt_chunk, sizeof(dsf_fmt_chunk)) ||
		    !dsdiff_native_id_equals(&dsf_fmt_chunk.id, "fmt "))
			return false;

		uint64_t fmt_chunk_size = (((uint64_t)GUINT32_FROM_LE(dsf_fmt_chunk.size_high)) << 32) |
			((uint64_t)GUINT32_FROM_LE(dsf_fmt_chunk.size_low));

		if ( fmt_chunk_size != 52 )
			return false;
		
		uint32_t samplefreq = ((uint32_t)GUINT16_FROM_LE(dsf_fmt_chunk.sfreq_high)) << 16 |
			((uint32_t)GUINT16_FROM_LE(dsf_fmt_chunk.sfreq_low));

		g_message("DSD channel type : type %d, channel %d", dsf_fmt_chunk.channeltype, dsf_fmt_chunk.channelnum);

		if (!(dsf_fmt_chunk.version == 1 && dsf_fmt_chunk.formatid == 0 		/* Only support version 1 of the standard, */
				&& dsf_fmt_chunk.channeltype == 2 				/* DSD raw stereo files with a sample freq */
				&& dsf_fmt_chunk.channelnum == 2
                && (samplefreq == 2822400 || samplefreq == 5644800 )))					/* for x64 and x128(2822400 Hz or 5644800 Hz) */
        {
            if (dsf_fmt_chunk.version != 1)
                g_warning("Unsupported version : %d", dsf_fmt_chunk.version);

            if (dsf_fmt_chunk.formatid != 0)
                g_warning("Unsupported format : %d", dsf_fmt_chunk.formatid);

            if (dsf_fmt_chunk.channelnum != 2
                    || dsf_fmt_chunk.channeltype != 2)
                g_warning("Unsupported channel type : type %d, channel %d", dsf_fmt_chunk.channeltype, dsf_fmt_chunk.channelnum);

            if (samplefreq != 2822400
                    && samplefreq != 5644800)
                g_warning("Not supported DSD type. Only support 64 and 128DSD ");

            return false;
        }

		const uint64_t samplecnt = (((uint64_t)GUINT32_FROM_LE(dsf_fmt_chunk.scount_high)) << 32) |
			((uint64_t)GUINT32_FROM_LE(dsf_fmt_chunk.scount_low));

        uint64_t playable_size = samplecnt * 2 / 8;


		uint32_t chblksize = ((uint32_t)GUINT16_FROM_LE(dsf_fmt_chunk.blkszchl_high)) << 16 |
			((uint32_t)GUINT16_FROM_LE(dsf_fmt_chunk.blkszchl_low));
		if ( chblksize != 4096 )							/* Should be 4096 according to Sony spec */
		{										/* warn so people can report odd sizes */
			g_warning("Channel block size is %u instead of 4096, skipping\n", chblksize);
			return false;
		}

		/* Now read the 'data' chunk of the DSF file */

		struct dsdiff_native_dsf_data_chunk data_chunk;
		if (!dsdiff_native_read(decoder, is, &data_chunk, sizeof(data_chunk)) ||
		    !dsdiff_native_id_equals(&data_chunk.id, "data"))
			return false;

        /* Data size of DSF files are padded to multiple of 4096,
           we just use the actual data size as chunk size */
		uint64_t data_size = (((uint64_t)GUINT32_FROM_LE(data_chunk.size_high)) << 32) |
			((uint64_t)GUINT32_FROM_LE(data_chunk.size_low));

        if (data_size < sizeof(data_chunk)) {
			g_warning("Data size(%llu) is smaller than expected DSF\n", data_size);
            return false;
        }

		data_size -= sizeof(data_chunk);

        /* use the sample count from the DSF header as the upper
           bound, because some DSF files contain junk at the end of
           the "data" chunk */
       
        g_debug(" DSF data_size      : %llu", data_size);
        g_debug("     playable_size  : %llu", playable_size);
        if (data_size > playable_size) {
			g_warning("DSF had junk data at the end of 'data' chunk. data_size: %llu, playable_size %llu\n", data_size, playable_size);

            data_size = playable_size;
        }

		metadata->dsdiff_dsddata_chunk_is_offset = is->offset;			/* Record DSD data offset and size */
		metadata->dsdiff_dsddata_chunk_size = data_size;

        if (samplefreq == 2822400)
            metadata->sample_rate = 176400;                     /* Hard code rate for DSD-over-PCM x64 */
        else
            metadata->sample_rate = 352800;                     /* Hard code rate for DSD-over-PCM x128*/
                                                
        metadata->channels = (unsigned) dsf_fmt_chunk.channelnum;
		metadata->dsdiff_dsf_bitssample = dsf_fmt_chunk.bitssample;		/* Record bits per sample format */
		metadata->fileisdff = false;
		return true;
	}
	else
	{	/* Continue processing of a DFF format file */
		while (true) 
		{
			if (!dsdiff_native_read_chunk_header(decoder, is, chunk_header))
				return false;

			//const struct dsdiff_native_id *id = &chunk_header->id;
			//g_message("[dsdiff_native_read_metadata] DFF id=[%s]\n", id->value);
			
			if (dsdiff_native_id_equals(&chunk_header->id, "PROP"))
			{
				if (!dsdiff_native_read_prop(decoder, is, metadata, chunk_header))
				{
					// '16.04.15 Dubby : Support for Astell& Kern MQA site's dff file. Merry Christmas Mr Lawrence.dff
					
					char data[5];
					size_t nbytes = decoder_read(decoder, is, data, 5);

					g_message("[dsdiff_native_read_metadata] Check JUNK header [%x %x %x %x] at [%lld]",data[1], data[2], data[3], data[4], is->offset);
					if((data[1]==0x4a) && (data[2]==0x55) && (data[3]==0x4e) && (data[4]==0x4b)) // Check if JUNK
					{
						dsdiff_native_skip_to(decoder, is, is->offset - nbytes + 1); //Jump to next bytes
						g_message("[dsdiff_native_read_metadata] JUNK jump to [%lld]", is->offset);
						continue;
					}
					
					return false;

				}
	
			} 
			else if (dsdiff_native_id_equals(&chunk_header->id, "DSD ")) 
			{
				/* Store DSD data chunk position in stream, 'is->offset' points to start of DSD data */
				metadata->dsdiff_dsddata_chunk_is_offset = is->offset;
				chunk_size = dsdiff_native_chunk_size(chunk_header);
				metadata->dsdiff_dsddata_chunk_size = chunk_size;
				metadata->fileisdff = true;
				return true;
			}
			else 
			{
				/* ignore unknown chunk */
				chunk_size = dsdiff_native_chunk_size(chunk_header);
				goffset chunk_end_offset = is->offset + chunk_size;

				if (!dsdiff_native_skip_to(decoder, is, chunk_end_offset))
					return false;

			}
		}
	}

}

/**
 * Decode the a complete "DSD " data chunk.
 */
static bool
dsdiff_native_decode_chunk(struct decoder *decoder, struct input_stream *is,
		unsigned channels, uint64_t chunk_size, uint64_t stream_start_offset, 
		bool fileisdff, uint32_t dsdiff_dsf_bitssample)
{
	uint8_t buffer[16384];								/* 2x size of output n_buffer */
											/* ratio input:output buffer is 1:2 because of packed DSD output */

	const uint64_t stream_end_offset = chunk_size + stream_start_offset;		/* Needed for forward seeking */

	const size_t sample_size = sizeof(buffer[0]);
	const size_t frame_size = channels * sample_size;
	const unsigned buffer_frames = sizeof(buffer) / frame_size / 2;			/* JK: Only hold half of frames in input buffer */

	const unsigned buffer_samples = buffer_frames * frame_size;
	const size_t buffer_size = buffer_samples * sample_size;

	uint8_t n_buffer[G_N_ELEMENTS(buffer)];						/* JK: buffer to hold dsd data converted to dsd-over-usb spec */

	while (chunk_size > 0) 
	{
		/* see how much aligned data from the remaining chunk
		   fits into the local buffer */
		unsigned now_frames = buffer_frames;
		size_t now_size = buffer_size;
		unsigned now_samples = buffer_samples;
		if (chunk_size < (uint64_t)now_size)
		{
			now_frames = (unsigned)chunk_size / frame_size;
			now_size = now_frames * frame_size;
			now_samples = now_frames * channels;
		}

		/** decoder_read parameters:
 		 * Blocking read from the input stream.
		 *
 		 * @param decoder the decoder object
		 * @param is the input stream to read from
 		 * @param buffer the destination buffer
		 * @param length the maximum number of bytes to read
		 * @return the number of bytes read, or 0 if one of the following
		 * occurs: end of file; error; command (like SEEK or STOP).
		 */

		size_t nbytes = decoder_read(decoder, is, buffer, now_size);
#if 0	// '16.01.26 Dubby : DSD scrub fail bug fix.	
		if (nbytes != now_size)
			return false;
#else
		if (nbytes != now_size)
			nbytes = now_size;
#endif

		chunk_size -= nbytes;

	
		/* end debug */

		/* dsdnative_translate_dff paramters:
		 * "translates" a stream of octets to 'DSD-over-USB' spec PCM data 
		 * @param samples -- number of octets/samples to "translate"
		 * @param src -- pointer to first octet (input)
		 * @param dst -- pointer to first uint8_t (output)
		 * @param dsdsampleformat -- uint with dest. sampleformat: 24 or 32 bit
		 */

		if (fileisdff)
		{
			dsdnative_translate_dff(now_samples, buffer, n_buffer, dsdsampleformat);
		}
		else
		{
			dsdnative_translate_dsf(now_samples, buffer, n_buffer, dsdsampleformat, dsdiff_dsf_bitssample);
		}

		now_samples = now_samples * 2;				/* double up, output size is twice the input */

		/* Submit to the decoder API ie send to MPD */

		/* decoder_data paramters:
		 *
		 * @param decoder the decoder object
		 * @param is an input stream which is buffering while we are waiting
 		 * for the player
		 * @param data the source buffer
		 * @param length the number of bytes in the buffer
		 * @return the current command, or DECODE_COMMAND_NONE if there is no
		 * command pending
		 */

		GError *error = NULL;
		goffset offset;
		goffset curpos;

		enum decoder_command cmd =
			decoder_data(decoder, is, n_buffer,
				     now_samples * sizeof(n_buffer[0]),
				     5652);						/* 5652kbit/s is the rate for DSD64 files */

		switch (cmd) 
		{
		case DECODE_COMMAND_NONE:
			break;

		case DECODE_COMMAND_START:
		case DECODE_COMMAND_STOP:
			return false;

		case DECODE_COMMAND_SEEK:

			curpos = is->offset;
			offset = (goffset) (stream_start_offset + (705600 * decoder_seek_where(decoder)));
	
			if ( offset <  (goffset) stream_start_offset ) 
				offset = (goffset) stream_start_offset;
			if ( offset > (goffset) stream_end_offset )
				offset = (goffset) stream_end_offset;

			if (!fileisdff)			/* Seek code for DSF file format, needs other code due to interleave format */
			{
				if ( offset > (goffset) stream_start_offset )
				{
					offset -= stream_start_offset;
					float i = (float) offset / (float) 4096;
					unsigned t = (unsigned ) i;
					if ( t %2 == 1 )
					{
						t -= 1;				/* Adjust offset to nearest for left channel data */
					}
					offset = (t * 4096) + stream_start_offset;
				}	

			}	

			/* modify chunk_size so it shrinks and grows according to FF or REW */
	
			if (offset < curpos)						/* Rewind */
			{
				chunk_size = chunk_size + (curpos - offset);
			}
			else								/* Fast forward */
			{
				chunk_size = chunk_size - (offset - curpos);
			}
	
			if (input_stream_lock_seek(is, offset, SEEK_SET, &error))	/* Seek to new offset */
			{
				decoder_command_finished(decoder);
			} 
			else
			{
				g_warning("seeking failed: %s", error->message);
				g_error_free(error);
				decoder_seek_error(decoder);
			}
		}
	}
	//	return dsdiff_native_skip(decoder, is, chunk_size);
	return true;
}

static void
dsdiff_native_stream_decode(struct decoder *decoder, struct input_stream *is)
{
	struct dsdiff_native_metadata metadata = {
		.sample_rate = 0,
		.channels = 0,
		.dsdiff_dsddata_chunk_is_offset = 0,
		.dsdiff_dsddata_chunk_size = 0,
		.dsdiff_comt_chunk_is_offset = 0,
		.dsdiff_diin_chunk_is_offset = 0,
		.dsdiff_emid_chunk_is_offset = 0,
		.dsdiff_mark_chunk_is_offset = 0,
		.dsdiff_diar_chunk_is_offset = 0,
		.dsdiff_diti_chunk_is_offset = 0,
		.dsdiff_manf_chunk_is_offset = 0,
		.dsdiff_id3_chunk_is_offset = 0
	};

	struct dsdiff_native_chunk_header chunk_header;
	if (!dsdiff_native_read_metadata(decoder, is, &metadata, &chunk_header))
		return;

	GError *error = NULL;
	struct audio_format audio_format;

	if ( dsdsampleformat == 32)
	{
		/* use 32-bit sample format */
		if (!audio_format_init_checked(&audio_format, metadata.sample_rate,
				       SAMPLE_FORMAT_S32,
				       metadata.channels, &error)) {
			g_warning("%s", error->message);
			g_error_free(error);
			return;
		}
	}
	else
	{
		/* use 24-bit sample format */
		if (!audio_format_init_checked(&audio_format, metadata.sample_rate,
				       SAMPLE_FORMAT_S24_P32,
				       metadata.channels, &error)) {
			g_warning("%s", error->message);
			g_error_free(error);
			return;
		}

	}

    if (metadata.sample_rate == 352800)
        decoder->dc->isDSD = DSD_TYPE_128;
    else
        decoder->dc->isDSD = DSD_TYPE_64;


//    g_message("################## isDSD %d\n%s", decoder->dc->isDSD, is->uri);

	/* success: file was recognized and is supported */

	uint64_t chunk_size = metadata.dsdiff_dsddata_chunk_size;

	/* Calculate song time from DSD chunk size */
	/// to support 128x properly by Eric
	///float songtimeOrg = ( ( chunk_size / metadata.channels ) * 8) / (float) 2822400;
	float songtime =  ( ( chunk_size / metadata.channels ) * 8) / (float) (metadata.sample_rate * 16);
	/// end of Eric

	decoder_initialized(decoder, &audio_format, true, songtime);

	/* workaround for problem with chunk sizes which are not dividable by 4 */
	/* without this mpd will hang after playing such a file. Only seen once with a file made wit Korg Audiogate */

	if (! (chunk_size %4 == 0))
	{
		g_warning("Workaround applied for DSD data chunk size not dividable by 4\n");
		chunk_size -= 2;
	} 
	if (!dsdiff_native_decode_chunk(decoder, is,
		 metadata.channels, chunk_size, metadata.dsdiff_dsddata_chunk_is_offset, metadata.fileisdff, metadata.dsdiff_dsf_bitssample))
	{
		g_warning("Something happened during dsdiff_native_decode_chunk\n");
	}
	else
	{
		g_warning("Nothing happened during dsdiff_native_decode_chunk\n");
	}
	
}

static struct tag *
dsdiff_native_stream_tag(struct input_stream *is)
{
	struct dsdiff_native_metadata metadata = {
		.sample_rate = 0,
		.channels = 0,
		.dsdiff_dsddata_chunk_is_offset = 0,
		.dsdiff_dsddata_chunk_size = 0,
		.dsdiff_comt_chunk_is_offset = 0,
		.dsdiff_diin_chunk_is_offset = 0,
		.dsdiff_emid_chunk_is_offset = 0,
		.dsdiff_mark_chunk_is_offset = 0,
		.dsdiff_diar_chunk_is_offset = 0,
		.dsdiff_diti_chunk_is_offset = 0,
		.dsdiff_manf_chunk_is_offset = 0,
		.dsdiff_id3_chunk_is_offset = 0
	};
	struct dsdiff_native_chunk_header chunk_header;
	if (!dsdiff_native_read_metadata(NULL, is, &metadata, &chunk_header))
		return NULL;

	/* See if there is additional metadata for DFF files */
//	if (metadata.fileisdff)
//	{
//		if (!dsdiff_native_read_metadata_extra(NULL, is, &metadata, &chunk_header))
//		{
//			g_warning("Problem getting additional metadata, skipping further metadata processing\n");
// 			tagformat = DSDIFF_TAG_NONE;							/* Disable further tag processing */
//		}
//	}
	tagformat = DSDIFF_TAG_NONE;							/* Disable further tag processing */	
	struct audio_format audio_format;
	if (!audio_format_init_checked(&audio_format, metadata.sample_rate,
				       SAMPLE_FORMAT_S32,
				       metadata.channels, NULL))		
		/* refuse to parse files which we cannot play anyway */
		return NULL;

	uint64_t chunk_size = metadata.dsdiff_dsddata_chunk_size;

	/// to support 128x properly by Eric
    uint32_t dsdDatarate = metadata.sample_rate * 16;
	///float songtime = ( ( chunk_size / metadata.channels ) * 8) / (float) 2822400;
    float songtime =  ( ( chunk_size / metadata.channels ) * 8) / (float) (dsdDatarate);
	/// end of Eric

	struct tag *tag;
	tag = tag_new();
	tag->time = (int) songtime;					/* Set song time, regardless if tag support is disabled */
	

	if ( tagformat != DSDIFF_TAG_NONE )
	{
		/* Use native DSDIFF tags if tagformat = DSDIFF_TAG_NATIVE or */
		/* tagformat = DSDIFF_ID3TAG and there is no id3 tag available */
		/* and the native tags are actually found */

		if ( (tagformat == DSDIFF_TAG_NATIVE && metadata.dsdiff_diar_chunk_size != 0 ) ||
				( tagformat == DSDIFF_TAG_ID3PREF && metadata.dsdiff_id3_chunk_size == 0 && metadata.dsdiff_diar_chunk_size != 0 ) )

		{
			/* Set 'is' to start of DIAR chunk data */
			if (dsdiff_native_skip_to(NULL, is, metadata.dsdiff_diar_chunk_is_offset))
			{
				/* get string length for Artist name */
				struct dsdiff_native_artist artist;
				if (dsdiff_native_read(NULL, is, &artist, sizeof(artist)))
				{
					uint64_t alength = (uint64_t)GUINT32_FROM_BE(artist.size);
					char astr[alength];
					char *label;
					label = astr;
					
					if (dsdiff_native_read(NULL, is, label, alength))
					{
						astr[alength] = '\0';
						/* Add Artist tag */
						tag_add_item_n(tag, TAG_ARTIST, label, alength);
					}
				}
			}
		}

		if ( (tagformat == DSDIFF_TAG_NATIVE && metadata.dsdiff_diti_chunk_size != 0 ) ||
				( tagformat == DSDIFF_TAG_ID3PREF && metadata.dsdiff_id3_chunk_size == 0 && metadata.dsdiff_diti_chunk_size != 0 ) )
	
		{
			/* Set 'is' to start of DITI chunk data */
			if (dsdiff_native_skip_to(NULL, is, metadata.dsdiff_diti_chunk_is_offset))
			{
				/* get string length of Title name */
				struct dsdiff_native_artist title;
				if (dsdiff_native_read(NULL, is, &title, sizeof(title)))
				{		
					uint64_t tlength = (uint64_t)GUINT32_FROM_BE(title.size);
					char tstr[tlength];
					char *label;
					label = tstr;

					if (dsdiff_native_read(NULL, is, label, tlength))
					{
						tstr[tlength]= '\0';
						/* Add Title tag */
						tag_add_item_n(tag, TAG_TITLE, label, tlength);
					}
				}
			}
		}
					
		if ( (tagformat == DSDIFF_TAG_ID3ONLY && metadata.dsdiff_id3_chunk_size != 0) || 
				(tagformat == DSDIFF_TAG_ID3PREF && metadata.dsdiff_id3_chunk_size != 0) ||
				(tagformat == DSDIFF_TAG_NATIVE && metadata.dsdiff_id3_chunk_size !=0 && !metadata.fileisdff) )
		{

			/* Set 'is' to start of 'ID3 ' chunk data */
			if (dsdiff_native_skip_to(NULL, is, metadata.dsdiff_id3_chunk_is_offset))
			{
				struct id3_tag *id3_tag = NULL;
				id3_length_t count;
				count = is->size - is->offset;
				id3_byte_t myid3[count];
				id3_byte_t *myid3data;
				myid3data = myid3;
	
				if (dsdiff_native_read(NULL, is, myid3data , count))
				{	
					id3_tag = id3_tag_parse(myid3data, count);
					if (id3_tag == NULL)
					{
						g_warning("id3_tag = NULL\n");
					}
					else
					{
						tag = tag_id3_import(id3_tag);
						tag->time = (int) songtime;			/* Reset songtime to time calculated from DSD */
					}
				}
			}				
		}
	}
	return tag;
}

static const char *const dsdiff_native_suffixes[] = {
	"dff",
	"dsf",
	NULL
};

static const char *const dsdiff_native_mime_types[] = {
	"application/x-dff",
	"application/x-dsf",
	NULL
};

const struct decoder_plugin dsdiff_nativeplayback_plugin = {
	.name = "dsdiff_native",
	.init = dsdiff_native_init,
	.stream_decode = dsdiff_native_stream_decode,
	.stream_tag = dsdiff_native_stream_tag,
	.suffixes = dsdiff_native_suffixes,
	.mime_types = dsdiff_native_mime_types,
};
