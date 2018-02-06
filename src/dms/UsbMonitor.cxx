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
#include "UsbMonitor.hxx"
#include "util/StringUtil.hxx"
#include "util/RuntimeError.hxx"
#include "dms/util/IOReader.hxx"
#include "dms/util/IOWriter.hxx"
#include "Idle.hxx"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <linux/netlink.h>


namespace Dms {


UsbMonitor::UsbMonitor(EventLoop &_loop)
	: SocketMonitor(_loop), TimeoutMonitor(_loop)
{

}

UsbMonitor::~UsbMonitor()
{
	Stop();
}

void
UsbMonitor::Start()
{
	const int buffersize = 1024;
	int ret;

	struct sockaddr_nl snl;
	bzero(&snl, sizeof(struct sockaddr_nl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 1;

	int fdd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (fdd < 0) {
		throw FormatRuntimeError("socket NETLINK_KOBJECT_UEVENT failed");
	}
	setsockopt(fdd, SOL_SOCKET, SO_RCVBUF, &buffersize, sizeof(buffersize));

	ret = bind(fdd, (struct sockaddr *)&snl, sizeof(struct sockaddr_nl));
	if (ret < 0) {
		close(fdd);
		throw FormatRuntimeError("bind NETLINK_KOBJECT_UEVENT failed");
	}

	SocketMonitor::Open(fdd);
	SocketMonitor::ScheduleRead();
	OnTimeout();
}

void
UsbMonitor::Stop()
{
	if(SocketMonitor::IsDefined()) {
		SocketMonitor::Close();
	}
}

bool
UsbMonitor::OnSocketReady(gcc_unused unsigned flags)
{
	char buf[4096];

	int len = recv(SocketMonitor::Get(), &buf, sizeof(buf), 0);
	if (len < 0) {
		return true;
	}

	//fprintf(stderr, "= %d %s\n", len, buf);
	if (StringStartsWith(buf, "add@/devices/virtual")) {
		TimeoutMonitor::Schedule(100);
	} else if (StringStartsWith(buf, "remove@/devices/virtual")) {
		TimeoutMonitor::Schedule(100);
	}

	return true;
}

UsbStorage
UsbMonitor::LockUpdate()
{
	std::lock_guard<Mutex> locker(mutex);

	usbs.query();
	usbs.sort();

	return usbs;
}

void
UsbMonitor::OnTimeout()
{
	LockUpdate();
	idle_add(IDLE_LOCAL);
}

}
