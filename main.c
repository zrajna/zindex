/*
 * main.c -- from the example of zlib/gzip stream indexing and random access
 * Copyright (C) 2005 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 * Version 1.0  29 May 2005  Mark Adler
 *
 *  Modified and extended on: Feb 18, 2015
 *      Author: Zalan Rajna
 */

#include "zindex.h"

/* Create zindex index for input file. Default: .idx and .ucs extra files. */
int main(int argc, char **argv)
{
	int ret;
    long len;
    FILE *in;
    struct access *index;

    size_t argLen;
    char *idxExt;
	char *idxName;
    char *ucsExt;
    char *ucsName;

	FILE *idxFile;
	FILE *ucsFile;

    /* open input file */
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "usage: zindex file.gz [file.gz.idx file.gz.idx.ucs]\n");
        return 1;
    }
    if (argc==2) {
		argLen = strlen(argv[1]);
		idxExt = ".idx";
		idxName = (char *) calloc(argLen + strlen(idxExt) + 1, sizeof(char));
		if (idxName == NULL) {
			fprintf(stderr,"** ERROR: ziopen failed to alloc idxName\n");
			return 1;
		}
		strcpy(idxName, argv[1]);
		strcpy(idxName+argLen, idxExt);
		ucsExt = ".idx.ucs";
		ucsName = (char *) calloc(argLen + strlen(ucsExt) + 1, sizeof(char));
		if (ucsName == NULL) {
			free(idxName);
			fprintf(stderr,"** ERROR: ziopen failed to alloc ucsName\n");
			return 1;
		}
		strcpy(ucsName, argv[1]);
		strcpy(ucsName+argLen, ucsExt);
	}
    else {
    	idxName = argv[2];
    	ucsName = argv[3];
    }

    in = fopen(argv[1], "rb");
    if (in == NULL) {
        fprintf(stderr, "zindex: could not open %s for reading\n", argv[1]);
		goto return_fail;
    }
	idxFile = fopen(idxName, "wb");
	if (idxFile == NULL) {
		fclose(in);
		fprintf(stderr, "zindex: could not open %s for writing\n", idxName);
		goto return_fail;
	}
    ucsFile = fopen(ucsName, "wb");
    if (ucsFile == NULL) {
    	fclose(in);
    	fclose(idxFile);
    	fprintf(stderr, "zindex: could not open %s for writing\n", ucsName);
		goto return_fail;
    }
	fprintf(stdout,"Creating index files:\n\t%s\n\t%s\n", idxName, ucsName);
	if (argc == 2) {
		free(idxName);
		idxName = NULL;
		free(ucsName);
		ucsName = NULL;
	}

	/* build index */
	len = build_index(in, SPAN, &index);
	if (len <= 0) {
		fclose(in);
		fclose(idxFile);
		fclose(ucsFile);
		switch (len) {
		case Z_MEM_ERROR:
			fprintf(stderr, "zindex: out of memory\n");
			break;
		case Z_DATA_ERROR:
			fprintf(stderr, "zindex: compressed data error in %s\n", argv[1]);
			break;
		case Z_ERRNO:
			fprintf(stderr, "zindex: read error on %s\n", argv[1]);
			break;
		default:
			fprintf(stderr, "zindex: error %li while building index\n", len);
		}
		goto return_fail;
	}

	len = write_index(index, idxFile, ucsFile);
	ret = 0;
	if (len <= 0) {
		fprintf(stderr, "zindex: failed to write index files\n");
		ret = 1;
	}
	if (len < index->have) {
		fprintf(stderr, "zindex: writing index failed, only %li/%li written\n", len, index->have);
		ret = 1;
	}
	fprintf(stdout, "Index files created with %li access points\n", len);
	fclose(ucsFile);
	fclose(idxFile);
	fclose(in);

	free_index(index);
    return ret;

return_fail:
	if (argc == 2) {
		free(idxName);
		free(ucsName);
	}
	return 1;
}

