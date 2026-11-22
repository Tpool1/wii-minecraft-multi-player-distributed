/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHECKPOINT_FILE_H
#define CHECKPOINT_FILE_H
#include <stdbool.h>
#include <stdio.h>
#define CHECKPOINT_PREVIOUS ".fcx-prev"
bool checkpoint_file(const char *path, bool recovered, bool (*write_data)(FILE *, void *),
					 bool (*verify)(const char *), void *data);
#endif
