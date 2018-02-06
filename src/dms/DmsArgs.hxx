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
 
#ifndef DMS_ARGS_HXX
#define DMS_ARGS_HXX

#include "AudioFormat.hxx"
#include "command/CommandError.hxx"
#include "client/Client.hxx"
#include "util/ConstBuffer.hxx"
#include "PlayerControl.hxx"
#include "tag/Tag.hxx"
#include "tag/TagBuilder.hxx"
#include <string.h>

#define DMS_VOLUME_MAX		255
#define DMS_VOLUME_MIN  	42
#define DMS_VOLUME_MAX_DB	8.0
#define DMS_VOLUME_MIN_DB	-98.5

#define volume_db_to_value(db)	(DMS_VOLUME_MIN + 2 * (db - DMS_VOLUME_MIN_DB))
#define volume_perventage_to_value(per) ((DMS_VOLUME_MAX - DMS_VOLUME_MIN)/100.0 * per + 0.5 + DMS_VOLUME_MIN)

#define DMS_BRIGHTNESS_STEP (100.0/255)

#define DMS_IO_ERROR				"dms io error"
#define DMS_CHECK_WRITE_BACK_ERROR	"dms check write back error"

class BufferedOutputStream;

typedef enum DMS_CHANNEL {
	CHANNEL_MASTER,
	CHANNEL_OPTICAL,
	CHANNEL_COAXIAL_1,
	CHANNEL_COAXIAL_2,
	CHANNEL_AESEBU,
	CHANNEL_BLUETOOTH,
	CHANNEL_XMOS,
	CHANNEL_MAX,
} channel_t;

typedef enum DMS_SOURCE {
	SOURCE_NONE,
// main
// usb
	SOURCE_SD,
	SOURCE_USB_1,
	SOURCE_USB_2,
	SOURCE_USB_3,
// network
	SOURCE_SMB,
	SOURCE_UPNP,
	SOURCE_PHONE,
// renderer
	SOURCE_DLNA,
	SOURCE_AIRPLAY,
	SOURCE_ROONREADY,

// internet
	SOURCE_INTERNET,

// source
	SOURCE_USB_SOURCE,
	SOURCE_NETWORK_SOURCE,
	SOURCE_INTERNET_SOURCE,

// external
	SOURCE_COAXIAL_1,
	SOURCE_COAXIAL_2,
	SOURCE_OPTICAL,
	SOURCE_AESEBU,
	SOURCE_BLUETOOTH,

	SOURCE_MAX,

// startup only
	SOURCE_NETWORK_LAST_USED,
	SOURCE_INTERNET_LAST_USED,
} source_t;

typedef enum DMS_RATE {
	RATE_BYPASS,
	RATE_44K1,
	RATE_MIN = RATE_44K1,
	RATE_48K,
	RATE_88K2,
	RATE_96K,
	RATE_176K4,
	RATE_192K,
	RATE_352K8,
	RATE_384K,
	RATE_705K6,
	RATE_768K,
	RATE_DSD64,
	RATE_DSD128,
	RATE_DSD256,
	RATE_MAX = RATE_DSD256,
	RATE_DSD512,
	RATE_NONE,
	RATE_UP,
} rate_t;

enum DMS_BLUETOOTH_STATE {
	BLUETOOTH_STATE_UNCONNECTED = RATE_352K8,
	BLUETOOTH_STATE_PAIRING_MODE,
	BLUETOOTH_STATE_CONNECTING,
	BLUETOOTH_STATE_CONNECTED,
};

enum DMS_VOLUME_POLICY {
	VOLUME_POLICY_MASTER,
	VOLUME_POLICY_INDEPENDENT,
};

enum DMS_IR {
	IR_FRONT,
	IR_REAR,
	IR_BOTH,
	IR_DISABLE,
};

enum DMS_NETWORK {
	NETWORK_ETHERNET,
	NETWORK_WIFI,
};

enum DMS_IP_ASSIGN {
	IP_ASSIGN_DHCP,
	IP_ASSIGN_STATIC,
};

enum DMS_ON_OFF_TOGGLE {
	DMS_OFF,
	DMS_ON,
	DMS_TOGGLE,
};

enum DMS_UP_DOWN {
	DMS_UP,
	DMS_DOWN,
};

