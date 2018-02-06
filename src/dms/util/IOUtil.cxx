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
#include "IOUtil.hxx"
#include "thread/Mutex.hxx"
#include "IOReader.hxx"
#include "fs/io/FileOutputStream.hxx"
#include "fs/io/BufferedOutputStream.hxx"
#include "fs/AllocatedPath.hxx"
#include "util/StringUtil.hxx"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <sys/wait.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <mutex>

namespace Dms {

bool get_mac_addr(struct sockaddr &addr, const char *interface)
{
	struct ifreq ifreq;
	int sock;

	if (interface == nullptr) {
		return false;
	}
	sock = socket(AF_INET,SOCK_STREAM, 0);
	if (sock < 0) {
		return false;
	}
	strcpy(ifreq.ifr_name, interface);
	if(ioctl(sock, SIOCGIFHWADDR, &ifreq) < 0) {
		return false;
	}
	memcpy(&addr, &ifreq.ifr_hwaddr, sizeof(addr));

	return true;
}

bool get_active_mac_addr(struct sockaddr &addr, const char *interface)
{
	struct ifaddrs *ifa = NULL, *ifList;
	bool ret;

	if (getifaddrs(&ifList) < 0) {
		return false;
	}
	for (ifa = ifList; ifa != NULL; ifa = ifa->ifa_next) {
		if(ifa->ifa_addr->sa_family == AF_INET) {
			if((ifa->ifa_flags & IFF_UP)
				&& (ifa->ifa_flags & IFF_BROADCAST)
				&& (ifa->ifa_flags & IFF_MULTICAST)
				&& !(ifa->ifa_flags & IFF_LOOPBACK)) {
				interface = ifa->ifa_name;
				break;
			}
		}
	}

	ret = get_mac_addr(addr, interface);
	freeifaddrs(ifList);
	return ret;
}

int system2(const char *cmdstring)
{
	pid_t pid;
	int status;

	if (cmdstring == nullptr) {
		return 1;
	}

	if ((pid = fork()) < 0) {
		status = -1;
	} else if (pid == 0) {
		execl("/system/bin/sh", "sh", "-c", cmdstring, nullptr);
		exit(127);
	} else {
		while(waitpid(pid, &status, 0) < 0) {
			if(errno != EINTR) {
				status = -1;
				break;
			}
		}
	}

	return status;
}

int system_with_back(const char* cmd, std::string &buf)
{
	static Mutex mutex;
	int fd[2];
	int bak_fd;
	int new_fd;
	const std::lock_guard<Mutex> protect(mutex);

	if(pipe(fd))   {
		printf("pipe error!\n");
		return -1;
	}

	bak_fd = dup(STDOUT_FILENO);
	new_fd = dup2(fd[1], STDOUT_FILENO);

	system2(cmd);
	char buffer[4096];
	size_t len = read(fd[0], buffer, sizeof(buffer)-1);
	if (len > 0) {
		buf.append(buffer, len);
	}
	dup2(bak_fd, new_fd);
	close(fd[0]);
	close(fd[1]);
	close(bak_fd);

	return len;
}

#define PATH_MPD_QUEUE	"/data/mpd/queue"

void delete_all_queue()
{
	struct dirent *ep;
	DIR *dp = opendir(PATH_MPD_QUEUE);
	if (dp != nullptr) {
		while ((ep = readdir(dp)) != nullptr) {
			if (ep->d_type == DT_REG) {
				std::string filepath = PATH_MPD_QUEUE;
				filepath.append("/");
				filepath.append(ep->d_name);
				remove(filepath.c_str());
			}
		}
		closedir(dp);
	}
}

#define PATH_DMS_SN			"/sys/class/rksn/sn"

std::string get_sn()
try {
	char buf[50] = {0};
	IOReader reader(PATH_DMS_SN);
	reader.read((void*)buf, sizeof(buf) - 1);
	return std::string(buf);
} catch (...) {
	return std::string();
}

}
