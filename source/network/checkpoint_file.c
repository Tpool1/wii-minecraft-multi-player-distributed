/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "checkpoint_file.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool remove_if_present(const char *path) {
	return remove(path) == 0 || errno == ENOENT;
}

bool checkpoint_file(const char *path, bool recovered, bool (*write_data)(FILE *, void *),
					 bool (*verify)(const char *), void *data) {
	const char suffix[] = ".fcx-new";
	size_t n = strlen(path);
	char *temp = malloc(n + sizeof suffix);
	char *prev = malloc(n + sizeof CHECKPOINT_PREVIOUS);
	bool committed = false;
	if(temp && prev) {
		memcpy(temp, path, n); memcpy(temp + n, suffix, sizeof suffix);
		memcpy(prev, path, n); memcpy(prev + n, CHECKPOINT_PREVIOUS, sizeof CHECKPOINT_PREVIOUS);
		FILE *out = fopen(temp, "wb");
		if(out) {
			bool complete = write_data(out, data) && fflush(out) == 0 && !ferror(out);
			if(fclose(out) != 0) complete = false;
			if(complete && verify(temp)) {
				// FAT does not replace an existing name. Keep a readable generation
				// on disk at each rename, including recovery from a missing primary.
				bool use_previous = recovered || (!verify(path) && verify(prev));
				bool rotated = use_previous ? remove_if_present(path)
					: remove_if_present(prev) && rename(path, prev) == 0;
				if(rotated) committed = rename(temp, path) == 0;
			}
			if(!committed) remove(temp);
		}
	}
	free(prev);
	free(temp);
	return committed;
}
