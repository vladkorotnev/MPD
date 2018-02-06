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

#define __STDC_FORMAT_MACROS /* for PRIu64 */

#include "config.h"
#include "FileCommands.hxx"
#include "CommandError.hxx"
#include "protocol/Ack.hxx"
#include "protocol/Result.hxx"
#include "client/Client.hxx"
#include "util/ConstBuffer.hxx"
#include "util/CharUtil.hxx"
#include "util/UriUtil.hxx"
#include "util/Error.hxx"
#include "util/StringUtil.hxx"
#include "tag/TagHandler.hxx"
#include "tag/ApeTag.hxx"
#include "tag/TagId3.hxx"
#include "TagStream.hxx"
#include "TagFile.hxx"
#include "storage/StorageInterface.hxx"
#include "storage/Registry.hxx"
#include "fs/AllocatedPath.hxx"
#include "fs/FileInfo.hxx"
#include "fs/DirectoryReader.hxx"
#include "TimePrint.hxx"
#include "ls.hxx"
#include "Partition.hxx"
#include "dms/DmsConfig.hxx"
#include "input/InputStream.hxx"
#include "input/LocalOpen.hxx"
#include "Log.hxx"
#include "util/Domain.hxx"
#include "util/ASCII.hxx"
#include "IOThread.hxx"
#include "Instance.hxx"

#include <assert.h>
#include <sys/stat.h>
#include <inttypes.h> /* for PRIu64 */

#include <stdio.h>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

static constexpr Domain domain("file_commands");
gcc_pure
static bool
SkipNameFS(PathTraitsFS::const_pointer name_fs)
{
	return name_fs[0] == '.' &&
		(name_fs[1] == 0 ||
		 (name_fs[1] == '.' && name_fs[2] == 0));
}

gcc_pure
static bool
skip_path(Path name_fs)
{
	return name_fs.HasNewline();
}

#if defined(WIN32) && GCC_CHECK_VERSION(4,6)
/* PRIu64 causes bogus compiler warning */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#endif

static const char *const support_picture_tbl[] = {
	"jpg",
	"jpeg",
	"png",
	"bmp",
};
static bool
IsSupportPicture(const std::string &filename)
{
	fs::path path(filename);

	if (!path.has_extension()) {
		return false;
	}
	const char *suffix = path.extension().c_str()+1;

	for (const auto &p : support_picture_tbl) {
		if (StringEqualsCaseASCII(p, suffix)) {
			return true;
		}
	}

	return false;
}

static std::vector<std::string>
GetPictureList(const std::string &path_str)
{
	std::vector<std::string> list;
	fs::path path(path_str);
	fs::directory_iterator end;

	for (fs::directory_iterator it(path); it != end; it++) {
		auto dd_filename = it->path().filename().string();
		if (fs::is_regular_file(*it)) {
			if (IsSupportPicture(dd_filename)) {
				list.push_back(path_str + dd_filename);
			}
		}
	}

	return list;
}

CommandResult
handle_listfiles_local(Client &client, const char *path_utf8)
{
	const auto path_fs = AllocatedPath::FromUTF8(path_utf8);
	if (path_fs.IsNull()) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "unsupported file name");
		return CommandResult::ERROR;
	}

	Error error;
	if (!client.AllowFile(path_fs, error))
		return print_error(client, error);

	DirectoryReader reader(path_fs);
	if (reader.HasFailed()) {
		error.FormatErrno("Failed to open '%s'", path_utf8);
		return print_error(client, error);
	}

	while (reader.ReadEntry()) {
		const Path name_fs = reader.GetEntry();
		if (SkipNameFS(name_fs.c_str()) || skip_path(name_fs))
			continue;

		std::string name_utf8 = name_fs.ToUTF8();
		if (name_utf8.empty())
			continue;

		const AllocatedPath full_fs =
			AllocatedPath::Build(path_fs, name_fs);
		FileInfo fi;
		if (!GetFileInfo(full_fs, fi, false))
			continue;

		if (fi.IsRegular())
			client_printf(client, "file: %s\n"
				      "size: %" PRIu64 "\n",
				      name_utf8.c_str(),
				      fi.GetSize());
		else if (fi.IsDirectory())
			client_printf(client, "directory: %s\n",
				      name_utf8.c_str());
		else
			continue;

		time_print(client, "Last-Modified", fi.GetModificationTime());
	}

	return CommandResult::OK;
}

