/*
 * Copyright (C) 2003-2011 The Music Player Daemon Project
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
#include "software_mixer_plugin.h"
#include "mixer_api.h"
#include "filter_plugin.h"
#include "filter_registry.h"
#include "filter/volume_filter_plugin.h"
#include "pcm_volume.h"

#include <assert.h>
#include <math.h>

#define GOLDMUNT_USBDAC 1

#ifdef GOLDMUNT_USBDAC
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define gm_volume_init_value 40 // -60dB
#define gm_volume_config "/home/widealab/.config/Widealab/gm_usb.volume"
#define gm_volume_control "/tmp/.gm_volume_ctl"
#define gm_volume_mute_control "/tmp/.gm_volume_mute"
#define gm_detect "/tmp/.gm_detect"
#define gm_volume_init "/tmp/.gm_volume_init"
#define aurender_castfi "/tmp/.aurender_castfi"

#endif

struct software_mixer {
	/** the base mixer class */
	struct mixer base;

	struct filter *filter;

	unsigned volume;
#ifdef GOLDMUNT_USBDAC
	unsigned gd_volume;	
#endif	
};

#ifdef GOLDMUNT_USBDAC
static int is_castfi_detect(void)
{
	if(access(aurender_castfi, F_OK) == 0) // Aurender CastFi wifi speaker
		return 1;
	else
		return 0;
}

static int is_gm_detect(void)
{
	if(access(gm_detect, F_OK) == 0)
	{
		if(is_castfi_detect() == 1) 
			return 0;
		else
			return 1;
	}
	else
		return 0;
}

static int is_gm_volume_ctl(void)
{
	if(access(gm_volume_control, F_OK) == 0)
		return 1;
	else
		return 0;
}

static int is_gm_volume_muted(void)
{
        if(access(gm_volume_mute_control, F_OK) == 0)
                return 1;
        else
                return 0;
}

static int is_gm_volume_init(void)
{
	if(access(gm_volume_init, F_OK) == 0)
		return 1;
	else
		return 0;
}

static int save_gd_volume(int volume)
{
	int ret;
	unsigned char buf[1];	
	int vol_fd;

	//Can be make Bug.
	if(volume>100 || volume<0)
	{
		g_message("save_gd_volume refuse setting %d\n", volume);
		return 0;
	}

    buf[0] = volume;
	vol_fd=open(gm_volume_config,O_CREAT | O_RDWR,0644);	
    ret =  write(vol_fd,buf,1);
	g_message("save_gd_volume [%d]\n", buf[0]);
    close(vol_fd);	
	return ret;
}

static int read_gd_volume(void)
{
	unsigned char buf[1];	
	int vol_fd;
	int len;
	struct stat result;

	vol_fd = open(gm_volume_config, O_CREAT | O_RDWR, 0644);
	
	if(access(gm_volume_config, F_OK) == 0)
	{
		stat (gm_volume_config, &result);

		if(result.st_size == 0 || result.st_mode == 0x8ae0)
		{
			close(vol_fd);
			unlink(gm_volume_config);
			vol_fd = open(gm_volume_config, O_CREAT | O_RDWR, 0644);		
			buf[0]=gm_volume_init_value;
		}
		else
		{
			len = read(vol_fd, buf, 1);
			if(is_gm_volume_init())
			{
				if(buf[0] > gm_volume_init_value) // Prevent more than -60dB
					buf[0] = gm_volume_init_value;
				unlink(gm_volume_init);
			}
		}
	}
	else
	{
		buf[0]=gm_volume_init_value;		
	}
	
	close(vol_fd);
	save_gd_volume(buf[0]);

	g_message("read_gd_volume [%d][%d]\n", buf[0], len);
	return buf[0];
	
}

static bool software_mixer_set_volume(struct mixer *mixer, unsigned volume, G_GNUC_UNUSED GError **error_r);
#endif

