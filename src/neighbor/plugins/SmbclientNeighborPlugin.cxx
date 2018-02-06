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
#include "SmbclientNeighborPlugin.hxx"
#include "lib/smbclient/Init.hxx"
#include "lib/smbclient/Domain.hxx"
#include "lib/smbclient/Mutex.hxx"
#include "neighbor/NeighborPlugin.hxx"
#include "neighbor/Explorer.hxx"
#include "neighbor/Listener.hxx"
#include "neighbor/Info.hxx"
#include "thread/Mutex.hxx"
#include "thread/Cond.hxx"
#include "thread/Thread.hxx"
#include "thread/Name.hxx"
#include "util/Macros.hxx"
#include "util/Domain.hxx"
#include "util/Error.hxx"
#include "Log.hxx"
#include "config/ConfigGlobal.hxx"
#include "config/ConfigError.hxx"
#include "config/Block.hxx"
#include "dms/DmsConfig.hxx"

#include <libsmbclient.h>

#include <list>
#include <algorithm>

#include "util/Domain.hxx"
#include "Log.hxx"

#include "Instance.hxx"
#include "PlayerControl.hxx"
#include "Partition.hxx"
#include <unistd.h>

//This value determines the maximum number of local master browsers to query for the list of workgroups  
#define MAX_LMB_COUNT  10

extern Instance *instance;

static constexpr Domain smbclient_neighbor("smbclient_neighbor");

