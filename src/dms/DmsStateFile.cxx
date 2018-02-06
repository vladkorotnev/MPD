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
#include "DmsStateFile.hxx"
#include "fs/io/TextFile.hxx"
#include "fs/io/FileOutputStream.hxx"
#include "fs/io/BufferedOutputStream.hxx"
#include "Partition.hxx"
#include "Instance.hxx"
#include "fs/FileSystem.hxx"
#include "util/Domain.hxx"
#include "util/StringUtil.hxx"
#include "Log.hxx"
#include "queue/PlaylistState.hxx"
#include "PlaylistSave.hxx"
#include "PlaylistFile.hxx"
#include "Product.hxx"
#include "dms/Context.hxx"

#include <string.h>

static constexpr Domain domain("dms_state_file");

#define DMS_STATE_FILE_SOURCE			"source: "
#define DMS_STATE_FILE_NETWORK_LAST		"network_last: "
#define DMS_STATE_FILE_INTERNET_LAST	"internet_last: "
#define DMS_STATE_FILE_VOLUME_POLICY	"volume_policy: "
#define DMS_STATE_FILE_VOLUME_MASTER	"volume_master: "
#define DMS_STATE_FILE_VOLUME_INDEPENDENT	"volume_independent: "
#define DMS_STATE_FILE_SRC_INDEPENDENT	"SRC_independent: "
#define DMS_STATE_FILE_STARTUP			"startup: "
#define DMS_STATE_FILE_IR				"ir: "
#define DMS_STATE_FILE_NETWORK_TYPE		"network_type: "
#define DMS_STATE_FILE_IP_ASSIGN		"ip_assign: "
#define DMS_STATE_FILE_PORT				"port: "
#define DMS_STATE_FILE_IP				"static_ip: "
#define DMS_STATE_FILE_USER_TIPS		"user_tips: "
#define DMS_STATE_FILE_APP_USER_TIPS	"app_user_tips: "
#define DMS_STATE_FILE_MUTE				"mute: "
#define DMS_STATE_FILE_PLAYMODE			"playmode: "
#define DMS_STATE_FILE_ALIAS_NAME		"alias_name: "
#define DMS_STATE_FILE_BRIGHTNESS		"brightness: "
#define DMS_STATE_FILE_PCSHARE			"pcshare: "
#define DMS_STATE_FILE_MEDIASERVER		"mediaserver: "
#define DMS_STATE_FILE_COVER_SOURCE		"cover_source: "
#define DMS_STATE_FILE_FOLDER_COVER		"folder_cover: "

DmsStateFile::DmsStateFile(AllocatedPath &&_path, unsigned _interval,
		     Partition &_partition, EventLoop &_loop)
	:TimeoutMonitor(_loop),
	 path(std::move(_path)), path_utf8(path.ToUTF8()),
	 interval(_interval),
	 partition(_partition),
	 df(_partition.df),
	 dc(_partition.dc),
	 preDmsconfig(),
	 prev_playlist_version(0)
{
}

void
DmsStateFile::RememberVersions()
{
	preDmsconfig = df;
	prev_playlist_version = playlist_state_get_hash(partition.playlist,
							partition.pc);
}

bool
DmsStateFile::IsModified() const
{
	return (preDmsconfig != df ||
		prev_playlist_version != playlist_state_get_hash(partition.playlist,
								 partition.pc));
}