#if defined(WIN32) && GCC_CHECK_VERSION(4,6)
#pragma GCC diagnostic pop
#endif

gcc_pure
static bool
IsValidName(const char *p)
{
	if (!IsAlphaASCII(*p))
		return false;

	while (*++p) {
		const char ch = *p;
		if (!IsAlphaASCII(ch) && ch != '_' && ch != '-')
			return false;
	}

	return true;
}

gcc_pure
static bool
IsValidValue(const char *p)
{
	while (*p) {
		const char ch = *p++;

		if ((unsigned char)ch < 0x20)
			return false;
	}

	return true;
}

static void 
printf_duration(SongTime duration, void *ctx)
{
	Client &client = *(Client *)ctx;

	if (duration.IsPositive())
		client_printf(client, "Time: %i\n"
		  	    "duration: %1.3f\n",
		   	   duration.RoundS(),
		   	   duration.ToDoubleS());

}

//tag_print(Client &client, TagType type, const char *value)
//void (*tag)(TagType type, const char *value, void *ctx);
static void 
printf_tag(TagType type, const char *value, void *ctx)
{
	Client &client = *(Client *)ctx;
	if(value != nullptr)
		client_printf(client, "%s: %s\n", tag_item_names[type], value);

}


static void
print_pair(const char *key, const char *value, void *ctx)
{
	Client &client = *(Client *)ctx;

	if (IsValidName(key) && IsValidValue(value))
		client_printf(client, "%s: %s\n", key, value);
}

static constexpr tag_handler print_comment_handler = {
	printf_duration,
	printf_tag,
	print_pair,
	nullptr,
};

static CommandResult
read_stream_comments(Client &client, const char *uri)
{
	if (!uri_supported_scheme(uri)) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "unsupported URI scheme");
		return CommandResult::ERROR;
	}

	if (!tag_stream_scan(uri, print_comment_handler, &client)) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "Failed to load file");
		return CommandResult::ERROR;
	}

	return CommandResult::OK;

}

static CommandResult
read_file_comments(Client &client, const Path path_fs)
{
	if (!tag_file_scan(path_fs, print_comment_handler, &client)) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "Failed to load file");
		return CommandResult::ERROR;
	}

	tag_ape_scan2(path_fs, &print_comment_handler, &client);
	tag_id3_scan(path_fs, &print_comment_handler, &client);

	return CommandResult::OK;

}

static const char *
translate_uri(const char *uri)
{
	if (memcmp(uri, "file:///", 8) == 0)
		/* drop the "file://", leave only an absolute path
		   (starting with a slash) */
		return uri + 7;

	return uri;
}

CommandResult
handle_read_comments(Client &client, ConstBuffer<const char *> args)
{
	assert(args.size == 1);
	const char *const uri = translate_uri(args.front());

	if (memcmp(uri, "file:///", 8) == 0) {
		/* read comments from arbitrary local file */
		const char *path_utf8 = uri + 7;
		AllocatedPath path_fs = AllocatedPath::FromUTF8(path_utf8);
		if (path_fs.IsNull()) {
			command_error(client, ACK_ERROR_NO_EXIST,
				      "unsupported file name");
			return CommandResult::ERROR;
		}

		Error error;
		if (!client.AllowFile(path_fs, error))
			return print_error(client, error);

		return read_file_comments(client, path_fs);
	} else if (uri_has_scheme(uri)) {
		return read_stream_comments(client, uri);
	} else if (!PathTraitsUTF8::IsAbsolute(uri)) {
#ifdef ENABLE_DATABASE
		const Storage *storage = client.GetStorage();
		if (storage == nullptr) {
#endif
			command_error(client, ACK_ERROR_NO_EXIST,
				      "No database");
			return CommandResult::ERROR;
#ifdef ENABLE_DATABASE
		}

		{
			AllocatedPath path_fs = storage->MapFS(uri);
			if (!path_fs.IsNull())
				return read_file_comments(client, path_fs);
		}

		{
			const std::string uri2 = storage->MapUTF8(uri);
			if (uri_has_scheme(uri2.c_str()))
				return read_stream_comments(client,
							    uri2.c_str());
		}

		command_error(client, ACK_ERROR_NO_EXIST, "No such file");
		return CommandResult::ERROR;
#endif
	} else {
		command_error(client, ACK_ERROR_NO_EXIST, "No such file");
		return CommandResult::ERROR;
	}
}

