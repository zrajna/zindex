/* zindex.h -- from the example of zlib/gzip stream indexing and random access
 * by Mark Adler: zran.c Version 1.0  29 May 2005 under copyright of zlib.h
 *
 *  Modified and extended: Feb 18, 2015
 *  For modifications: copyright 2015 Zalan Rajna under GNU GPLv3
 */

#ifndef ZRAN_H_
#define ZRAN_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "zlib.h"

#define SPAN 4194304L	/* desired distance between access points */
#define WINSIZE 32768U      /* sliding window size */
#define CHUNK 16384         /* file input buffer size */

/* access point entry */
struct idx_point {
    off_t out;          /* corresponding offset in uncompressed data */
    off_t in;           /* offset in input file of first full byte */
    int bits;           /* number of bits (1-7) from byte at in - 1, or 0 */
};

struct ucs_point {
    unsigned char window[WINSIZE];  /* preceding 32K of uncompressed data */
};

/* access point list */
struct access {
    size_t have;           /* number of list entries filled in */
    size_t size;           /* number of list entries allocated */
    struct idx_point *idx_list; /* allocated list */
    struct ucs_point *ucs_list; /* allocated list or NULL */
};

struct zindex{
	FILE * zFile;
	FILE * idxFile;
	FILE * ucsFile;
	struct access * data;
	off_t pos;
	off_t end;
};
typedef struct zindex * zindexPtr;

void free_index(struct access *index);

int build_index(FILE *in, off_t span, struct access **built);

int write_index(struct access *index, FILE *idxFile, FILE *ucsFile);

int read_index(FILE *idxFile, struct access **built);

zindexPtr ziopen_auto(const char *path, const char *mode);

zindexPtr ziopen(const char *zPath, const char *idxPath, const char *ucsPath, const char *mode);

zindexPtr zidopen(int zfd, int idxfd, int ucsfd, const char *mode);

int ziclose(zindexPtr * idx);

int ziread(zindexPtr idx, void* buf, unsigned len);

int ziwrite(zindexPtr idx, const void* buf, unsigned len);

long ziseek(zindexPtr idx, long offset, int whence);

int zirewind(zindexPtr idx);

long zitell(zindexPtr idx);

int ziputs(zindexPtr idx, const char *str);

char * zigets(zindexPtr idx, char* str, int size);

int ziflush(zindexPtr idx);

int zieof(zindexPtr idx);

int ziputc(zindexPtr idx, int c);

int zigetc(zindexPtr idx);

#if !defined(WIN32)
int ziprintf(zindexPtr idx, const char *format, ...);
#endif

#endif /* ZRAN_H_ */