enum DMS_BRIGHTNESS :int{
	BRIGHTNESS_OFF,
	BRIGHTNESS_LOW,
	BRIGHTNESS_MIDDLE,
	BRIGHTNESS_HIGH,
};

typedef struct dms_params_struct {
	unsigned char	source;
	unsigned char	rate;
	unsigned char	src;
	unsigned char	volume;
	unsigned char	mute;
	unsigned char	bypass;
} gcc_packed dms_params_t;

inline channel_t source_to_channel(enum DMS_SOURCE source)
{
	switch (source) {
	case SOURCE_SD:
	case SOURCE_USB_SOURCE:
	case SOURCE_USB_1:
	case SOURCE_USB_2:
	case SOURCE_USB_3:
	case SOURCE_SMB:
	case SOURCE_UPNP:
	case SOURCE_INTERNET_SOURCE:
		return CHANNEL_MASTER;
		
	case SOURCE_COAXIAL_1:
		return CHANNEL_COAXIAL_1;
	case SOURCE_COAXIAL_2:
		return CHANNEL_COAXIAL_2;
	case SOURCE_OPTICAL:
		return CHANNEL_OPTICAL;
	case SOURCE_AESEBU:
		return CHANNEL_AESEBU;
	case SOURCE_BLUETOOTH:
		return CHANNEL_BLUETOOTH;
	default:
		return CHANNEL_MASTER;
	}
}


template<typename T>
class DmsArg {
public:
	DmsArg(T t) : value(t) {}

	virtual bool operator==(const DmsArg &t) const {return value == t.value;}

	virtual bool operator!=(const DmsArg &t) const {return !(*this == t);}

	virtual DmsArg &operator=(const DmsArg &t) {
		value = t.value;
		return *this;
	}
		
	virtual std::string toString() const {return "";}

	virtual bool parse(gcc_unused const char *s) {return false;}

public:
	T	value;
};

class DmsUsb : public DmsArg<int> {
public:
	DmsUsb() : DmsArg(SOURCE_NONE), is_valid(false) {}

	DmsUsb &operator=(const DmsUsb &t) {
		value = t.value;
		nodes = t.nodes;
		paths = t.paths;
		ids = t.ids;
		is_valid = t.is_valid;
		return *this;
	}

	std::string toString() const;

	std::string getPath() const;

	bool valid() const {return is_valid;}

public:
	bool is_valid;

	std::vector<std::string> nodes;

	std::vector<std::string> paths;

	std::vector<std::string> ids;

	static const char *arg_name_tbl[];

	static const char *path_tbl[];
};

class DmsSource : public DmsArg<enum DMS_SOURCE>{
public:
	DmsSource() :DmsArg(SOURCE_NONE) {}

	virtual bool operator==(const DmsSource &t) const {
		return (value == t.value
			&& uri == t.uri
			&& source_name == t.source_name
			&& icon == t.icon);}

	virtual bool operator!=(const DmsSource &t) const {return !(*this == t);}

	virtual DmsSource &operator=(const DmsSource &t) {
		value = t.value;
		uri = t.uri;
		source_name = t.source_name;
		icon = t.icon;
		usb = t.usb;
		return *this;
	}

	virtual std::string toString() const;

	std::string validSources();

	std::string validNetworkUris();

	std::string validExternalUris();

	std::string validUsbUris();

	std::string validRendererUris();

	std::string validSourceScreenUris();

	std::string validInternetUris();

	std::string validLastUsedUris();

	std::string validStartups();

	bool parse(const char *s);

	bool parse(channel_t channel);

	bool parseUsb(const char *s);

	bool parseNetwork(const char *s);

	bool parseRenderer(const char *s);

	bool parseSourceScreen(const char *s);

	bool parseExternal(const char *s);

	bool parseInternet(const char *s);

	bool parseStartup(const char *s);

	bool isNone() const {return value == SOURCE_NONE;}

	bool isMaster() const;

	bool isUsb() const;

	bool isNetwork() const;

	bool isNetworkLastUsed() const {return value == SOURCE_NETWORK_LAST_USED;}

	bool isSamba() const {return value == SOURCE_SMB;}

	bool isUpnp() const {return value == SOURCE_UPNP;}

	bool isInternet() const { return value == SOURCE_INTERNET;}

	bool isTidal() const;

	bool isSpotify() const;

	bool isInternetLastUsed() const {return value == SOURCE_INTERNET_LAST_USED;}

	bool isSourceScreen() const;