static int cover_count = 0;
static inline void inc_cover_count()
{
	cover_count++;
}
static inline void clear_cover_cnt()
{
	cover_count = 0;
}
static inline int get_cover_cnt()
{
	return cover_count;
}

static void
print_cover(CoverType type, const char *value, void *ctx, size_t length=0)
{
	Client &client = *(Client *)ctx;

	if (get_cover_cnt() > 0) {
		return;
	}
	if (length == 0) {
		client_printf(client, "%s: %s\n", cover_item_names[type], value);
	} else {
		client_printf(client, "%s: \n", cover_item_names[type]);
		client_write(client, value, length);
		client_printf(client, "\n");
		inc_cover_count();
	}
}

static constexpr tag_handler print_cover_handler = {
	nullptr,
	nullptr,
	nullptr,
	print_cover,
};

static CommandResult
read_stream_covers(Client &client, const char *uri)
{
	if (!uri_supported_scheme(uri)) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "unsupported URI scheme");
		return CommandResult::ERROR;
	}

	if (!tag_stream_scan(uri, print_cover_handler, &client)) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "Failed to load file");
		return CommandResult::ERROR;
	}

	return CommandResult::OK;

}

static CommandResult
read_file_covers(Client &client, const Path path_fs)
{
	if (!tag_file_scan(path_fs, print_cover_handler, &client)) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "Failed to load file");
		return CommandResult::ERROR;
	}

	return CommandResult::OK;

}

static bool cover_scan_stream(InputStream &is, gcc_unused std::string str,
	const tag_handler &handler, void *handler_ctx, Error &error)
{
	size_t size = is.GetSize();
	if (size == 0)
		return false;
	if (size > 5*1024*1024) {
		return false;
	}
	size_t pos = 0;
	char *buf = new char[size];
	while (size > pos) {
		size_t nbytes = is.Read(buf+pos , size-pos, error);
		if (nbytes == 0 && is.IsEOF()) {
			break;
		}
		pos += nbytes;
	}
	if (size != pos) {
		delete[] buf;
		return false;
	}
	char length[21];
	snprintf(length, sizeof(length)-1, "%u", size);
	tag_handler_invoke_cover(&handler, handler_ctx, COVER_LENGTH, (const char*)length);
	tag_handler_invoke_cover(&handler, handler_ctx, COVER_DATA, buf, size);
	delete[] buf;

	return true;
}

static bool read_local_folder_covers(Client &client, Path path_uri)
{
	Mutex mutex;
	Cond cond;
	InputStream *is = nullptr;
	Error error;
	bool ret = false;

	std::string cover_uri = path_uri.c_str();
	if (cover_uri.back() != '/')
		cover_uri += "/";
	auto list = GetPictureList(cover_uri);
	for (const auto &path_str : list) {
		AllocatedPath path_fs = AllocatedPath::FromUTF8(path_str.c_str());
		if (path_fs.IsNull())
			continue;
		is = OpenLocalInputStream(path_fs,
					  mutex, cond,
					  IgnoreError());
		if (is == nullptr)
			continue;
		ret = cover_scan_stream(*is, path_str, print_cover_handler, &client, error);
		delete is;
		if (ret) {
			break;
		}
	}

	return ret;
}

