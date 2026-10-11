/*
 * Copyright (c) 2023 Lain Bailey <lain@obsproject.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <glob.h>
#include <time.h>

#include "darray.h"
#include "dstr.h"
#include "platform.h"

bool os_sleepto_ns(uint64_t time_target)
{
	uint64_t current = os_gettime_ns();
	if (time_target < current)
		return false;

	time_target -= current;

	struct timespec req, remain;
	memset(&req, 0, sizeof(req));
	memset(&remain, 0, sizeof(remain));
	req.tv_sec = time_target / 1000000000;
	req.tv_nsec = time_target % 1000000000;

	while (nanosleep(&req, &remain)) {
		req = remain;
		memset(&remain, 0, sizeof(remain));
	}

	return true;
}

bool os_sleepto_ns_fast(uint64_t time_target)
{
	uint64_t current = os_gettime_ns();
	if (time_target < current)
		return false;

	do {
		uint64_t remain_us = (time_target - current + 999) / 1000;
		useconds_t us = remain_us >= 1000000 ? 999999 : remain_us;
		usleep(us);

		current = os_gettime_ns();
	} while (time_target > current);

	return true;
}

void os_sleep_ms(uint32_t duration)
{
	usleep(duration * 1000);
}

#if !defined(__APPLE__)

uint64_t os_gettime_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec);
}

#endif

struct os_dir {
	const char *path;
	DIR *dir;
	struct dirent *cur_dirent;
	struct os_dirent out;
};

os_dir_t *os_opendir(const char *path)
{
	struct os_dir *dir;
	DIR *dir_val;

	dir_val = opendir(path);
	if (!dir_val)
		return NULL;

	dir = bzalloc(sizeof(struct os_dir));
	dir->dir = dir_val;
	dir->path = path;
	return dir;
}

static inline bool is_dir(const char *path)
{
	struct stat stat_info;
	if (stat(path, &stat_info) == 0)
		return !!S_ISDIR(stat_info.st_mode);

	blog(LOG_DEBUG, "is_dir: stat for %s failed, errno: %d", path, errno);
	return false;
}

struct os_dirent *os_readdir(os_dir_t *dir)
{
	struct dstr file_path = {0};

	if (!dir)
		return NULL;

	dir->cur_dirent = readdir(dir->dir);
	if (!dir->cur_dirent)
		return NULL;

	const size_t length = strlen(dir->cur_dirent->d_name);
	if (sizeof(dir->out.d_name) <= length)
		return NULL;
	memcpy(dir->out.d_name, dir->cur_dirent->d_name, length + 1);

	dstr_copy(&file_path, dir->path);
	dstr_cat(&file_path, "/");
	dstr_cat(&file_path, dir->out.d_name);

	dir->out.directory = is_dir(file_path.array);

	dstr_free(&file_path);

	return &dir->out;
}

void os_closedir(os_dir_t *dir)
{
	if (dir) {
		closedir(dir->dir);
		bfree(dir);
	}
}

struct posix_glob_info {
	struct os_glob_info base;
	glob_t gl;
};

int os_glob(const char *pattern, int flags, os_glob_t **pglob)
{
	struct posix_glob_info pgi;
	int ret = glob(pattern, 0, NULL, &pgi.gl);

	if (ret == 0) {
		DARRAY(struct os_globent) list;
		da_init(list);

		for (size_t i = 0; i < pgi.gl.gl_pathc; i++) {
			struct os_globent ent = {0};

			ent.path = pgi.gl.gl_pathv[i];
			ent.directory = is_dir(ent.path);
			da_push_back(list, &ent);
		}

		pgi.base.gl_pathc = list.num;
		pgi.base.gl_pathv = list.array;

		*pglob = bmemdup(&pgi, sizeof(pgi));
	} else {
		*pglob = NULL;
	}

	UNUSED_PARAMETER(flags);
	return ret;
}

void os_globfree(os_glob_t *pglob)
{
	if (pglob) {
		struct posix_glob_info *pgi = (struct posix_glob_info *)pglob;
		globfree(&pgi->gl);

		bfree(pgi->base.gl_pathv);
		bfree(pgi);
	}
}