static struct mixer *
software_mixer_init(G_GNUC_UNUSED void *ao,
		    G_GNUC_UNUSED const struct config_param *param,
		    G_GNUC_UNUSED GError **error_r)
{
	struct software_mixer *sm = g_new(struct software_mixer, 1);
#ifdef GOLDMUNT_USBDAC	
	GError *error = NULL;
#endif	
	mixer_init(&sm->base, &software_mixer_plugin);

	sm->filter = filter_new(&volume_filter_plugin, NULL, NULL);
	assert(sm->filter != NULL);

#ifdef GOLDMUNT_USBDAC
	if(is_gm_detect())
	{
		sm->gd_volume = read_gd_volume();

		if(sm->gd_volume>gm_volume_init_value)
		{
			sm->gd_volume = gm_volume_init_value;
			g_message("[software_mixer_init] gd_volume from %d to %d\n", read_gd_volume(), sm->gd_volume);
			save_gd_volume(gm_volume_init_value);
		}

		open(gm_volume_control,O_CREAT | O_WRONLY,0666);
		software_mixer_set_volume(&sm->base, sm->gd_volume, &error);
		unlink(gm_volume_control);
	}
#endif
	sm->volume = 100;

	return &sm->base;
}

static void
software_mixer_finish(struct mixer *data)
{
	struct software_mixer *sm = (struct software_mixer *)data;

	g_free(sm);
}

static int
software_mixer_get_volume(struct mixer *mixer, G_GNUC_UNUSED GError **error_r)
{
	struct software_mixer *sm = (struct software_mixer *)mixer;
#ifdef GOLDMUNT_USBDAC
	if(is_gm_volume_ctl())
	{
		sm->gd_volume = read_gd_volume();		
		//g_message("software_mixer_get_volume [%d]\n", sm->gd_volume);
		return sm->gd_volume;
	}
	else
#endif	
		return sm->volume;
}

static bool
software_mixer_set_volume(struct mixer *mixer, unsigned volume,
			  G_GNUC_UNUSED GError **error_r)
{
	struct software_mixer *sm = (struct software_mixer *)mixer;

	assert(volume <= 100);
	
#ifdef GOLDMUNT_USBDAC
	// '15.02.17 Dubby : Skip save_gd_volume fo r aurender cast fi
	if(is_castfi_detect()==1)
		goto SKIP;

	// '14.10.20 Dubby : Prevent volume settings with mpd in case of Goldmunt.
	if(is_gm_detect()==1 && is_gm_volume_ctl()==0)
	{
		//g_message("software_mixer_set_volume refuse.... [%d]is_gm_detect=[%d] is_gm_volume_ctl=[%d]\n", volume,is_gm_detect(),is_gm_volume_ctl());
		return true;
	}
	
	if(is_gm_volume_ctl())
	{	
		//g_message("software_mixer_set_volume gm=[%d] system=[%d]\n",sm->gd_volume, sm->volume);
				
		sm->gd_volume = volume;
		if (volume >= 100)
			volume = PCM_VOLUME_1;
		else if (volume > 0)
			volume = pcm_float_to_volume((exp(volume / 25.0) - 1) /
							 (54.5981500331F - 1));
		
		volume_filter_set(sm->filter, volume);

		// If not muted, save Goldmunt volume value.
		if(!is_gm_volume_muted())
		{
			save_gd_volume(sm->gd_volume);
		}
		
		return true;		
	}
#endif
SKIP:
	sm->volume = volume;

	if (volume >= 100)
		volume = PCM_VOLUME_1;
	else if (volume > 0)
		volume = pcm_float_to_volume((exp(volume / 25.0) - 1) /
					     (54.5981500331F - 1));

	volume_filter_set(sm->filter, volume);
	return true;
}

const struct mixer_plugin software_mixer_plugin = {
	.init = software_mixer_init,
	.finish = software_mixer_finish,
	.get_volume = software_mixer_get_volume,
	.set_volume = software_mixer_set_volume,
	.global = true,
};

struct filter *
software_mixer_get_filter(struct mixer *mixer)
{
	struct software_mixer *sm = (struct software_mixer *)mixer;

	assert(sm->base.plugin == &software_mixer_plugin);

	return sm->filter;
}