static bool read_stream_folder_covers(Client &client, const char *path_uri)
{
	Mutex mutex;
	Cond cond;
	InputStream *is = nullptr;
	Error error;
	bool ret = false;

	Storage *storage = client.partition.instance.storage;
	auto dir = storage->OpenDirectory(path_uri, error);
	if (dir == nullptr) {
		return false;
	}
	const char *filename;
	dir->Lock();
	std::string cover_uri = storage->MapUTF8(path_uri);
	if (cover_uri.back() != '/')
		cover_uri += "/";
	std::vector<std::string> list;
	while ((filename = dir->Read()) != nullptr) {
		if (IsSupportPicture(filename)) {
			list.push_back(cover_uri + filename);
		}
	}
	dir->Unlock();
	delete dir;

	for (const auto &path_str : list) {
		is = InputStream::OpenReady(path_str.c_str(),
					  mutex, cond,
					  IgnoreError());
		if (is == nullptr)
			continue;
		ret = cover_scan_stream(*is, path_str, print_cover_handler, &client, error);
		delete is;
		if (ret) {
			break;
		}
	}

	return ret;
}

CommandResult
handle_read_covers(Client &client, ConstBuffer<const char *> args)
{
	assert(args.size == 1);
	const char *const uri = translate_uri(args.front());

	clear_cover_cnt();
	if (memcmp(uri, "file:///", 8) == 0) {
		/* read comments from arbitrary local file */
		const char *path_utf8 = uri + 7;
		AllocatedPath path_fs = AllocatedPath::FromUTF8(path_utf8);
		if (path_fs.IsNull()) {
			command_error(client, ACK_ERROR_NO_EXIST,
				      "unsupported file name");
			return CommandResult::ERROR;
		}

		Error error;
		if (!client.AllowFile(path_fs, error))
			return print_error(client, error);

		return read_file_covers(client, path_fs);
	} else if (uri_has_scheme(uri)) {
		return read_stream_covers(client, uri);
	} else if (!PathTraitsUTF8::IsAbsolute(uri)) {
#ifdef ENABLE_DATABASE
		const Storage *storage = client.GetStorage();
		if (storage == nullptr) {
#endif
			command_error(client, ACK_ERROR_NO_EXIST,
				      "No database");
			return CommandResult::ERROR;
#ifdef ENABLE_DATABASE
		}

		{
			AllocatedPath path_fs = storage->MapFS(uri);
			if (!path_fs.IsNull())
				return read_file_covers(client, path_fs);
		}

		{
			const std::string uri2 = storage->MapUTF8(uri);
			if (uri_has_scheme(uri2.c_str()))
				return read_stream_covers(client,
							    uri2.c_str());
		}

		command_error(client, ACK_ERROR_NO_EXIST, "No such file");
		return CommandResult::ERROR;
#endif
	} else {
		command_error(client, ACK_ERROR_NO_EXIST, "No such file");
		return CommandResult::ERROR;
	}
}

CommandResult
handle_read_song_cover(Client &client, ConstBuffer<const char *> args)
{
	assert(args.size == 1);
	const char *const uri = args.front();

	client_printf(client, "file: %s\n", uri);

	clear_cover_cnt();
	if (memcmp(uri, "file:///", 8) == 0) {
		/* read comments from arbitrary local file */
		const char *path_utf8 = uri + 7;
		AllocatedPath path_fs = AllocatedPath::FromUTF8(path_utf8);
		if (path_fs.IsNull()) {
			command_error(client, ACK_ERROR_NO_EXIST,
				      "unsupported file name");
			return CommandResult::ERROR;
		}

		Error error;
		if (!client.AllowFile(path_fs, error))
			return print_error(client, error);

		return read_file_covers(client, path_fs);
	} else if (uri_has_scheme(uri)) {
		return read_stream_covers(client, uri);
	} else if (!PathTraitsUTF8::IsAbsolute(uri)) {
#ifdef ENABLE_DATABASE
		const Storage *storage = client.GetStorage();
		if (storage == nullptr) {
#endif
			command_error(client, ACK_ERROR_NO_EXIST,
				      "No database");
			return CommandResult::ERROR;
#ifdef ENABLE_DATABASE
		}

		{
			AllocatedPath path_fs = storage->MapFS(uri);
			if (!path_fs.IsNull())
				return read_file_covers(client, path_fs);
		}

		{
			const std::string uri2 = storage->MapUTF8(uri);
			if (uri_has_scheme(uri2.c_str()))
				return read_stream_covers(client,
							    uri2.c_str());
		}

		command_error(client, ACK_ERROR_NO_EXIST, "No such file");
		return CommandResult::ERROR;
#endif
	} else {
		command_error(client, ACK_ERROR_NO_EXIST, "No such file");
		return CommandResult::ERROR;
	}
}

