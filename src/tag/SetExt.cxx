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
 
#include "SetExt.hxx"
#include "TagBuilder.hxx"
#include "TagSettings.h"

#include <assert.h>
#include <stdio.h>

static std::string
get_parent(std::string str)
{
	auto p1 = str.rfind('/');
	if (p1 == std::string::npos) {
		return std::string("Folder");
	}

	auto p2 = str.rfind('/', p1-1);
	if (p2 == std::string::npos) {
		return std::string("Folder");
	}
	return str.substr(p2+1, p1-p2-1);
}

void
TagExtSet::InsertUnique(const Tag &tag,       TagType type, const std::string &uri)
{
	bool found = false;

	for (const auto &item : tag) {
		if (item.type == type) {
			TagExt t;
			t.value = item.value;
			t.song = uri;
			insert(t);
			found = true;
		}
	}

	if (found) {
		return;
	}

	/* fall back to "Artist" if no "AlbumArtist" was found */
	if (type == TAG_ALBUM_ARTIST) {
		for (const auto &item : tag) {
			if (item.type == TAG_ARTIST) {
				TagExt t;
				t.value = item.value;
				t.song = uri;
				insert(t);
				found = true;
			}
		}
	}

	if (found) {
		return;
	}

	// fall back to folder name
	if (type == TAG_ALBUM || type == TAG_ALBUM_SORT) {
		auto s = get_parent(uri);
		// fall back to folder name
		TagExt t;
		t.value = s;
		t.song = uri;
		insert(t);
		found = true;
	}

	if (found) {
		return;
	}

	TagExt t;
	t.value = ""; // "unknown"
	t.song = uri;
	insert(t);
}
