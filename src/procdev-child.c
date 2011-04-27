/*
    Copyright (C) 2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <pthread.h>

#include "eegdev-common.h"
#include "eegdev-procdev.h"

static pthread_mutex_t outlock = PTHREAD_MUTEX_INITIALIZER;

static
int return_parent(int call, int retval, const void* buf, size_t count)
{
	int ret = 0;
	ssize_t rsize;
	int32_t com[2] = {call, retval};

	pthread_mutex_lock(&outlock);

	rsize = write(PIPOUT, &com , sizeof(com));
	if (rsize <= 0) {
		if (rsize == 0)
			errno = EPIPE;
		ret = -1;
	}
	if ( (!ret && buf && !retval && count && 
	              (write(PIPOUT, buf, count) < 0)) )
		ret = -1;

	pthread_mutex_unlock(&outlock);
	
	return ret;
}


static
int set_channel_groups(struct eegdev* dev, size_t argsize)
{
	unsigned int ngrp = argsize / sizeof(struct grpconf);
	struct grpconf grp[ngrp];
	int retval = 0;
	size_t selchsize = ngrp*sizeof(*(dev->selch));
	void* selch;
	ssize_t rsize;
	
	// Get argument supplied by user to the API
	rsize = read(PIPIN, grp, argsize);
	if (rsize < (ssize_t)argsize) {
		retval = (rsize >= 0) ? ECHILD : errno;
		goto exit;
	}

	if (dev->selch)
		free(dev->selch);
	if (!(dev->selch = malloc(selchsize))) {
		retval = errno;
		goto exit;
	}

	if (dev->ops.set_channel_groups(dev, ngrp, grp)) {
		selchsize = 0;
		selch = NULL;
		retval = errno;
	}

exit:
	return return_parent(PROCDEV_SET_CHANNEL_GROUPS, retval,
	                     dev->selch, selchsize);
}


static
void safe_strncpy(char* dst, const char* src, size_t n)
{
	const char* strsrc = (src != NULL) ? src : "";
	size_t eos = strlen(strsrc);

	if (eos >= n)
		eos = n-1;
	
	memcpy(dst, src, eos);
	dst[eos] = '\0';
}


static
int fill_chinfo(struct eegdev* dev)
{
	int32_t	arg[2]; // {stype, ich}
	struct egd_chinfo info;
	struct egd_procdev_chinfo chinfo;
	int retval = 0;

	// Read options from pipe and execute the device method
	if (read(PIPIN, arg, sizeof(arg)) < 0) {
		retval = errno;
		goto exit;
	}
	dev->ops.fill_chinfo(dev, arg[0], arg[1], &info);

	// Copy the channel information to be sent back to the parent
	safe_strncpy(chinfo.label, info.label, sizeof(chinfo.label));
	safe_strncpy(chinfo.unit, info.unit, sizeof(chinfo.unit));
	safe_strncpy(chinfo.transducter, info.transducter,
	                                     sizeof(chinfo.transducter));
	safe_strncpy(chinfo.prefiltering, info.prefiltering,
	                                    sizeof(chinfo.prefiltering));
	chinfo.isint = info.isint;
	chinfo.dtype = info.dtype;
	chinfo.min = info.min;
	chinfo.max = info.max;

exit:
	return return_parent(PROCDEV_FILL_CHINFO, retval, 
	                     &chinfo, sizeof(chinfo));
}


LOCAL_FN
int egd_init_eegdev(struct eegdev* dev, const struct eegdev_operations* ops)
{	
	memset(dev, 0, sizeof(*dev));
	memcpy((void*)&(dev->ops), ops, sizeof(*ops));

	return 0;
}


LOCAL_FN
void egd_destroy_eegdev(struct eegdev* dev)
{	
	free(dev->selch);
}


LOCAL_FN
int egd_update_ringbuffer(struct eegdev* dev, const void* in, size_t length)
{
	ssize_t wsiz;
	const char* inbuff = in;
	
	while (length) {
		wsiz = write(PIPDATA, inbuff, length);
		if (wsiz < 0) {
			egd_report_error(dev, errno);
			return -1;
		}
		length -= wsiz;
		inbuff += wsiz;
	}
	return 0;
}


LOCAL_FN
void egd_report_error(struct eegdev* dev, int error)
{
	int32_t com[2] = {PROCDEV_REPORT_ERROR, error};

	pthread_mutex_lock(&outlock);
	if (write(PIPOUT, com, sizeof(com)))
		dev->error = errno;
	pthread_mutex_unlock(&outlock);
}


LOCAL_FN
void egd_update_capabilities(struct eegdev* dev)
{
	int i;
	int32_t com[2] = {PROCDEV_UPDATE_CAPABILITIES, 0};
	struct egd_procdev_caps caps = {
		.sampling_freq = dev->cap.sampling_freq,
		.devtype_len = strlen(dev->cap.device_type)+1,
		.devid_len = strlen(dev->cap.device_id)+1
	};
	for (i=0; i<EGD_NUM_STYPE; i++)
		caps.type_nch[i] = dev->cap.type_nch[i];

	pthread_mutex_lock(&outlock);
	if ( (write(PIPOUT, com, sizeof(com)) < 0)
	  || (write(PIPOUT, &caps, sizeof(caps)) < 0)
	  || (write(PIPOUT, dev->cap.device_type, caps.devtype_len) < 0)
	  || (write(PIPOUT, dev->cap.device_id, caps.devid_len) < 0) )
	  	dev->error = errno;
	pthread_mutex_unlock(&outlock);
}


LOCAL_FN
void egd_set_input_samlen(struct eegdev* dev, unsigned int samlen)
{
	int32_t com[2] = {PROCDEV_SET_SAMLEN, samlen};
	dev->in_samlen = samlen;

	pthread_mutex_lock(&outlock);
	if (write(PIPOUT, com, sizeof(com)))
		dev->error = errno;
	pthread_mutex_unlock(&outlock);
}


LOCAL_FN
int run_eegdev_process(eegdev_open_proc open_fn, int argc, char* argv[])
{
	(void)argc;
	int ret;
	struct eegdev* dev;
	int32_t com[2];
	struct opendev_options options = {
		.path = argv[1],
		.numch = atoi(argv[2])
	};

	// Open the device and send acknowledgement to parent
	dev = open_fn(&options);
	ret = return_parent(PROCDEV_CREATION_ENDED,
	                    dev ? 0 : errno, NULL, 0); 
	if (ret || (dev == NULL))
		return EXIT_FAILURE;

	for (;;) {
		if (read(PIPIN, &com, sizeof(com)) <= 0)
			return EXIT_FAILURE;
			
		if (com[0] == PROCDEV_CLOSE_DEVICE) {
			ret = dev->ops.close_device(dev);
			return_parent(com[0], ret ? errno : 0, NULL, 0); 
			break;
		} else if (com[0] == PROCDEV_SET_CHANNEL_GROUPS)
			set_channel_groups(dev, com[1]);
		else if (com[0] == PROCDEV_START_ACQ) {
			ret = dev->ops.start_acq(dev);
			return_parent(com[0], ret ? errno : 0, NULL, 0); 
		} else if (com[0] == PROCDEV_STOP_ACQ) {
			dev->ops.stop_acq(dev);
			return_parent(com[0], 0, NULL, 0); 
		} else if (com[0] == PROCDEV_FILL_CHINFO)
			fill_chinfo(dev);
	}
	
	return EXIT_SUCCESS;
}