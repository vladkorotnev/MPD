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
 
#ifndef DMS_COMMANDS_HXX
#define DMS_COMMANDS_HXX

#include "dms/DmsConfig.hxx"
#include "dms/DmsControl.hxx"
#include "CommandResult.hxx"

class Client;
template<typename T> struct ConstBuffer;

const char *
source_to_string(unsigned source);

const char *
volume_policy_to_string (enum DMS_VOLUME_POLICY policy);

const char *
SRC_to_string (enum DMS_RATE src);

CommandResult
handle_dmsUsb(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsExternal(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsSourceScreen(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsNetworkSource(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsRenderer(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsInternet(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmssource(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsversion(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsSRC(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsRate(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_setvol(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsvolume(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsvolumepolicy(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsStartup(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsIr(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsNetwork(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsUserTips(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsAppUserTips(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsBrightness(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsMute(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsHmute(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_listLocals(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_unmountAll(Client &client, gcc_unused ConstBuffer<const char *> args);

CommandResult
handle_playmode(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_dmsUpdate(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_poweron(Client &client, ConstBuffer<const char *> args);

bool unmountAll();

CommandResult
handle_dmsBluetoothStatus(Client &client, gcc_unused ConstBuffer<const char *> args);

CommandResult
handle_getSn(Client &client, gcc_unused ConstBuffer<const char *> args);

CommandResult
handle_aliasName(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_neighborOptions(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_coverSource(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_folderCoverPatterns(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_listSources(Client &client, gcc_unused ConstBuffer<const char *> args);

CommandResult
handle_listStartups(Client &client, gcc_unused ConstBuffer<const char *> args);

CommandResult
handle_renameSourceName(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_loadSourceQueue(Client &client, gcc_unused ConstBuffer<const char *> args);

CommandResult
handle_set_config(Client &client, ConstBuffer<const char *> args);

CommandResult
handle_list_config(Client &client, ConstBuffer<const char *> args);

#endif
