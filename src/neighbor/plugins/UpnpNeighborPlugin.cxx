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
#include "UpnpNeighborPlugin.hxx"
#include "lib/upnp/Domain.hxx"
#include "lib/upnp/ClientInit.hxx"
#include "lib/upnp/Discovery.hxx"
#include "lib/upnp/ContentDirectoryService.hxx"
#include "neighbor/NeighborPlugin.hxx"
#include "neighbor/Explorer.hxx"
#include "neighbor/Listener.hxx"
#include "neighbor/Info.hxx"
#include "Log.hxx"
#include "config/ConfigGlobal.hxx"
#include "config/ConfigError.hxx"
#include "config/Block.hxx"
#include "util/Domain.hxx"
#include <stdio.h>
static constexpr Domain upnp_neighbor_domain("upnp_neighbor");

class UpnpNeighborExplorer final
	: public NeighborExplorer, UPnPDiscoveryListener {
	struct Server {
		std::string name, comment;

		bool alive;

		Server(std::string &&_name, std::string &&_comment)
			:name(std::move(_name)), comment(std::move(_comment)),
			 alive(true) {}
		Server(const Server &) = delete;

		gcc_pure
		bool operator==(const Server &other) const {
			return name == other.name;
		}

		gcc_pure
		NeighborInfo Export() const {
			return { "smb://" + name + "/", comment,"", ""};  //???why smb
		}
	};

	UPnPDeviceDirectory *discovery;

public:
	UpnpNeighborExplorer(NeighborListener &_listener, const char *_name)
		:NeighborExplorer(_listener, _name), discovery(nullptr) {}

	/* virtual methods from class NeighborExplorer */
	virtual bool Open(Error &error) override;
	virtual void Close() override;
	virtual bool Reopen(Error &error) override;
	virtual List GetList() const override;

private:
	/* virtual methods from class UPnPDiscoveryListener */
	virtual void FoundUPnP(const ContentDirectoryService &service) override;
	virtual void LostUPnP(const ContentDirectoryService &service) override;
};

bool
UpnpNeighborExplorer::Open(Error &error)
{
	UpnpClient_Handle handle;
	if (!UpnpClientGlobalInit(handle, error))
		return false;

	discovery = new UPnPDeviceDirectory(handle, this);
	if (!discovery->Start(error)) {
		delete discovery;
		discovery = nullptr;
		UpnpClientGlobalFinish();
		return false;
	}

	return true;
}

void
UpnpNeighborExplorer::Close()
{
	if (discovery != nullptr) {
		delete discovery;
		discovery = nullptr;
		UpnpClientGlobalFinish();
	}
}

bool
UpnpNeighborExplorer::Reopen(Error &error)
{
	FormatDefault(upnp_neighbor_domain, "%s %d", __func__, __LINE__);
	Close();
	if (!Open(error)) {
		FormatDefault(upnp_neighbor_domain, "%s %d open error=%s", __func__, __LINE__, error.GetMessage());
		return false;
	}

	return true;
}

NeighborExplorer::List
UpnpNeighborExplorer::GetList() const
{
	std::vector<ContentDirectoryService> tmp;

	{
		Error error;
		if (!discovery || !discovery->GetDirectories(tmp, error)) {
			//LogError(error);
		}
	}

	List result;
	for (const auto &i : tmp) {
		result.emplace_front(i.GetURI(), i.getFriendlyName(),i.getDeviceIconUrl(), "");
	
	}

	return result;
}

void
UpnpNeighborExplorer::FoundUPnP(const ContentDirectoryService &service)
{
	const NeighborInfo n(service.GetURI(), service.getFriendlyName(),service.getDeviceIconUrl(), "");
	listener.FoundNeighbor(n);
}

void
UpnpNeighborExplorer::LostUPnP(const ContentDirectoryService &service)
{
	const NeighborInfo n(service.GetURI(), service.getFriendlyName(),service.getDeviceIconUrl(), "");
	listener.LostNeighbor(n);
}

static NeighborExplorer *
upnp_neighbor_create(gcc_unused EventLoop &loop,
		     NeighborListener &listener,
		     const ConfigBlock &block,
		     gcc_unused Error &error)
{
	return new UpnpNeighborExplorer(listener, block.GetBlockValue("plugin"));
}

const NeighborPlugin upnp_neighbor_plugin = {
	"upnp",
	upnp_neighbor_create,
};
