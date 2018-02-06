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
 
#pragma once

#include <string.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string>

namespace Dms {

bool get_mac_addr(struct sockaddr &addr, const char *interface);

bool get_active_mac_addr(struct sockaddr &addr, const char *interface = "eth0");

int system2(const char *cmdstring);

int system_with_back(const char* cmd, std::string &buf);

void delete_all_queue();

std::string get_sn();

}