CommandResult
handle_read_folder_cover(Client &client, ConstBuffer<const char *> args)
{
	assert(args.size == 1);
	const char *const uri = args.front();

	client_printf(client, "folder: %s\n", uri);

	clear_cover_cnt();
	if (memcmp(uri, "file:///", 8) == 0) {
		/* read comments from arbitrary local file */
		const char *path_utf8 = uri + 7;
		AllocatedPath path_fs = AllocatedPath::FromUTF8(path_utf8);
		if (path_fs.IsNull()) {
			command_error(client, ACK_ERROR_NO_EXIST,
					  "unsupported file name");
			return CommandResult::ERROR;
		}

		Error error;
		if (!client.AllowFile(path_fs, error))
			return print_error(client, error);

		read_local_folder_covers(client, path_fs);
		return CommandResult::OK;
	} else if (uri_has_scheme(uri)) {
		read_stream_folder_covers(client, uri);
		return CommandResult::OK;
	} else if (!PathTraitsUTF8::IsAbsolute(uri)) {
#ifdef ENABLE_DATABASE
		const Storage *storage = client.GetStorage();
		if (storage == nullptr) {
#endif
			command_error(client, ACK_ERROR_NO_EXIST,
					  "No database");
			return CommandResult::ERROR;
#ifdef ENABLE_DATABASE
		}

		{
			AllocatedPath path_fs = storage->MapFS(uri);
			if (!path_fs.IsNull()) {
				read_local_folder_covers(client, path_fs);
				return CommandResult::OK;
			}
		}

		{
			const std::string uri2 = storage->MapUTF8(uri);
			if (uri_has_scheme(uri2.c_str()))
				read_stream_folder_covers(client,
								uri);
			return CommandResult::OK;
		}

		command_error(client, ACK_ERROR_NO_EXIST, "No such file");
		return CommandResult::ERROR;
#endif
	} else {
		command_error(client, ACK_ERROR_NO_EXIST, "No such file");
		return CommandResult::ERROR;
	}
}

CommandResult
handle_read_cover(Client &client, ConstBuffer<const char *> args)
{
	CoverOption	co;
	CommandResult ret;
	const char *const uri = args.front();

	if (StringStartsWith(uri, "https://api.tidalhifi.com") ||
		StringStartsWith(uri, "https://api.tidal.com") ||
		StringFind(uri, "caryaudio.vtuner.com") != nullptr) {
		client_printf(client, "file: %s\n", uri);
		return CommandResult::OK;
	}
	clear_cover_cnt();
	for (int i=0;i<CoverOption::COVER_MAX;i++) {
		switch (co.orders[i]) {
		case CoverOption::COVER_SONG:
			ret = handle_read_song_cover(client, args);
			if (ret != CommandResult::OK) {
				FormatDefault(domain, "no cover in the song");
				return ret;
			} else if (get_cover_cnt() > 0) {
				FormatDefault(domain, "find cover in the song");
				return CommandResult::OK;
			}
			break;
		case CoverOption::COVER_FOLDER:
		{
			if (uri_has_scheme(uri)) {
				continue;
			}
			const char *_argv[1];
			ConstBuffer<const char *> _args(_argv, 0);
			std::string str = args.front();
			size_t pos = str.rfind("/");
			if (pos == std::string::npos)
				break;
			str = str.substr(0, pos);
			if (str.empty())
				break;
			_argv[_args.size++] = str.c_str();
			ret = handle_read_folder_cover(client, _args);
			if (ret != CommandResult::OK) {
				FormatDefault(domain, "no cover in the folder");
				return ret;
			} else if (get_cover_cnt() > 0) {
				FormatDefault(domain, "find cover in the folder");
				return CommandResult::OK;
			}
		}
			break;
		default:
			break;
		}
	}

	return CommandResult::OK;
}

