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
#include "NeighborCommands.hxx"
#include "client/Client.hxx"
#include "Instance.hxx"
#include "Partition.hxx"
#include "protocol/Result.hxx"
#include "neighbor/Glue.hxx"
#include "neighbor/Info.hxx"
#include "neighbor/Explorer.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringUtil.hxx"
#include "dms/DmsConfig.hxx"
#include "dms/Context.hxx"
#include "db/Interface.hxx"

#include <set>
#include <string>

#include <assert.h>

bool
neighbor_commands_available(const Instance &instance)
{
	return instance.neighbors != nullptr;
}

CommandResult
handle_listneighbors(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	const NeighborGlue *const neighbors =
		client.partition.instance.neighbors;
	DmsConfig	&df = client.partition.df;
	bool	pcshare = df.pcshare.toBool();
	bool	mediaserver = df.mediaserver.toBool();
	
	if (neighbors == nullptr) {
		command_error(client, ACK_ERROR_UNKNOWN,
			      "No neighbor plugin configured");
		return CommandResult::ERROR;
	}

	for (const auto &i : neighbors->GetList()) {
		if (StringStartsWith(i.uri.c_str(), "smb://") && !pcshare) {
			continue;
		} else if (StringStartsWith(i.uri.c_str(), "upnp://") && !mediaserver) {
			continue;
		}
		client_printf(client,
			      "neighbor: %s\n"
			      "name: %s\n",			    
			      i.uri.c_str(),
			      i.display_name.c_str()
			     );
		if  (!i.device_icon_url.empty())  {
			client_printf(client,
			          "icon_url: %s\n",
				  i.device_icon_url.c_str());
		}
	}
	return CommandResult::OK;
}

CommandResult
handle_scanNeighbors(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	DmsConfig &df = client.partition.df;
	DmsSource &source = df.source;
	Error error;
	Database *upnpdatabase = client.partition.instance.upnpdatabase;
	NeighborGlue *neighbors =
		client.partition.instance.neighbors;
	if (source.isNetwork() ||
		!client.context.poweron.isRunning()) {
		return CommandResult::OK;
	}
	if (neighbors == nullptr) {
		command_error(client, ACK_ERROR_UNKNOWN,
			      "No neighbor plugin configured");
		return CommandResult::ERROR;
	}

	auto state = client.player_control.GetState();
	if (state == PlayerState::PLAY) {
		client.player_control.Pause();
	}

	if (upnpdatabase != nullptr) {
		upnpdatabase->Close();
	}

	if (!neighbors->Reopen(error)) {
		command_error(client, ACK_ERROR_SYSTEM,
			      "scan neighbor fail!");
		return CommandResult::ERROR;
	}
	if (upnpdatabase != nullptr
		&& !upnpdatabase->Open(error)) {
		command_error(client, ACK_ERROR_SYSTEM,
				  "open upnp error:%s",
				  error.GetMessage());
		return CommandResult::ERROR;
	}

	return CommandResult::OK;
}

