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
#include "DmsConfig.hxx"
#include "client/Client.hxx"
#include "protocol/ArgParser.hxx"
#include "protocol/Result.hxx"
#include "Partition.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Domain.hxx"
#include "fs/AllocatedPath.hxx"
#include "fs/Traits.hxx"
#include "fs/FileSystem.hxx"
#include "fs/NarrowPath.hxx"
#include "fs/io/FileOutputStream.hxx"
#include "fs/io/BufferedOutputStream.hxx"
#include "fs/io/FileReader.hxx"
#include "Log.hxx"
#include "Product.hxx"

static constexpr Domain dms_config_domain("dms_config");

DmsConfig::DmsConfig()
{
	volume_master.value = volume_db_to_value(-30);
	usertips.value = DMS_ON;
	appUsertips.value = DMS_ON;
	smbdebug = 0;
	pcshare.value = DMS_ON;
	mediaserver.value = DMS_ON;
	switch (mDmsProduct.getType()) {
	case DmsProduct::DMS500:
		aliasName = "CARY DMS-500";
		break;
	case DmsProduct::AIOS:
		aliasName = "CARY AiOS";
		break;
	default:
		gcc_unreachable();
	}
	startup.parse("usb_source");
}

bool DmsConfig::operator==(const DmsConfig &t) const
{
	for (int i=0;i<CHANNEL_MAX;i++) {
		if (volume_independent[i].value != t.volume_independent[i].value
			&& SRC_independent[i].value != t.SRC_independent[i].value) {
			return false;
		}
	}
	
	return (source == t.source
		&& volume_policy == t.volume_policy
		&& volume_master == t.volume_master
		&& usertips == t.usertips
		&& appUsertips == t.appUsertips
		&& startup == t.startup
		&& ir == t.ir
		&& networkType == t.networkType
		&& ipAssign == t.ipAssign
		&& port == t.port
		&& staticIp == t.staticIp
		&& brightness == t.brightness
		&& aliasName == t.aliasName
		&& pcshare == t.pcshare
		&& mediaserver == t.mediaserver
		&& coverOption == t.coverOption
		&& folderCoverOption == t.folderCoverOption
		);
}

DmsConfig &DmsConfig::operator=(const DmsConfig &t)
{
	for (int i=0;i<CHANNEL_MAX;i++) {
		volume_independent[i] = t.volume_independent[i];
		SRC_independent[i] = t.SRC_independent[i];
	}

	staticIp = t.staticIp;

	source = t.source;
	volume_policy = t.volume_policy;
	volume_master.value = t.volume_master.value;
	usertips = t.usertips;
	appUsertips = t.usertips;
	startup = t.startup;
	ir = t.ir;
	networkType = t.networkType;
	ipAssign = t.ipAssign;
	port = t.port;
	brightness = t.brightness;
	aliasName = t.aliasName;
	pcshare = t.pcshare;
	mediaserver = t.mediaserver;
	coverOption = t.coverOption;
	folderCoverOption = t.folderCoverOption;
	
	return *this;
}

DmsVolume	&DmsConfig::getVolume(const DmsSource &s)
{
	switch (volume_policy.value) {
	case VOLUME_POLICY_MASTER:
		return volume_master;
	case VOLUME_POLICY_INDEPENDENT:
		return volume_independent[s.toChannel()];
	default:
		assert(false);
		gcc_unreachable();
	}
}

DmsVolume	&DmsConfig::getVolume()
{
	return getVolume(source);
}

void DmsConfig::setVolume(unsigned char vol)
{
	if (vol >= DMS_VOLUME_MAX)
		vol = DMS_VOLUME_MAX;
	else if (DMS_VOLUME_MAX <= DMS_VOLUME_MIN)
		vol = DMS_VOLUME_MIN;

	switch (volume_policy.value) {
	case VOLUME_POLICY_MASTER:
		volume_master.value = vol;
		break;
	case VOLUME_POLICY_INDEPENDENT:
		volume_independent[source.toChannel()].value = vol;
	default:
		assert(false);
		gcc_unreachable();
	}
}

DmsSRC &DmsConfig::getSRC()
{
	return SRC_independent[source.toChannel()];
}

DmsSRC &DmsConfig::getSRC(const DmsSource &s)
{
	return SRC_independent[s.toChannel()];
}

bool DmsConfig::setSRC(enum DMS_RATE s)
{
	if (s == RATE_BYPASS ||
		(s >= RATE_MIN && s <= RATE_MAX)) {
		SRC_independent[source.toChannel()].value = s;
		return true;
	} else {
		return false;
	}
}