inline void
DmsStateFile::Write(BufferedOutputStream &os)
{
	os.Format(DMS_STATE_FILE_SOURCE "%s\n", df.source.uri.c_str());
	os.Format(DMS_STATE_FILE_NETWORK_LAST "%s\n", df.networkLastUri.c_str());
	os.Format(DMS_STATE_FILE_INTERNET_LAST "%s\n", df.internetLastUri.c_str());
	os.Format(DMS_STATE_FILE_VOLUME_POLICY "%s\n", df.volume_policy.toString().c_str());
	os.Format(DMS_STATE_FILE_VOLUME_MASTER "%d\n", df.volume_master.value);
	os.Format(DMS_STATE_FILE_VOLUME_INDEPENDENT);
	for (int i=0;i<CHANNEL_MAX;i++) {
		os.Format(" %d", df.volume_independent[i].value);
	}
	os.Format("\n");
	os.Format(DMS_STATE_FILE_SRC_INDEPENDENT);
	for (int i=0;i<CHANNEL_MAX;i++) {
		os.Format(" %s", df.SRC_independent[i].toString().c_str());
	}
	os.Format("\n");
	os.Format(DMS_STATE_FILE_STARTUP "%s\n", df.startup.uri.c_str());
	os.Format(DMS_STATE_FILE_IR		 "%s\n", df.ir.toString().c_str());
	os.Format(DMS_STATE_FILE_NETWORK_TYPE "%s\n", df.networkType.toString().c_str());
	os.Format(DMS_STATE_FILE_IP_ASSIGN "%s\n", df.ipAssign.toString().c_str());
	os.Format(DMS_STATE_FILE_PORT "%d\n", df.port.value);
	os.Format(DMS_STATE_FILE_IP "%s\n", df.staticIp.toString().c_str());
	os.Format(DMS_STATE_FILE_USER_TIPS "%d\n", df.usertips.toBool());
	os.Format(DMS_STATE_FILE_APP_USER_TIPS "%d\n", df.appUsertips.toBool());
	//os.Format(DMS_STATE_FILE_PLAYMODE "%s\n", df.playmode.toString().c_str());
	os.Format(DMS_STATE_FILE_ALIAS_NAME "%s\n", df.aliasName.c_str());
	os.Format(DMS_STATE_FILE_BRIGHTNESS "%d\n", df.brightness.value);
	os.Format(DMS_STATE_FILE_PCSHARE "%d\n", df.pcshare.toBool());
	os.Format(DMS_STATE_FILE_MEDIASERVER "%d\n", df.mediaserver.toBool());

	auto cosl = df.coverOption.toOrderStringList();
	for (auto str : cosl) {
		os.Format(DMS_STATE_FILE_COVER_SOURCE "%s\n", str.c_str());
	}
	auto fcsl = df.folderCoverOption.sl;
	for (auto str : fcsl) {
		os.Format(DMS_STATE_FILE_FOLDER_COVER "%s\n", str.c_str());
	}
}

inline bool
DmsStateFile::Write(OutputStream &os, Error &error)
{
	BufferedOutputStream bos(os);
	Write(bos);
	return bos.Flush(error);
}

void
DmsStateFile::Write()
{
	FormatDefault(domain,
		    "Saving dms state file %s", path_utf8.c_str());

	Error error;
	FileOutputStream fos(path, error);
	if (!fos.IsDefined() || !Write(fos, error) || !fos.Commit(error)) {
		LogError(error);
		return;
	}

	RememberVersions();
}

