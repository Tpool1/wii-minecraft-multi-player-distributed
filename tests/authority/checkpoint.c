/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include "../../source/network/checkpoint_file.h"

static bool write_value(FILE *file, void *data) {
	const char *value = data;
	return fputs(value, file) >= 0 && strcmp(value, "partial") != 0;
}
static bool contains(const char *path, const char *value) {
	char text[50] = {0};
	FILE *file = fopen(path, "rb");
	if(!file) return false;
	size_t n = fread(text, 1, sizeof(text)-1, file);
	fclose(file);
	return n == strlen(value) && !memcmp(value, text, n);
}
static bool verify(const char *path) {
	return contains(path, "first") || contains(path, "second") || contains(path, "third");
}
static void require(bool ok, const char *reason) {
	if(!ok) { fprintf(stderr, "FAIL %s\n", reason); exit(1); }
}
int main(void) {
	FILE *f = fopen("level.dat", "wb");
	require(f != NULL, "initialize disposable level");
	require(fputs("first", f) >= 0 && fclose(f) == 0, "write original level");
	require(!checkpoint_file("level.dat", false, write_value, verify, "partial"), "reject failed serializer");
	require(contains("level.dat", "first"), "preserve current level after a partial write");
	require(!checkpoint_file("level.dat", false, write_value, verify, "corrupt"), "reject failed reopen validation");
	require(contains("level.dat", "first"), "preserve current level after verification failure");
	require(checkpoint_file("level.dat", false, write_value, verify, "second"), "commit next level");
	require(contains("level.dat", "second") && contains("level.dat" CHECKPOINT_PREVIOUS, "first"), "retain preceding level");
	require(remove("level.dat") == 0, "simulate missing primary after FAT rename");
	require(checkpoint_file("level.dat", true, write_value, verify, "third"), "recover previous level");
	require(contains("level.dat", "third") && contains("level.dat" CHECKPOINT_PREVIOUS, "first"), "preserve known-good fallback");
	puts("PASS failed/invalid checkpoint, FAT rename recovery, retained previous generation");
	return 0;
}
