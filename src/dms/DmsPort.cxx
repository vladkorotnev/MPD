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
#include "DmsPort.hxx"

#define DMS_PORT_PATH		"/data/mpd/port"

int PortFile::read()
{
	int port = 6600;
	AllocatedPath path = AllocatedPath::FromUTF8(DMS_PORT_PATH);
	
	if (!path.IsNull()) {
		FILE *fp = FOpen(path, PATH_LITERAL("r"));
		if (fp != nullptr) {
			fscanf(fp, "%i", &port);
			fclose(fp);
		}
	}

	return port;
}

void PortFile::write(int port)
{
	AllocatedPath path = AllocatedPath::FromUTF8(DMS_PORT_PATH);

	if (!path.IsNull()) {
		FILE *fp = FOpen(path, PATH_LITERAL("w"));
		if (fp != nullptr) {
			fprintf(fp, "%i\n", port);
			fclose(fp);
		}
	}
}

