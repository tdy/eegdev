/*
    Copyright (C) 2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "configuration.h"

#define INCBUFSIZE	1024
#define INCNUMSETTINGS	32


LOCAL_FN
void egdi_free_config(struct egdi_config* cf)
{
	free(cf->settings);
	free(cf->buffer);
	cf->numsettings = cf->nmaxsettings = 0;
	cf->cursize = cf->maxsize = 0;
}


LOCAL_FN
void egdi_init_config(struct egdi_config* cf)
{
	cf->settings = NULL;
	cf->buffer = NULL;
	cf->numsettings = 0;
	cf->nmaxsettings = 0;
	cf->cursize = cf->maxsize = 0;
}


LOCAL_FN
void egdi_reinit_config(struct egdi_config* cf)
{
	cf->numsettings = 0;
	cf->cursize = 0;
}


LOCAL_FN
int egdi_add_setting(struct egdi_config* cf,
                     const char* name, const char* value)
{
	size_t nlen, vlen;
	unsigned int nmax;
	void* newbuff;
	struct setting* set;
	
	nlen = strlen(name) + 1;
	vlen = strlen(value) + 1;
	
	// Increase strings buffer size if too small for the new strings
	while (cf->cursize + nlen + vlen > cf->maxsize) {
		newbuff = realloc(cf->buffer, cf->maxsize + INCBUFSIZE);
		if (!newbuff)
			return -1;
		cf->maxsize += INCBUFSIZE;
		cf->buffer = newbuff;
	}

	// Increase the settings array size if too small
	if (cf->numsettings == cf->nmaxsettings) {
		nmax = cf->nmaxsettings + INCNUMSETTINGS;
		newbuff = realloc(cf->settings, nmax*sizeof(*cf->settings));
		if (!newbuff)
			return -1;
		cf->nmaxsettings += INCNUMSETTINGS;
		cf->settings = newbuff;
	}

	// Stack the settings and strings on their respective buffers
	set = cf->settings + cf->numsettings;
	set->c_offset = cf->cursize;
	set->v_offset = cf->cursize + nlen;
	strcpy(cf->buffer + set->c_offset, name);
	strcpy(cf->buffer + set->v_offset, value);
	cf->cursize += nlen + vlen;
	cf->numsettings++;

	return 0;
}


LOCAL_FN
const char* egdi_get_setting_value(struct egdi_config* cf, const char* name)
{
	unsigned int i=cf->numsettings;
	const char* buff = cf->buffer;

	// Search backward for the setting of the specified name: all prior
	// definitions of a setting are overriden by the latest definition
	while (i) {
		i--;
		if (!strcmp(buff+ cf->settings[i].c_offset, name))
			return buff + cf->settings[i].v_offset;
	}

	return NULL;
}
