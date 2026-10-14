#pragma once

#include <stdio.h>
#include <stdlib.h>
#include "util/dstr.h"

static inline void copy_os_release_value(struct dstr *value, const char *start)
{
	size_t len = strcspn(start, "\r\n");
	if (len >= 2 && (start[0] == '"' || start[0] == '\'') && start[len - 1] == start[0]) {
		start++;
		len -= 2;
	}
	if (len)
		dstr_ncopy(value, start, len);
}

static inline void read_distribution_info(FILE *fp, struct dstr *distro, struct dstr *version)
{
	char *line = NULL;
	size_t linecap = 0;
	dstr_init_copy(distro, "Unknown");
	dstr_init(version);
	struct dstr build_id;
	dstr_init(&build_id);
	while (getline(&line, &linecap, fp) != -1) {
		if (!strncmp(line, "NAME=", 5))
			copy_os_release_value(distro, line + 5);
		else if (!strncmp(line, "VERSION_ID=", 11))
			copy_os_release_value(version, line + 11);
		else if (!strncmp(line, "BUILD_ID=", 9))
			copy_os_release_value(&build_id, line + 9);
	}
	if (!version->len) {
		if (build_id.len)
			dstr_copy_dstr(version, &build_id);
		else
			dstr_copy(version, strcmp(distro->array, "Unknown") ? "rolling" : "Unknown");
	}
	dstr_free(&build_id);
	free(line);
}