	bool isExternal() const;

	bool isRenderer() const;

	bool isAirplay() const {return value == SOURCE_AIRPLAY;}

	bool isDLNA() const {return value == SOURCE_DLNA;}

	bool isRoonReady() const {return value == SOURCE_ROONREADY;}

	std::string getUpnpId() const {
		if (!isUpnp()) {
			return std::string();
		}
		std::string id = uri.substr(strlen("upnp://"));
		std::size_t found = id.find("/");
		if (found != std::string::npos) {
			id.resize(found);
		}
		return id;
	}

	inline bool isBluetooth() const { return value == SOURCE_BLUETOOTH;}

	channel_t toChannel() const;

	CommandResult mountUsb(Client &client, DmsUsb &_usb);

	CommandResult mountSamba(Client &client, ConstBuffer<const char *> _args);

	CommandResult mountNfs(Client &client, ConstBuffer<const char *> _args);

	std::string getQueuePath();

	std::string getPlaylistName(const char *playlist);
	
	bool isMounted(std::list<std::string> &list);

	bool checkMounted(Client &client);
	
	inline const std::string getUri() const {return uri;}

	inline void setUri(std::string u) {uri = u;}

	inline const std::string getName() const {return source_name;}

	inline void setName(std::string n) {source_name = n;}

	inline const std::string getIcon() const {return icon;}

	inline void setIcon(std::string v) {icon = v;}

	void reset();

public:
	std::string		uri;

	std::string		source_name;

	std::string		icon;

	DmsUsb			usb;
};

class DmsBluetooth {
public:
	enum STATE {
	PREPARE,
	PAIRING,
	CONNECTING,
	CONNECTED,
	};

	enum CODEC {
	CODEC_NONE = 0x00,
	SBC,
	AAC,
	APTX,
	LLAPTX,
	CODEC_END,
	};
		
	static const char *arg_state_tbl[];

	static const char *arg_codec_tbl[];
	
public:
	DmsBluetooth() : state(PAIRING), codec(CODEC_NONE) {
		status.audio_format.Clear();
		status.audio_format = AudioFormat(44100, SampleFormat::S16, 2);
		status.bit_rate = 0;
		status.bufferd_time = SignedSongTime::zero();
		status.elapsed_time = SongTime::zero();
		status.state = PlayerState::STOP;
		status.total_time = SignedSongTime::zero();
		tag.Clear();
	}

	bool operator==(const DmsBluetooth &t) const {
		return (state == t.state)
			&& (codec == t.codec)
			&& (id == t.id)
			&& (start_time_point == t.start_time_point);
	}

	bool operator!=(const DmsBluetooth &t) const {return !(*this == t);}

	DmsBluetooth &operator=(const DmsBluetooth &t) {
		state = t.state;
		codec = t.codec;
		status = t.status;
		id = t.id;
		start_time_point = t.start_time_point;
		TagBuilder tb(t.tag);
		tb.Commit(tag);
		return *this;
	}

	std::string stateString() const;

	std::string codecString() const;

	bool parse(enum STATE _state);

	bool parse(enum CODEC _codec);

	player_status getStatus();

public:
	enum STATE		state;

	enum CODEC		codec;

	player_status status;

	unsigned id = 1;

	unsigned start_time_point = 0;

	Tag tag;
};

class DmsVolumePolicy : public DmsArg<enum DMS_VOLUME_POLICY>{
public:
	static const char *arg_name_tbl[];
	
public:
	DmsVolumePolicy() : DmsArg (VOLUME_POLICY_MASTER) {}

	virtual std::string toString() const;

	std::string validArgs();

	virtual bool parse(const char *s);
};

class DmsIr : public DmsArg<enum DMS_IR>{
public:
	static const char *arg_name_tbl[];
	
public:
	DmsIr() :DmsArg(IR_FRONT) {}

	virtual std::string toString() const;

	std::string validArgs();

	virtual bool parse(const char *s);

	bool parse(bool front, bool rear);
};

class DmsNetWorkType : public DmsArg<enum DMS_NETWORK>{
public:
	static const char *arg_name_tbl[];
	
public:
	DmsNetWorkType() : DmsArg(NETWORK_ETHERNET) {}

	virtual std::string toString() const;

	std::string validArgs();

	virtual bool parse(const char *s);
};