void
DmsStateFile::Read()
{
	FormatDebug(domain, "Loading dms state file %s", path_utf8.c_str());

	Dms::Context &context = Dms::GetContext();
	Error error;
	TextFile file(path, error);
	if (file.HasFailed()) {
		LogError(error);
		return;
	}

	CoverOption::TYPE cosl[CoverOption::COVER_MAX];
	int pos_cosl = 0;
	memset(cosl, -1, sizeof(cosl));
	const char *line;
	while ((line = file.ReadLine()) != nullptr) {
		if (StringStartsWith(line, DMS_STATE_FILE_SOURCE)) {
			df.source.parse(line + strlen(DMS_STATE_FILE_SOURCE));
			df.source.reset();
		} else if (StringStartsWith(line, DMS_STATE_FILE_NETWORK_LAST)) {
			df.networkLastUri = line + strlen(DMS_STATE_FILE_NETWORK_LAST);
		} else if (StringStartsWith(line, DMS_STATE_FILE_INTERNET_LAST)) {
			df.internetLastUri = line + strlen(DMS_STATE_FILE_INTERNET_LAST);
		} else if (StringStartsWith(line, DMS_STATE_FILE_VOLUME_POLICY)) {
			df.volume_policy.parse(line + strlen(DMS_STATE_FILE_VOLUME_POLICY));
		} else if (StringStartsWith(line, DMS_STATE_FILE_VOLUME_MASTER)) {
			int volume_master;
			sscanf(line + strlen(DMS_STATE_FILE_VOLUME_MASTER), "%d", &volume_master);
			df.volume_master.parse(volume_master);
		} else if (StringStartsWith(line, DMS_STATE_FILE_VOLUME_INDEPENDENT)) {
			int volume_independent[CHANNEL_MAX];
			sscanf(line + strlen(DMS_STATE_FILE_VOLUME_INDEPENDENT),
				" %d %d %d %d %d %d %d",
				&volume_independent[0],
				&volume_independent[1],
				&volume_independent[2],
				&volume_independent[3],
				&volume_independent[4],
				&volume_independent[5],
				&volume_independent[6]
				);
			for (int i=0;i<CHANNEL_MAX;i++) {
				df.volume_independent[i].parse(volume_independent[i]);
			}
		} else if (StringStartsWith(line, DMS_STATE_FILE_SRC_INDEPENDENT)) {
			char SRC_independent[CHANNEL_MAX][20];
			sscanf(line + strlen(DMS_STATE_FILE_SRC_INDEPENDENT),
				" %s %s %s %s %s %s %s",
				SRC_independent[0],
				SRC_independent[1],
				SRC_independent[2],
				SRC_independent[3],
				SRC_independent[4],
				SRC_independent[5],
				SRC_independent[6]
				);
			for (int i=0;i<CHANNEL_MAX;i++) {
				df.SRC_independent[i].parse(SRC_independent[i]);
			}
		} else if (StringStartsWith(line, DMS_STATE_FILE_STARTUP)) {
			df.startup.parseStartup(line + strlen(DMS_STATE_FILE_STARTUP));
		} else if (StringStartsWith(line, DMS_STATE_FILE_IR)) {
			df.ir.parse(line + strlen(DMS_STATE_FILE_IR));
		} else if (StringStartsWith(line, DMS_STATE_FILE_NETWORK_TYPE)) {
			df.networkType.parse(line + strlen(DMS_STATE_FILE_NETWORK_TYPE));
		} else if (StringStartsWith(line, DMS_STATE_FILE_IP_ASSIGN)) {
			df.ipAssign.parse(line + strlen(DMS_STATE_FILE_IP_ASSIGN));
		} else if (StringStartsWith(line, DMS_STATE_FILE_PORT)) {
			df.port.parse(line + strlen(DMS_STATE_FILE_PORT));
		} else if (StringStartsWith(line, DMS_STATE_FILE_IP)) {
			df.staticIp.parse(line + strlen(DMS_STATE_FILE_IP));
		} else if (StringStartsWith(line, DMS_STATE_FILE_USER_TIPS)) {
			df.usertips.parse(line + strlen(DMS_STATE_FILE_USER_TIPS));
		} else if (StringStartsWith(line, DMS_STATE_FILE_APP_USER_TIPS)) {
			df.appUsertips.parse(line + strlen(DMS_STATE_FILE_APP_USER_TIPS));
		} else if (StringStartsWith(line, DMS_STATE_FILE_PLAYMODE)) {
			context.playmode.parse(line + strlen(DMS_STATE_FILE_PLAYMODE));
			context.playmode.apply(context.partition);
		} else if (StringStartsWith(line, DMS_STATE_FILE_ALIAS_NAME)) {
			df.aliasName = line + strlen(DMS_STATE_FILE_ALIAS_NAME);
		} else if (StringStartsWith(line, DMS_STATE_FILE_BRIGHTNESS)) {
			df.brightness.parse(line + strlen(DMS_STATE_FILE_ALIAS_NAME));
		} else if (StringStartsWith(line, DMS_STATE_FILE_PCSHARE)) {
			df.pcshare.parse(line + strlen(DMS_STATE_FILE_PCSHARE));
		} else if (StringStartsWith(line, DMS_STATE_FILE_MEDIASERVER)) {
			df.mediaserver.parse(line + strlen(DMS_STATE_FILE_MEDIASERVER));
		} else if (StringStartsWith(line, DMS_STATE_FILE_COVER_SOURCE)) {
			CoverOption::TYPE type;
			if (df.coverOption.parse(line + strlen(DMS_STATE_FILE_COVER_SOURCE), type)) {
				cosl[pos_cosl++] = type;
			}
		}
	}
	memcpy(df.coverOption.orders, cosl, sizeof(df.coverOption.orders));
	
	if (!dc.writeSource(df.source.value, df.getSRC().value)) {
		FormatError(domain, "write source SRC %s",DMS_IO_ERROR);
	}
	if (!dc.writeVolume(df.getVolume().value)) {
		FormatError(domain, "write volume %s",DMS_IO_ERROR);
	}
	if (!dc.writeIr(df.ir.value)) {
		FormatError(domain, "write IR %s",DMS_IO_ERROR);
	}
	if (df.brightness.value == 0) {
		df.brightness = DmsBrightness();
	}
	if (!dc.writeBrightness(df.brightness.value)) {
		FormatError(domain, "write brightness %s",DMS_IO_ERROR);
	}
	if (!dc.writeUserTips(df.usertips.toBool())) {
		FormatError(domain, "write usertips %s",DMS_IO_ERROR);
	}
	df.src = df.getSRC();
	RememberVersions();
}

void
DmsStateFile::CheckModified()
{
	if (!IsActive() && IsModified())
		ScheduleSeconds(interval);
}

void
DmsStateFile::OnTimeout()
{
	Write();
}
