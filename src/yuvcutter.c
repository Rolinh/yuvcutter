/*
 * Copyright (c) 2013, Robin Hahling
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the author nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * yuvcutter.c
 *
 * Cut first N frames from a raw YUV video file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "yuvcutter.h"

#define _XOPEN_SOURCE 600

int
main(int argc, char *argv[])
{
	int ch;
	int helpflag = 0;
	int verboseflag = 0;
	int versionflag = 0;
	int countflag = 0;
	unsigned int width = 1920;
	unsigned int height = 1080;
	unsigned int nb_frames = 1;
	int yuv_mode = 420;
	const char *filename = "input.yuv";
	struct yuv_file *video;

	if (argv[1] && argv[1][0] != '-') {
		filename = argv[1];
	}

	while ((ch = getopt(argc, argv, "hvCF:H:M:N:VW:")) != -1) {
		switch(ch) {
		case 'h':
			helpflag = 1;
			break;
		case 'v':
			verboseflag = 1;
			break;
		case 'C':
			countflag = 1;
			break;
		case 'H':
			height = (unsigned int)strtoul(optarg, NULL, 10);
			break;
		case 'M':
			yuv_mode = (int)strtol(optarg, NULL, 10);
			break;
		case 'N':
			nb_frames = (unsigned int)strtoul(optarg, NULL, 10);
			break;
		case 'V':
			versionflag = 1;
			break;
		case 'W':
			width = (unsigned int)strtoul(optarg, NULL, 10);
			break;
		default:
			usage(EXIT_FAILURE);
			/* NOTREACHED */
		}
	}

	if (versionflag) {
		(void)printf("yuvcutter %s\n", VERSION);
		return EXIT_SUCCESS;
	}

	if (helpflag)
		usage(EXIT_SUCCESS);
		/* NOTREACHED */

	if (check_yuvfile(filename) == -1) {
		(void)fprintf(stderr, "Please, choose a YUV file\n");
		return EXIT_FAILURE;
	}

	if (!(yuv_mode == 420 || yuv_mode == 422 || yuv_mode == 444)) {
		(void)fprintf(stderr, "Please, choose either 420, 422 or 444 as"
			      " YUV mode: %d is not supported\n", yuv_mode);
		return EXIT_FAILURE;
	}

	if (!(video = count(filename, height, width, yuv_mode, nb_frames)))
		return EXIT_FAILURE;

	if (nb_frames >= video->frame_count) {
		(void)fprintf(stderr, "You cannot cut the same amount or more "
			      "frames than there actually is in the video.\n");
		return EXIT_FAILURE;
	}

	if (verboseflag)
		print_options(filename, height, width, nb_frames, yuv_mode,
			      countflag, video->frame_count);

	if (countflag) {
		(void)printf("Number of frames in %s: %u\n",
			     filename, video->frame_count);
	} else {
		if (cut(filename, height, width, nb_frames, yuv_mode, video)
				== -1)
			return EXIT_FAILURE;
	}

	free(video);

	return EXIT_SUCCESS;
}

struct yuv_file *
count(const char *filename, unsigned int height, unsigned int width,
    int yuv_mode, unsigned int nb_frames)
{
	struct yuv_file *ret;
	struct stat st;
	off_t frame_weight;
	off_t frame_size = (off_t)width * (off_t)height;

	if (!(ret = malloc(sizeof(struct yuv_file)))) {
		(void)fprintf(stderr, "Cannot allocate memory for yuv_file "
			      "structure\n");
		return NULL;
	}

	if (yuv_mode == 420)
		frame_weight = ((frame_size * 3) / 2);
	else if (yuv_mode == 422)
		frame_weight = (frame_size * 2);
	else if (yuv_mode == 444)
		frame_weight = (frame_size * 3);
	else {
		(void)fprintf(stderr, "Unsupported YUV mode: %d\n", yuv_mode);
		return NULL;
	}

	if (stat(filename, &st) == -1) {
		(void)fprintf(stderr, "Cannot stat %s: ", filename);
		perror("");
		return NULL;
	}

	ret->frame_count = (unsigned int)st.st_size / (unsigned int)frame_weight;
	ret->size_origin = st.st_size;
	ret->size_new = ret->size_origin - ((off_t)nb_frames * frame_weight);

	return ret;
}

