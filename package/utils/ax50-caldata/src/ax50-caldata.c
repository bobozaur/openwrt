// SPDX-License-Identifier: GPL-2.0-only
/* Factory MTD contains gzip(tar(wlanconfig.gz)), then another gzip/tar with
 * radio calibration. Validate both complete archives and gzip CRCs before
 * emitting a requested member. Never extract paths or accept fallback data.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define PARTITION_SIZE (1024 * 1024)
#define ARCHIVE_LIMIT (2 * 1024 * 1024)
#define CALDATA_LIMIT 65536
#define TAR_BLOCK 512

struct span {
	const unsigned char *data;
	size_t size;
};

static bool zeroes(const unsigned char *p, size_t size)
{
	while (size--)
		if (*p++)
			return false;
	return true;
}

static bool octal(const unsigned char *p, size_t size, size_t *value)
{
	bool end = false, digit = false;
	size_t n = 0;

	for (size_t i = 0; i < size; i++) {
		if (p[i] == 0 || p[i] == ' ') {
			if (digit || p[i] == 0)
				end = true;
			continue;
		}
		if (end || p[i] < '0' || p[i] > '7' || n > (SIZE_MAX - 7) / 8)
			return false;
		n = n * 8 + p[i] - '0';
		digit = true;
	}
	*value = n;
	return digit;
}

static bool gunzip(struct span input, unsigned char *output, struct span *result,
		   bool partition_padding)
{
	z_stream stream = {
		.next_in = (unsigned char *)input.data,
		.avail_in = input.size,
		.next_out = output,
		.avail_out = ARCHIVE_LIMIT,
	};
	int status;

	if (inflateInit2(&stream, 15 + 16) != Z_OK)
		return false;
	status = inflate(&stream, Z_FINISH);
	result->data = output;
	result->size = stream.total_out;
	input.data = stream.next_in;
	input.size = stream.avail_in;
	inflateEnd(&stream);
	if (status != Z_STREAM_END || !result->size)
		return false;
	if (!partition_padding)
		return input.size == 0;
	/* The remaining erased/padded partition bytes are not another stream. */
	for (size_t i = 0; i < input.size; i++)
		if (input.data[i] != 0 && input.data[i] != 0xff)
			return false;
	return true;
}

static int calibration_index(const char *name)
{
	static const char *const names[] = {
		"cal_wlan0.bin", "cal_wlan2.bin", "cal_wlan4.bin"
	};
	for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (!strcmp(name, names[i]))
			return i;
	return -1;
}

static bool tar_member(struct span archive, const char *requested, bool outer,
		       struct span *result)
{
	size_t pos = 0;
	unsigned int seen = 0;
	bool found = false;

	while (archive.size - pos >= TAR_BLOCK) {
		const unsigned char *header = archive.data + pos;
		const char *name = (const char *)header;
		size_t size, sum = 0, expected, padded;
		int index;

		if (zeroes(header, TAR_BLOCK))
			return archive.size - pos >= 2 * TAR_BLOCK &&
				zeroes(header, archive.size - pos) && found;
		/* Neither links, extensions nor prefixed paths occur in this format. */
		if (!memchr(name, 0, 100) || header[345] ||
		    (header[156] != 0 && header[156] != '0') ||
		    !octal(header + 124, 12, &size) ||
		    !octal(header + 148, 8, &expected))
			return false;
		for (size_t i = 0; i < TAR_BLOCK; i++)
			sum += i >= 148 && i < 156 ? ' ' : header[i];
		if (sum != expected || !size || size > (outer ? ARCHIVE_LIMIT : CALDATA_LIMIT))
			return false;
		if (!outer && !strncmp(name, "./", 2))
			name += 2;
		index = outer ? (!strcmp(name, "wlanconfig.gz") ? 0 : -1) : calibration_index(name);
		if (index < 0 || (seen & (1U << index)))
			return false;
		seen |= 1U << index;
		padded = (size + TAR_BLOCK - 1) & ~(TAR_BLOCK - 1);
		pos += TAR_BLOCK;
		if (padded > archive.size - pos)
			return false;
		if (!strcmp(name, requested)) {
			result->data = archive.data + pos;
			result->size = size;
			found = true;
		}
		pos += padded;
	}
	return false;
}

int main(int argc, char **argv)
{
	unsigned char *partition = NULL, *outer = NULL, *inner = NULL;
	struct span outer_tar, inner_gzip, inner_tar, calibration;
	FILE *source = NULL;
	int result = EXIT_FAILURE;

	if (argc != 3 || calibration_index(argv[2]) < 0) {
		fprintf(stderr, "Usage: ax50-caldata <calibration MTD> <cal_wlan0|2|4.bin>\n");
		return EXIT_FAILURE;
	}
	partition = malloc(PARTITION_SIZE);
	outer = malloc(ARCHIVE_LIMIT);
	inner = malloc(ARCHIVE_LIMIT);
	if (!partition || !outer || !inner || !(source = fopen(argv[1], "rb")))
		goto out;
	if (fread(partition, 1, PARTITION_SIZE, source) != PARTITION_SIZE ||
	    ferror(source) || fgetc(source) != EOF || ferror(source))
		goto out;
	if (!gunzip((struct span){partition, PARTITION_SIZE}, outer, &outer_tar, true) ||
	    !tar_member(outer_tar, "wlanconfig.gz", true, &inner_gzip) ||
	    !gunzip(inner_gzip, inner, &inner_tar, false) ||
	    !tar_member(inner_tar, argv[2], false, &calibration))
		goto out;
	if (fwrite(calibration.data, 1, calibration.size, stdout) == calibration.size &&
	    !fflush(stdout))
		result = EXIT_SUCCESS;
out:
	if (source)
		fclose(source);
	free(inner);
	free(outer);
	free(partition);
	if (result != EXIT_SUCCESS)
		fprintf(stderr, "Cannot read validated AX50 calibration\n");
	return result;
}
