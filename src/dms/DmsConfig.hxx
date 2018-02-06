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
 
#ifndef DMS_CONFIG_HXX
#define DMS_CONFIG_HXX

#include "thread/Mutex.hxx"
#include "thread/Cond.hxx"
#include "thread/Thread.hxx"
#include "client/Client.hxx"
#include <string.h>
#include "DmsArgs.hxx"

class DmsConfig
{
public:
	DmsConfig();

	bool operator==(const DmsConfig &t) const;

	inline bool operator!=(const DmsConfig &t) const {return !(*this == t);}

	DmsConfig &operator=(const DmsConfig &t);
	
	DmsVolume	&getVolume();

	DmsVolume	&getVolume(const DmsSource &s);

	void setVolume(unsigned char vol);

	DmsSRC		&getSRC();

	DmsSRC		&getSRC(const DmsSource &s);

	bool setSRC(enum DMS_RATE s);

public:
	DmsSource		source;

	std::string		networkLastUri;

	std::string		internetLastUri;

	DmsVolumePolicy	volume_policy;

	DmsVolume		volume_master;

	DmsVolume		volume_independent[CHANNEL_MAX];

	DmsSRC			SRC_independent[CHANNEL_MAX];

	DmsBool			tube;

	//DmsBool			mute;

	DmsBool			usertips;

	DmsBool			appUsertips;

	DmsBool			pcshare;

	DmsBool			mediaserver;

	DmsSource		startup;

	DmsIr			ir;

	DmsNetWorkType	networkType;

	DmsIpAssign		ipAssign;

	DmsPort			port;

	DmsBrightness	brightness;

	DmsIp			staticIp;

	//Playmode		playmode;

	std::string		aliasName;

	CoverOption		coverOption;

	FolderCoverOption	folderCoverOption;

	DmsRate			rate; // not save to file

	DmsBluetooth	bluetooth; // not save to file

	DmsPoweron 		poweron;

	int			smbdebug; // not save to file

	DmsSRC			src; // not save to file

	AudioFormat   in_format; // not save to file
};

inline bool checkPlaylistPrefix(const char *name, const char *prefix)
{
	return memcmp(name, prefix, strlen(prefix)) == 0;
}

inline const char *stripPrefix(const char *name, const char *prefix)
{
	return (name + strlen(prefix)+1);
}

#endif