int
cut(const char *filename, unsigned int height, unsigned int width,
    unsigned int nb_frames, int yuv_mode, struct yuv_file *video)
{
	FILE *yuvfile;
	FILE *fout;
	unsigned int offset;
	int ret = 0;
	size_t i;
	size_t j = 0;
	double perct;
	unsigned char buf[BUFSIZ];
	unsigned int frame_size = width * height;

	if (yuv_mode == 420)
		offset = ((frame_size * 3) / 2) * nb_frames;
	else if (yuv_mode == 422)
		offset = (frame_size * 2) * nb_frames;
	else if (yuv_mode == 444)
		offset = (frame_size * 3) * nb_frames;
	else {
		(void)fprintf(stderr, "Unsupported YUV mode: %d\n", yuv_mode);
		return -1;
	}

	if ((yuvfile = fopen(filename, "r")) == NULL) {
		(void)fprintf(stderr, "Cannot open file %s for reading: ",
			      filename);
		perror("");
		return -1;
	}
	if ((fout = fopen("cut.yuv", "w")) == NULL) {
		(void)fprintf(stderr, "Cannot open file cut.yuv for writing: ");
		perror("");
		return -1;
	}

	if (fseek(yuvfile, (long)offset, SEEK_SET) == -1) {
		(void)fprintf(stderr, "Cannot seek to position %ld: ",
			      (long)offset);
		perror("");
		ret = -1;
		goto close_fd;
	}

	(void)printf("Please, wait while writing to file...\n");
	/* hide cursor */
	(void)printf("\033[?25l");
	while (!feof(yuvfile)) {
		i = fread(buf, 1, BUFSIZ, yuvfile);
		if (fwrite(buf, 1, i, fout) != i) {
			(void)fprintf(stderr, "Cannot write to output file\n");
			ret = -1;
			goto close_fd;
		}
		j += i;
		perct = (double)j / (double)video->size_new * 100.0;
		(void)printf("\r%.f%% ", perct);
		disp_perct_bar(perct);
		(void)printf(" (%lu)", j);
	}
	(void)printf("\nDone writing to ./cut.yuv\n");
	/* show the cursor again */
	(void)printf("\033[?25h");

close_fd:
	if (fclose(yuvfile) == EOF) {
		(void)fprintf(stderr, "Cannot close %s file: ", filename);
		perror("");
		ret = -1;
	}
	if (fclose(fout) == EOF) {
		perror("Cannot close cut.yuv file");
		ret = -1;
	}

	return ret;
}

int
check_yuvfile(const char *filename)
{
	char *ext;

	ext = strrchr(filename, '.');

	if (!ext || (strcasecmp(filename, ext) == 0))
		return -1;

	if (strcasecmp(ext + 1, "yuv") == 0)
		return 0;

	return -1;
}

void
print_options(const char *filename, unsigned int height, unsigned int width,
    unsigned int nb_frames, int yuv_mode, int countflag,
    unsigned int frame_count)
{

	(void)printf("           Input file name: %s\n", filename);
	(void)printf("                    Height: %u\n", height);
	(void)printf("                     Width: %u\n", width);
	(void)printf("                  YUV mode: %u\n", yuv_mode);
	(void)printf("          Number of frames: %u\n", frame_count);
	if (!countflag) {
		(void)printf("        # of frames to cut: %u\n", nb_frames);
		(void)printf("Resulting number of frames: %u\n\n",
			     frame_count - nb_frames);
	}
}

void
disp_perct_bar(double perct)
{
	int i, j;
	int barinc = 5;

	(void)putchar('[');
	for (i = 0; i < perct; i += barinc)
		(void)putchar('#');
	for (j = i; j < 100; j += barinc)
		(void)putchar(' ');
	(void)putchar(']');
}

void
usage(int status)
{
	if (status != 0)
		(void)fprintf(stderr, "Try yuvcutter -h for more info\n");
	else {
		(void)printf("Usage:\nyuvcutter [input.yuv] [OPTION(S)] [-C] "
			     "[-H HEIGHT] [-M YUV_MODE] [-N NB_FRAMES] "
			     "[-W WIDTH]\n\n"
			"Common options:\n"
			"\t-h\t\tShow this help and exit\n"
			"\t-v\t\tActivate verbose mode\n"
			"\t-V\t\tShow program version and exit\n\n"
			"Specific options:\n"
			"\t-C\t\tCount the number of frames in the input file\n"
			"\t-H HEIGHT\tSpecify video height\n"
			"\t-M YUV_MODE\tSpecify YUV mode (420, 422 or 444)\n"
			"\t-N NB_FRAMES\tSpecify the number of frames to cut\n"
			"\t-W WIDTH\tSpecify video width\n\n"
			"Read yuvcutter manual page for more details\n");
	}

	exit(status);
	/* NOTREACHED */
}