class DmsIpAssign : public DmsArg<enum DMS_IP_ASSIGN>{
public:
	static const char *arg_name_tbl[];
	
public:
	DmsIpAssign() : DmsArg(IP_ASSIGN_DHCP) {}

	virtual std::string toString() const;

	std::string validArgs();

	virtual bool parse(const char *s);
};

class DmsBrightness : public DmsArg<unsigned char>{
public:
	static const char *arg_name_tbl[];
	
	static const unsigned char arg_value_tbl[];
	
public:
	DmsBrightness() : DmsArg(65*2.554) {}

	virtual std::string toString() const;

	std::string validArgs();

	virtual bool parse(const char *s);

	virtual bool parse(const char *s, const DmsBrightness &cur);

	bool parse(enum DMS_BRIGHTNESS level);

	bool parse(unsigned char bri);

	inline unsigned char toPercentage() { return (unsigned char)(DMS_BRIGHTNESS_STEP*value+0.5);}

	enum DMS_BRIGHTNESS toLevel(unsigned char v) const;

	inline enum DMS_BRIGHTNESS toLevel()const {return toLevel(value);}
};

class DmsRate : public DmsArg<enum DMS_RATE> {
public:
	static const char *arg_name_tbl[];
	
public:
	DmsRate() : DmsArg(RATE_NONE) {}

	virtual std::string toString() const;

	uint32_t toUint() const;

	std::string validArgs();
	
	bool parse(enum DMS_RATE rate);

	static DmsRate fromUint(unsigned int r);
};

class DmsSRC : public DmsArg<enum DMS_RATE> {
public:
	static const char *arg_name_tbl[];
	
public:
	DmsSRC() : DmsArg(RATE_BYPASS) {}

	virtual std::string toString() const;

	std::string validArgs();

	virtual bool parse(const char *s);

	virtual bool parse(const char *s, const DmsRate &cur_rate, const DmsSRC &cur_SRC);

	bool parse(enum DMS_RATE src);

	static DmsSRC fromUint(unsigned int s);

	void apply() const;

	void load(const char *uri);

	void store(const char *uri) const;
};

class DmsBool : public DmsArg<enum DMS_ON_OFF_TOGGLE> {
public:
	static const char *arg_name_tbl[];
	
public:
	DmsBool() :DmsArg(DMS_OFF) {}

	virtual inline std::string toString() const;

	inline bool toBool() {return value;}

	std::string validArgs();

	virtual bool parse(const char *s, const DmsBool &cur_bool);

	bool parse(bool on);
	
	virtual bool parse(const char *s);
};

void apply_volume(double vol);

double load_volume(const char *uri, double vol);

void store_volume(const char *uri, double vol);

class DmsVolume : public DmsArg<unsigned char> {
public:
	static const char *arg_name_tbl[];
	
public:
	DmsVolume() : DmsArg(volume_perventage_to_value(80))
		,muteAction(MUTE_ACTION_NONE) {}

	inline unsigned char getValue() {
		return (value >= DMS_VOLUME_MIN ? value : DMS_VOLUME_MIN);}
	
	virtual std::string toString() const;

	std::string validArgs();

	virtual bool parse(const char *s, const DmsVolume &cur_vol, bool cur_mute);

	bool parse(unsigned char vol);

	unsigned char toPercentage() const;

	void fromPercentage(unsigned char per);

	float toDb();
	
public:
	enum MUTE_ACTION {MUTE_ACTION_NONE, MUTE_ACTION_MUTE, MUTE_ACTION_UNMUTE};

	enum MUTE_ACTION muteAction;
};

class DmsPort : public DmsArg<int> {	
public:
	DmsPort() : DmsArg(6600) {}

	int toInt();

	std::string validArgs();

	virtual bool parse(const char *s);
};

class DmsIp : public DmsArg<std::string> {	
public:
	DmsIp() : DmsArg(std::string()){}

	virtual std::string toString() const;

	std::string validArgs();

	virtual bool parse(const char *s);
};

class DmsVersion : public DmsArg<std::string> {
public:
	DmsVersion() : DmsArg(std::string()) {}

	virtual std::string toString() const;

	virtual bool parse(const char *version);
};

class DmsUpdate : public DmsArg<int> {
public:
	enum DMS_UPDATE {
		UPDATING,
		ERROR,
		SUCCEED,
		UPDATE_END,
	};
		
