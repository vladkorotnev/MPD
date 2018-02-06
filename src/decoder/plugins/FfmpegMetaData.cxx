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

/* necessary because libavutil/common.h uses UINT64_C */
#define __STDC_CONSTANT_MACROS

#include "config.h"
#include "FfmpegMetaData.hxx"
#include "tag/TagTable.hxx"
#include "tag/TagHandler.hxx"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
}

static constexpr struct tag_table ffmpeg_tags[] = {
	{ "year", TAG_DATE },
	{ "author-sort", TAG_ARTIST_SORT },
	{ "album_artist", TAG_ALBUM_ARTIST },
	{ "album_artist-sort", TAG_ALBUM_ARTIST_SORT },

	/* sentinel */
	{ nullptr, TAG_NUM_OF_ITEM_TYPES }
};

static void
FfmpegScanTag(TagType type,
	      AVDictionary *m, const char *name,
	      const struct tag_handler *handler, void *handler_ctx)
{
	AVDictionaryEntry *mt = nullptr;

	while ((mt = av_dict_get(m, name, mt, 0)) != nullptr)
		tag_handler_invoke_tag(handler, handler_ctx,
				       type, mt->value);
}

static void
FfmpegScanPairs(AVDictionary *dict,
		const struct tag_handler *handler, void *handler_ctx)
{
	AVDictionaryEntry *i = nullptr;

	while ((i = av_dict_get(dict, "", i, AV_DICT_IGNORE_SUFFIX)) != nullptr)
		tag_handler_invoke_pair(handler, handler_ctx,
					i->key, i->value);
}

void
FfmpegScanDictionary(AVDictionary *dict,
		     const struct tag_handler *handler, void *handler_ctx)
{
	if (handler->tag != nullptr) {
		for (unsigned i = 0; i < TAG_NUM_OF_ITEM_TYPES; ++i)
			FfmpegScanTag(TagType(i), dict, tag_item_names[i],
				      handler, handler_ctx);

		for (const struct tag_table *i = ffmpeg_tags;
		     i->name != nullptr; ++i)
			FfmpegScanTag(i->type, dict, i->name,
				      handler, handler_ctx);
	}

	if (handler->pair != nullptr)
		FfmpegScanPairs(dict, handler, handler_ctx);
}

static const int MAX_MIMENAME_NUM  = 11;
static const char *mime_tympename[MAX_MIMENAME_NUM] = {
	"image/jpeg",
	"image/png",
	"image/x-ms-bmp",
	 "image/jp2",
	 "image/x-portable-pixmap",
	 "image/gif",
	 "image/x-pcx",
	 "image/x-targa image/x-tga",
	 "image/tiff",
	 "image/webp",
	 "image/x-xwindowdump",
};


enum MimeNameIndex
{
	MJPEG,
	PNG,
	BMP,
	JPEG2000,
	PAM,
	GIF,
	PCX,
	TARGA,
	TIFF,
	WEBP,
	XWD,

};

static const char  *
get_MIMEDescriptor(enum AVCodecID id)
{
	switch(id)
	{
		case AV_CODEC_ID_MJPEG:
			return mime_tympename[MJPEG];

		case AV_CODEC_ID_PNG:
			return mime_tympename[PNG];

		case AV_CODEC_ID_BMP:
			return mime_tympename[BMP];

		case AV_CODEC_ID_JPEG2000:
			return mime_tympename[JPEG2000];

		case AV_CODEC_ID_PAM:
			return mime_tympename[PAM];

		case AV_CODEC_ID_GIF:
			return mime_tympename[GIF];

		case AV_CODEC_ID_PCX:
			return mime_tympename[PCX];

		case AV_CODEC_ID_TARGA:
			return mime_tympename[TARGA];
			
			
		case AV_CODEC_ID_TIFF:
			
			return mime_tympename[TIFF];
			

		case AV_CODEC_ID_WEBP:
			
			return mime_tympename[WEBP];
			

		case AV_CODEC_ID_XWD:
			
			return mime_tympename[XWD];
			

		
		default:
   
   			return nullptr;
	
   	}

  
 
}




static bool
ffmpeg_copy_cover_paramer(CoverType type, unsigned value,	 
						 const struct tag_handler *handler, void *handler_ctx)
{
	
	char buf[21];

	if (snprintf(buf, sizeof(buf)-1, "%u", value)) {
		tag_handler_invoke_cover(handler, handler_ctx, type, (const char*)buf);
		return true;
	}
	return false;
}


void
FfmpegScanCover(AVFormatContext &format_context,		 
    const struct tag_handler *handler, void *handler_ctx)
{


	if(handler->cover != nullptr) 
	{
		for (unsigned i = 0; i < format_context.nb_streams; ++i)
		{
			if(format_context.streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)
			{
				ffmpeg_copy_cover_paramer(COVER_TYPE, 0,handler, handler_ctx);
				
				const char  * mime_types = get_MIMEDescriptor(format_context.streams[i]->codec->codec_id);		
				if (mime_types != nullptr) {
			
					tag_handler_invoke_cover(handler, handler_ctx, COVER_MIME,mime_types);
			
				}
				
				tag_handler_invoke_cover(handler, handler_ctx, COVER_DESCRIPTION,"");
				
				ffmpeg_copy_cover_paramer(COVER_WIDTH, format_context.streams[i]->codec->width, handler, handler_ctx);		
				ffmpeg_copy_cover_paramer(COVER_HEIGHT, format_context.streams[i]->codec->height, handler, handler_ctx);
				ffmpeg_copy_cover_paramer(COVER_DEPTH, 0, handler, handler_ctx);
				ffmpeg_copy_cover_paramer(COVER_COLORS, 0, handler, handler_ctx);


				AVPacket pkt = format_context.streams[i]->attached_pic;

				if (pkt.data != nullptr) {
					ffmpeg_copy_cover_paramer(COVER_LENGTH, pkt.size, handler, handler_ctx);
					
					tag_handler_invoke_cover(handler, handler_ctx, COVER_DATA,
					(const char*)pkt.data, pkt.size);
				
				}
		
			}
		

	
		}

	}


}
