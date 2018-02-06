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
#include "Stats.hxx"
#include "PlayerControl.hxx"
#include "client/Client.hxx"
#include "Partition.hxx"
#include "Instance.hxx"
#include "db/Selection.hxx"
#include "db/Interface.hxx"
#include "db/Stats.hxx"
#include "db/DatabaseLock.hxx"
#include "db/plugins/upnp/UpnpDatabasePlugin.hxx"
#include "db/plugins/simple/SimpleDatabasePlugin.hxx"
#include "db/plugins/simple/Directory.hxx"
#include "util/Error.hxx"
#include "system/Clock.hxx"
#include "Log.hxx"
#ifdef ENABLE_DATABASE
#include "db/update/Service.hxx"
#define COMMAND_STATUS_UPDATING_DB	"updating_db"
#endif
#include "util/Domain.hxx"
#include "Log.hxx"

static constexpr Domain stats_domain("main");

#ifndef WIN32
/**
 * The monotonic time stamp when MPD was started.  It is used to
 * calculate the uptime.
 */
static unsigned start_time;
#endif

#ifdef ENABLE_DATABASE

static DatabaseStats stats;

enum class StatsValidity : uint8_t {
	INVALID, VALID, FAILED,
};

static StatsValidity stats_validity = StatsValidity::INVALID;

#endif

void stats_global_init(void)
{
#ifndef WIN32
	start_time = MonotonicClockS();
#endif
}

#ifdef ENABLE_DATABASE

void
stats_invalidate()
{
	stats_validity = StatsValidity::INVALID;
}

static bool
stats_update(const Database &db, const DatabaseSelection &selection)
{
	switch (stats_validity) {
	case StatsValidity::INVALID:
		break;

	case StatsValidity::VALID:
		return true;

	case StatsValidity::FAILED:
		return false;
	}

	Error error;
	if (db.GetStats(selection, stats, error)) {
		stats_validity = StatsValidity::VALID;
		return true;
	} else {
		//LogError(error);
		FormatDefault(stats_domain, "%s %d get stats fail! %s", __func__, __LINE__, error.GetMessage());

		stats_validity = StatsValidity::FAILED;
		return false;
	}
}

static void
db_stats_print(Client &client)
{
	DmsConfig	&df = client.partition.df;
	DmsSource &source = df.source;
    Error error;
	const Database *db = client.GetDatabase(error);
	if  (db == nullptr) {
	      return;
	}
	
#ifdef ENABLE_DATABASE
	const UpdateService *update_service = client.partition.instance.update;
	unsigned updateJobId = update_service != nullptr
		? update_service->GetId()
		: 0;
	if (updateJobId != 0) {
		stats_validity = StatsValidity::INVALID;
		client_printf(client,
				  COMMAND_STATUS_UPDATING_DB ": %i\n",
				  updateJobId);
	}
#endif
	const char *uri = source.isUpnp() ? source.getName().c_str() : "";
	bool ignore_repeat = source.isUpnp() ? true : false;
	const DatabaseSelection selection(uri, true, ignore_repeat);
	if (!stats_update(*db, selection)) {
		FormatDefault(stats_domain, "%s %d get stats fail!", __func__, __LINE__);
		//return;
	}

	unsigned total_duration_s =
		std::chrono::duration_cast<std::chrono::seconds>(stats.total_duration).count();

	client_printf(client,
		      "artists: %u\n"
		      "albums: %u\n"
		      "songs: %u\n"
		      "db_playtime: %u\n",
		      stats.artist_count,
		      stats.album_count,
		      stats.song_count,
		      total_duration_s);

	time_t update_stamp = 0;
	std::list<std::string> list;
	Storage *_composite = client.partition.instance.storage;
	if (db->IsPlugin(simple_db_plugin) &&
		getAllMounts(_composite, list)) {
		SimpleDatabase *db2 = static_cast<SimpleDatabase*>(client.partition.instance.database);
		for (const auto &str : list) {
			db_lock();
			const auto lr = db2->GetRoot().LookupDirectory(str.c_str());
			db_unlock();
			if (lr.directory->IsMount()) {
				Database &_db2 = *(lr.directory->mounted_database);
				time_t t = _db2.GetUpdateStamp();
				update_stamp = t > update_stamp ? t : update_stamp;
			}
		}
	} else if (db->IsPlugin(upnp_db_plugin)) {
		update_stamp = db->GetUpdateStamp();
	}

	if (update_stamp > 0)
		client_printf(client,
			      "db_update: %lu\n",
			      (unsigned long)update_stamp);
}

#endif

void
stats_print(Client &client)
{
	client_printf(client,
		      "uptime: %u\n"
		      "playtime: %lu\n",
#ifdef WIN32
		      GetProcessUptimeS(),
#else
		      MonotonicClockS() - start_time,
#endif
		      (unsigned long)(client.player_control.GetTotalPlayTime() + 0.5));

	DmsConfig	&df = client.partition.df;
	if (!df.source.isUsb() &&
		!df.source.isNetwork()) {
		return;
	}

#ifdef ENABLE_DATABASE
	    db_stats_print(client);
#endif
}
