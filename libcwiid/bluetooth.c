/* Copyright (C) 2007 L. Donnie Smith <donnie.smith@gatech.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "cwiid_internal.h"

/* When filtering wiimotes, in order to avoid having to store the
 * remote names before the blue_dev array is malloced (because we don't
 * yet know how many wiimotes there are, we'll assume there are no more
 * than dev_count, and realloc to the actual number afterwards, since
 * reallocing to a smaller chunk should be fast. */
#define BT_MAX_INQUIRY 128
/* timeout in 2 second units */
int cwiid_get_bdinfo_array(int dev_id, unsigned int timeout, int max_bdinfo,
                           struct cwiid_bdinfo **bdinfo, uint8_t flags)
{
	inquiry_info *dev_list = NULL;
	int max_inquiry;
	int dev_count;
	int sock = -1;
	int bdinfo_count;
	int i, j;
	int err = 0;
	int ret;

	/* NULLify for the benefit of error handling */
	*bdinfo = NULL;

	/* If not given (=-1), get the first available Bluetooth interface */
	if (dev_id == -1) {
      dev_id = hci_get_route( NULL );
      if (dev_id < 0) {
			cwiid_err(NULL, "No Bluetooth interface found");
			return -1;
		}
	}

	/* Open connection to Bluetooth Interface */
   sock = hci_open_dev( dev_id );
   if (sock < 0) {
		cwiid_err(NULL, "Bluetooth interface open error: %s", strerror(errno));
		err = 1;
		goto CODA;
	}

	/* Get Bluetooth Device List */
	if ((flags & BT_NO_WIIMOTE_FILTER) && (max_bdinfo != -1)) {
		max_inquiry = max_bdinfo;
	}
	else {
		max_inquiry = BT_MAX_INQUIRY;
	}
   dev_count = hci_inquiry( dev_id, timeout, max_inquiry, NULL, &dev_list, IREQ_CACHE_FLUSH );
   if (dev_count < 0) {
		cwiid_err(NULL, "Bluetooth device inquiry error: %s", strerror(errno));
		err = 1;
		goto CODA;
	}

	if (dev_count == 0) {
		bdinfo_count = 0;
		goto CODA;
	}

	/* Allocate info list */
	if (max_bdinfo == -1) {
		max_bdinfo = dev_count;
	}
   *bdinfo = malloc( max_bdinfo * sizeof(**bdinfo) );
   if (*bdinfo == NULL) {
		cwiid_err(NULL, "Memory allocation error (bdinfo array)");
		err = 1;
		goto CODA;
	}

	/* Copy dev_list to bdinfo */
	for (bdinfo_count=i=0; (i < dev_count) && (bdinfo_count < max_bdinfo); i++) {

		/* Filter by class */
		if (!(flags & BT_NO_WIIMOTE_FILTER) &&
		  ((dev_list[i].dev_class[0] != WIIMOTE_CLASS_0) ||
		   (dev_list[i].dev_class[1] != WIIMOTE_CLASS_1) ||
		   (dev_list[i].dev_class[2] != WIIMOTE_CLASS_2))) {
			continue;
		}

		/* timeout (10000) in milliseconds */
#if 0
		if (hci_read_remote_name(sock, &dev_list[i].bdaddr, BT_NAME_LEN,
		                         (*bdinfo)[bdinfo_count].name, 10000)) {
			cwiid_err(NULL, "Bluetooth name read error: %s", strerror(errno));
			err = 1;
			goto CODA;
		}

		/* Filter by name */
		if (!(flags & BT_NO_WIIMOTE_FILTER) &&
		  strncmp((*bdinfo)[bdinfo_count].name, WIIMOTE_NAME, BT_NAME_LEN) &&
		  strncmp((*bdinfo)[bdinfo_count].name, WIIBALANCE_NAME, BT_NAME_LEN)) {
			continue;
		}
#endif

		/* Passed filter, add to bdinfo */
		bacpy( &(*bdinfo)[bdinfo_count].bdaddr, &dev_list[i].bdaddr );
		for (j=0; j<3; j++) {
			(*bdinfo)[bdinfo_count].btclass[j] = dev_list[i].dev_class[j];
		}
		bdinfo_count++;
	}

	if (bdinfo_count == 0) {
		free(*bdinfo);
	}
	else if (bdinfo_count < max_bdinfo) {
      *bdinfo = realloc(*bdinfo, bdinfo_count * sizeof(**bdinfo) );
      if (*bdinfo == NULL) {
			cwiid_err(NULL, "Memory reallocation error (bdinfo array)");
			err = 1;
			goto CODA;
		}
	}

CODA:
	bt_free(dev_list);
	if (sock != -1)
      hci_close_dev(sock);
	if (err) {
		if (*bdinfo) free(*bdinfo);
		ret = -1;
	}
	else {
		ret = bdinfo_count;
	}
	return ret;
}

int cwiid_find_wiimote(bdaddr_t *bdaddr, int timeout)
{
	struct cwiid_bdinfo *bdinfo;
	int bdinfo_count;

	if (timeout == -1) {
		while ((bdinfo_count = cwiid_get_bdinfo_array(-1, 2, 1, &bdinfo, 0))
		       == 0);
		if (bdinfo_count == -1) {
			return -1;
		}
	}
	else {
		bdinfo_count = cwiid_get_bdinfo_array(-1, timeout, 1, &bdinfo, 0);
		if (bdinfo_count == -1) {
			return -1;
		}
		else if (bdinfo_count == 0) {
			cwiid_err(NULL, "No wiimotes found");
			return -1;
		}
	}

	bacpy( bdaddr, &bdinfo[0].bdaddr );
	free(bdinfo);
	return 0;
}