static bool isPlaying(void)
{
	if (instance != nullptr && instance->partition != nullptr) {
		PlayerState PlayState = instance->partition->pc.GetState();
		if(PlayState == PlayerState::PLAY) {
			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}

class SmbclientNeighborExplorer final : public NeighborExplorer {
	struct Server {
		std::string name, comment, workgroup;

		bool alive;

		Server(std::string &&_name, std::string &&_comment, std::string &&_workgroup)
			:name(std::move(_name)), comment(std::move(_comment)), workgroup(_workgroup),
			 alive(true) {}
		Server(const Server &) = delete;

		gcc_pure
		bool operator==(const Server &other) const {
			return name == other.name;
		}

		gcc_pure
		NeighborInfo Export() const {
			return { "smb://" + name + "/", comment,"", workgroup};
		}
	};

	Thread thread;

	mutable Mutex mutex;
	Cond cond;

	List list;

	bool quit;

public:
	SmbclientNeighborExplorer(NeighborListener &_listener, const char *_name)
		:NeighborExplorer(_listener, _name) {}

	/* virtual methods from class NeighborExplorer */
	virtual bool Open(Error &error) override;
	virtual bool Reopen(Error &error) override;
	virtual void Close() override;
	virtual List GetList() const override;

private:
	void Run();
	void ThreadFunc();
	static void ThreadFunc(void *ctx);
};

bool
SmbclientNeighborExplorer::Open(Error &error)
{
	quit = false;
	list.clear();
	return thread.Start(ThreadFunc, this, error);
}

bool
SmbclientNeighborExplorer::Reopen(Error &error)
{
	FormatDefault(smbclient_neighbor, "%s %d", __func__, __LINE__);
	Close();
	if (!Open(error)) {
		FormatError(smbclient_neighbor, "%s %s", __func__, error.GetMessage());
		return false;
	}

	return true;
}

void
SmbclientNeighborExplorer::Close()
{
	mutex.lock();
	quit = true;
	cond.signal();
	mutex.unlock();

	thread.Join();
}

NeighborExplorer::List
SmbclientNeighborExplorer::GetList() const
{
	//const ScopeLock protect(mutex);
	/*
	List list;
	for (const auto &i : servers)
		list.emplace_front(i.Export());
	*/
	return list;
}

static void
ReadServer(NeighborExplorer::List &list, const smbc_dirent &e, const char *wg)
{
	const std::string name(e.name, e.namelen);
	std::string comment(e.comment, e.commentlen);

	if (comment.empty() || comment.compare("")==0) {
		comment = name;
	}
	list.emplace_front("smb://" + name, comment, "", wg);
}

static void
ReadServers(NeighborExplorer::List &list, const char *uri, const char *wg);

static void
ReadWorkgroup(NeighborExplorer::List &list, const std::string &name)
{
	std::string uri = "smb://" + name;
	ReadServers(list, uri.c_str(), name.c_str());
}

static void
ReadEntry(NeighborExplorer::List &list, const smbc_dirent &e, const char *wg)
{
	switch (e.smbc_type) {
	case SMBC_WORKGROUP:
		ReadWorkgroup(list, std::string(e.name, e.namelen));
		break;

	case SMBC_SERVER:
		ReadServer(list, e, wg);
		break;
	}
}

static void
ReadServers(NeighborExplorer::List &list, int fd, const char *wg)
{
	smbc_dirent *e;
	while ((e = smbc_readdir(fd)) != nullptr)
		ReadEntry(list, *e, wg);

	smbc_closedir(fd);
}

static void
ReadServers(NeighborExplorer::List &list, const char *uri, const char *wg)
{
	int fd = smbc_opendir(uri);
	if (fd >= 0) {
		ReadServers(list, fd, wg);
		smbc_closedir(fd);
	} else
		FormatErrno(smbclient_domain, "smbc_opendir('%s') failed",
			    uri);
}

gcc_pure
static NeighborExplorer::List
DetectServers()
{
	NeighborExplorer::List list;
	const ScopeLock protect(smbclient_mutex);
	ReadServers(list, "smb://", "");
	return list;
}

gcc_pure
static NeighborExplorer::List::const_iterator
FindBeforeServerByURI(NeighborExplorer::List::const_iterator prev,
		      NeighborExplorer::List::const_iterator end,
		      const std::string &uri)
{
	for (auto i = std::next(prev); i != end; prev = i, i = std::next(prev))
		if (i->uri == uri)
			return prev;

	return end;
}

inline void
SmbclientNeighborExplorer::Run()
{
	List found = DetectServers(), found2;

	mutex.lock();


	for (auto &i : found) {
		bool is_in_list = false;
		for (auto &j : list) {
			if (i.uri == j.uri) {
				is_in_list = true;
				break;
			}
		}
		if (!is_in_list) {
			found2.push_front(i);
		}
	}
	for (auto &i : found2) {
		list.push_front(i);
	}
	
	mutex.unlock();

	for (auto &i : found2) {
		listener.FoundNeighbor(i);
		FormatDefault(smbclient_neighbor, "FoundNeighbor\n workgroup:%s\n uri:%s\n display_name:%s", 
			i.workgroup.c_str(), i.uri.c_str(), i.display_name.c_str());
	}
}

inline void
SmbclientNeighborExplorer::ThreadFunc()
{
	mutex.lock();
	scanning = 10;

	do {
		mutex.unlock();
		if(isPlaying())	{
			scanning = 0;
			mutex.lock();
			cond.timed_wait(mutex, 3000);
			if (quit)
				break;
			continue;
		}
		if (instance != nullptr && instance->partition != nullptr) {
			DmsConfig	&df = instance->partition->df;
			if (df.source.value != SOURCE_NETWORK_SOURCE) {
				scanning = 0;
				mutex.lock();
				cond.timed_wait(mutex, 3000);
				if (quit)
					break;
				continue;
			}
		}
		//FormatDefault(smbclient_neighbor, "%s start", __func__);
		Run();
		//FormatDefault(smbclient_neighbor, "%s end", __func__);

		mutex.lock();
		if (quit)
			break;

		// TODO: sleep for how long?
		if (scanning) {
			scanning--;
			cond.timed_wait(mutex, 1000);
		} else {
			cond.timed_wait(mutex, 5000);
		}
		
	} while (!quit);
	scanning = 0;

	mutex.unlock();
}

void
SmbclientNeighborExplorer::ThreadFunc(void *ctx)
{
	SetThreadName("smbclient");

	SmbclientNeighborExplorer &e = *(SmbclientNeighborExplorer *)ctx;
	e.ThreadFunc();
}

static NeighborExplorer *
smbclient_neighbor_create(gcc_unused EventLoop &loop,
			  NeighborListener &listener,
			  const ConfigBlock &block,
			  gcc_unused Error &error)
{
	if (!SmbclientInit(error))
		return nullptr;

	return new SmbclientNeighborExplorer(listener, block.GetBlockValue("plugin"));
}

const NeighborPlugin smbclient_neighbor_plugin = {
	"smbclient",
	smbclient_neighbor_create,
};
