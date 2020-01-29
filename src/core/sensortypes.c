/*
    Copyright (C) 2011-2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@egmail.com>

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
#include <config.h>
#endif

		
#include <errno.h>
#include <mmthread.h>
#include <stdlib.h>
#include <string.h>

#include "eegdev.h"

struct sensor_type {
	struct sensor_type* next;
	int stype;
	char name[];
};

static mm_thr_once_t stype_once = MM_THR_ONCE_INIT;
static mm_thr_mutex_t stype_lock;
static struct sensor_type first = {.next = NULL, .stype = -1};

static
int add_sensor_type(const char* sname, struct sensor_type* start)
{
	unsigned int len;
	struct sensor_type *newtype, *curr = start ? start : &first;

	len = strlen(sname);
	if (!len) {
		errno = EINVAL;
		return -1;
	}

	// Check sname is unknown while going to the end of the list 
	while (curr->next) {
		curr = curr->next;
		if (!strcmp(sname, curr->name))
			return curr->stype;
	}

	// Create a new element
	if (!(newtype = malloc(sizeof(*newtype)+len+1)))
		return -1;
	memcpy(newtype->name, sname, len+1);
	newtype->stype = curr->stype+1;
	newtype->next = NULL;
	curr->next = newtype;

	return newtype->stype;
}


static
void sensor_type_exit(void)
{
	struct sensor_type *next, *curr = first.next;

	while (curr) {
		next = curr->next;
		free(curr);
		curr = next;
	}

	mm_thr_mutex_deinit(&stype_lock);
}


static
void sensor_type_init(void)
{
	mm_thr_mutex_init(&stype_lock, 0);
	add_sensor_type("eeg", NULL);
	add_sensor_type("trigger", NULL);
	add_sensor_type("undefined", NULL);
	atexit(sensor_type_exit);
}


API_EXPORTED
int egd_sensor_type(const char* name)
{
	int stype = 0;
	struct sensor_type *curr = &first;

	if (!name)  {
		errno = EINVAL;
		return -1;
	}
	
	// Initialize the list and mutex once in a thread-safe way
	mm_thr_once(&stype_once, sensor_type_init);

	// Test if sname is known while going to the end of the list 
	while (curr->next) {
		curr = curr->next;
		if (!strcmp(name, curr->name))
			return curr->stype;
	}

	// If we reach here, the type has not been found. So try to add it
	mm_thr_mutex_lock(&stype_lock);
	stype = add_sensor_type(name, curr);
	mm_thr_mutex_unlock(&stype_lock);

	return stype;
}


API_EXPORTED
const char* egd_sensor_name(int stype)
{
	struct sensor_type *curr;

	// Initialize the list and mutex once in a thread-safe way
	mm_thr_once(&stype_once, sensor_type_init);

	for (curr = first.next; curr; curr = curr->next) {
		if (curr->stype == stype)
			return curr->name;
	}

	errno = EINVAL;
	return NULL;
}

