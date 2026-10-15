// SPDX-License-Identifier: GPL-2.0-only
/* The protected GPHY MTD holds an LZMA-compressed uImage inside a multi-image.
 * Validate both complete images before emitting the 64-KiB executable body.
 */
#include <lzma.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define PARTITION_SIZE 0x40000
#define HEADER_SIZE 64
#define FIRMWARE_SIZE 0x10000
#define DECODED_SIZE (HEADER_SIZE + FIRMWARE_SIZE + 64)
#define MEMORY_LIMIT (16 * 1024 * 1024)

static uint32_t be32(const unsigned char *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
	       (uint32_t)p[2] << 8 | p[3];
}

static bool image(const unsigned char *data, size_t length, unsigned int type,
		  unsigned int compression, const char *name, size_t *size)
{
	unsigned char header[HEADER_SIZE], image_name[32] = { 0 };

	if (length < HEADER_SIZE || be32(data) != 0x27051956)
		return false;
	*size = be32(data + 12);
	if (*size > length - HEADER_SIZE || data[30] != type ||
	    data[31] != compression || strlen(name) >= sizeof(image_name))
		return false;
	memcpy(image_name, name, strlen(name));
	if (memcmp(data + 32, image_name, sizeof(image_name)))
		return false;
	memcpy(header, data, sizeof(header));
	memset(header + 4, 0, 4);
	return crc32(0, header, sizeof(header)) == be32(data + 4) &&
	       crc32(0, data + HEADER_SIZE, *size) == be32(data + 24);
}

static bool decode(const unsigned char *partition, unsigned char *output)
{
	lzma_stream stream = LZMA_STREAM_INIT;
	size_t size;
	lzma_ret status;
	bool valid;

	if (!image(partition, PARTITION_SIZE, 4, 3, "GPHY Firmware", &size) ||
	    size < 8 || be32(partition + HEADER_SIZE) != size - 8 ||
	    be32(partition + HEADER_SIZE + 4) != 0)
		return false;
	/* The unused flash tail is outside the checked uImage payload. */
	stream.next_in = partition + HEADER_SIZE + 8;
	stream.avail_in = size - 8;
	stream.next_out = output;
	stream.avail_out = DECODED_SIZE;
	if (lzma_alone_decoder(&stream, MEMORY_LIMIT) != LZMA_OK)
		return false;
	status = lzma_code(&stream, LZMA_FINISH);
	valid = status == LZMA_STREAM_END && stream.avail_in == 0 &&
		stream.total_out == DECODED_SIZE;
	lzma_end(&stream);
	if (!valid || !image(output, DECODED_SIZE, 5, 0, "GRX500 V1.1 GPHY BE", &size) ||
	    size != FIRMWARE_SIZE)
		return false;
	for (size_t i = HEADER_SIZE + size; i < DECODED_SIZE; i++)
		if (output[i])
			return false;
	return true;
}

int main(int argc, char **argv)
{
	unsigned char *partition = NULL, *output = NULL;
	FILE *input = NULL;
	int result = EXIT_FAILURE;

	if (argc != 2) {
		fprintf(stderr, "usage: ax50-gphy GPHY_MTD\n");
		return EXIT_FAILURE;
	}
	partition = malloc(PARTITION_SIZE);
	output = malloc(DECODED_SIZE);
	input = fopen(argv[1], "rb");
	if (!partition || !output || !input)
		goto out;
	if (fread(partition, 1, PARTITION_SIZE, input) != PARTITION_SIZE ||
	    fgetc(input) != EOF || ferror(input) || !decode(partition, output))
		goto out;
	if (fwrite(output + HEADER_SIZE, 1, FIRMWARE_SIZE, stdout) != FIRMWARE_SIZE ||
	    fflush(stdout))
		goto out;
	result = EXIT_SUCCESS;
out:
	if (input)
		fclose(input);
	free(output);
	free(partition);
	if (result != EXIT_SUCCESS)
		fprintf(stderr, "Invalid or unavailable AX50 GPHY firmware\n");
	return result;
}