	typedef enum DMS_UPDATE TYPE;
		
public:
	DmsUpdate() : DmsArg(UPDATE_END) {}

	virtual bool parse(unsigned char v);

	bool isSucceed() {return value == SUCCEED;}
};

class DmsPoweron : public DmsArg<int> {
public:
	enum DMS_POWERON {
		POWEROFF,
		POWERON,
		STARTUP,
		POWEROK,
		POWEROFFING,
		POWERON_END,
	};
		
	typedef enum DMS_POWERON TYPE;
		
public:
	DmsPoweron() : DmsArg(POWERON_END) {}

	std::string toString() const;

	virtual bool parse(unsigned char v);

	virtual bool parse(const char *s);
	
	std::string validArgs();

	bool isRunning() const {
		return (value == POWERON) || (value == STARTUP) || (value == POWEROK);
	}
	bool isPoweron() const {
		return (value == POWERON) || (value == STARTUP);
	}

	bool isPoweroff() const {
		return value == POWEROFF;
	}

	bool isPoweroffing() const {
		return value == POWEROFFING;
	}

public:	
	static const char *arg_name_tbl[];
};

class FolderCoverOption {
public:
	FolderCoverOption() {
		sl.push_back(std::string("front.jpg"));
		sl.push_back(std::string("cover.jpg"));
		sl.push_back(std::string("folder.jpg"));
		sl.push_back(std::string("back.jpg"));
	}
	bool operator==(const FolderCoverOption &t) const {
		if (sl.size() != t.sl.size())
			return false;
		for (size_t i=0;i<sl.size();i++) {
			if (sl[i] != t.sl[i])
				return false;
		}

		return true;
	}

	bool operator!=(const FolderCoverOption &t) const {return !(*this == t);}

	FolderCoverOption &operator=(const FolderCoverOption &t) {
		sl = t.sl;
		return *this;
	}

	std::vector<std::string> toStringList() const {
		return sl;
	}

	static std::string getType(std::string str) {
		if (str.find("front") != std::string::npos
			|| str.find("cover") != std::string::npos
			|| str.find("folder") != std::string::npos) {
			return std::string("Front cover");
		} else if (str.find("back") != std::string::npos) {
			return std::string("Back cover");
		} else {
			return std::string();
		}
	}

	static std::string getMime(std::string str) {
		std::size_t pos = str.find(".");
		if (pos != std::string::npos) {
			std::string mime = "image/";
			mime.append(str.substr(pos+1));
			return mime;
		} else {
			return std::string();
		}
	}

public:
	std::vector<std::string> sl;
};

class CoverOption {
public:
	enum COVER_ORDER {
		COVER_SONG,
		COVER_FOLDER,
		COVER_INTERNET,
		COVER_MAX,
	};
		
	typedef enum COVER_ORDER TYPE;

public:
	CoverOption();

	bool operator==(const CoverOption &t) const {
		for (int i=0;i<COVER_MAX;i++) {
			if (orders[i] != t.orders[i]) {
				return false;
			}
		}
		return true;
	}

	bool operator!=(const CoverOption &t) const {return !(*this == t);}

	CoverOption &operator=(const CoverOption &t) {
		memcpy(orders, t.orders, sizeof(orders));
		return *this;
	}

	std::string validArgs();

	std::vector<std::string> toOrderStringList() const;

	bool parse(const char *s, TYPE &type);

	bool parse(ConstBuffer<const char *> args);

	bool parseOrders(std::vector<std::string> ol);

public:
	static const char *arg_order_tbl[];

	TYPE orders[COVER_MAX];
};

class DSDType : public DmsArg<int> {
	typedef enum { DSD2PCM, DoP, NATIVE } dsdtype_t;

public:
	DSDType(dsdtype_t t = DSD2PCM) : DmsArg(t) {}

	std::string toString() const;

	bool parse(const char *s);
};

enum DMS_RATE get_master_rate(const AudioFormat af);

std::string master_rate_tostring(const AudioFormat af);

DmsRate get_current_rate(Client &client);

std::string current_rate_tostring(Client &client);

bool getAllMounts(Storage *_composite, std::list<std::string> &list);

void printAvailableSource(Client &client);

void printAvailableStartup(Client &client);

bool renameSourceName(Client &client, const char *s, const char *n);

bool renameSourceName(DmsSource s, const char *n);

void
dms_source_file_write(BufferedOutputStream &os);

#endif
