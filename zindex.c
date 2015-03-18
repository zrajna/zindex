/* zindex.c -- example of zlib/gzip stream indexing and random access
 * Copyright (C) 2005 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 * Version 1.0  29 May 2005  Mark Adler
 *
 * Modified and extended on: Feb 18, 2015
 * 	   Author: Zalan Rajna
 */

#include "zindex.h"

#define local static

/* Add an entry to the access point list.  If out of memory, deallocate the
   existing list and return NULL. */
local struct access *addpoint(struct access *index, int bits,
    off_t in, off_t out, unsigned left, unsigned char *window)
{
    struct idx_point *idxNext;
    struct ucs_point *ucsNext;

    /* if list is empty, create it (start with eight points) */
    if (index == NULL) {
        index = malloc(sizeof(struct access));
        if (index == NULL) return NULL;
        index->idx_list = malloc(sizeof(struct idx_point) << 3);
        if (index->idx_list == NULL) {
            free(index);
            return NULL;
        }
        if (window != NULL)
        {
			index->ucs_list = malloc(sizeof(struct ucs_point) << 3);
			if (index->ucs_list == NULL) {
				free(index->idx_list);
				free(index);
				return NULL;
			}
        }
        else
        	index->ucs_list = NULL;
        index->size = 8;
        index->have = 0;
    }

    /* if list is full, make it bigger */
    else if (index->have == index->size) {
       index->size <<= 1;
       idxNext = realloc(index->idx_list, sizeof(struct idx_point) * index->size);
        if (idxNext == NULL) {
            free_index(index);
            return NULL;
        }
        index->idx_list = idxNext;
        if (window != NULL)
        {
			ucsNext = realloc(index->ucs_list, sizeof(struct ucs_point) * index->size);
			if (ucsNext == NULL) {
				free_index(index);
				return NULL;
			}
			index->ucs_list = ucsNext;
        }
    }

    /* fill in entry and increment how many we have */
    idxNext = index->idx_list + index->have;
    idxNext->in = in;
    idxNext->out = out;
	idxNext->bits = bits;
    if (window != NULL)
    {
		ucsNext = index->ucs_list + index->have;
		if (left)
			memcpy(ucsNext->window, window + WINSIZE - left, left);
		if (left < WINSIZE)
			memcpy(ucsNext->window + left, window, WINSIZE - left);
    }

    index->have++;

    /* return list, possibly reallocated */
    return index;
}

/* Use the index to read len bytes from offset into buf, return bytes read or
   negative for error (Z_DATA_ERROR or Z_MEM_ERROR).  If data is requested past
   the end of the uncompressed data, then extract() will return a value less
   than len, indicating how much as actually read into buf.  This function
   should not return a data error unless the file was modified since the index
   was generated.  extract() may also return Z_ERRNO if there is an error on
   reading or seeking the input file. */
local int extract(FILE *in, struct access *index, FILE *ucsFile,
				off_t offset, unsigned char *buf, int len)
{
    int ret, skip;
    z_stream strm;
    size_t counter;
    struct idx_point *pIdxHere;
    struct ucs_point *pUcsHere;
    unsigned char input[CHUNK];
    unsigned char discard[WINSIZE];
    struct ucs_point ucsHere;

    /* proceed only if something reasonable to do */
    if (len < 0)
        return 0;

    /* find where in stream to start */
    pIdxHere = index->idx_list;
    pUcsHere = index->ucs_list;
    ret = index->have;
    counter = 0;
    while (--ret && pIdxHere[1].out <= offset)
    {
        pIdxHere++;
        if (pUcsHere != NULL)
        	++pUcsHere;
        else
        	++counter;
    }
    if (pUcsHere == NULL) {
    	if (ucsFile == NULL)
    		return Z_DATA_ERROR;
    	/*fseeko(ucsFile, (off_t)(WINSIZE * counter), SEEK_SET);*/
    	fseek(ucsFile, (int) (WINSIZE * counter), SEEK_SET);
    	if (fread(&ucsHere.window, WINSIZE, 1u, ucsFile) < 1u)
    		return Z_DATA_ERROR;
    	/*ucsHere.bits = 0;*/
    	pUcsHere = &ucsHere;
    }
    /* initialize file and inflate state to start there */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit2(&strm, -15);         /* raw inflate */
    if (ret != Z_OK)
        return ret;
    ret = fseek(in, (long) ( pIdxHere->in - (pIdxHere->bits ? 1 : 0) ), SEEK_SET);
    if (ret == -1)
        goto extract_ret;
    if (pIdxHere->bits) {
        ret = getc(in);
        if (ret == -1) {
          ret = ferror(in) ? Z_ERRNO : Z_DATA_ERROR;
            goto extract_ret;
        }
        (void)inflatePrime(&strm, pIdxHere->bits, ret >> (8 - pIdxHere->bits));
    }
    (void)inflateSetDictionary(&strm, pUcsHere->window, WINSIZE);

    /* skip uncompressed bytes until offset reached, then satisfy request */
    offset -= pIdxHere->out;
    strm.avail_in = 0;
    skip = 1;                               /* while skipping to offset */
    do {
        /* define where to put uncompressed data, and how much */
        if (offset == 0 && skip) {          /* at offset now */
            strm.avail_out = len;
            strm.next_out = buf;
            skip = 0;                       /* only do this once */
        }
        if (offset > WINSIZE) {             /* skip WINSIZE bytes */
            strm.avail_out = WINSIZE;
            strm.next_out = discard;
            offset -= WINSIZE;
        }
        else if (offset != 0) {             /* last skip */
            strm.avail_out = (unsigned)offset;
            strm.next_out = discard;
            offset = 0;
        }

        /* uncompress until avail_out filled, or end of stream */
        do {
            if (strm.avail_in == 0) {
                strm.avail_in = fread(input, 1, CHUNK, in);
                if (ferror(in)) {
                    ret = Z_ERRNO;
                    goto extract_ret;
                }
                if (strm.avail_in == 0) {
                    ret = Z_DATA_ERROR;
                    goto extract_ret;
                }
                strm.next_in = input;
            }
            ret = inflate(&strm, Z_NO_FLUSH);       /* normal inflate */
            if (ret == Z_NEED_DICT)
                ret = Z_DATA_ERROR;
            if (ret == Z_MEM_ERROR || ret == Z_DATA_ERROR)
                goto extract_ret;
            if (ret == Z_STREAM_END)
                break;
        } while (strm.avail_out != 0);

        /* if reach end of stream, then don't keep trying to get more */
        if (ret == Z_STREAM_END)
            break;
        /* do until offset reached and requested data read, or stream ends */
    } while (skip);

    /* compute number of uncompressed bytes read after offset */
    ret = skip ? 0 : len - strm.avail_out;

    /* clean up and return bytes read or error */
  extract_ret:
    (void)inflateEnd(&strm);
    return ret;
}

/* Deallocate an index built by build_index() */
void free_index(struct access *index)
{
    if (index != NULL) {
    	if (index->ucs_list != NULL)
    		free(index->ucs_list);
        free(index->idx_list);
        free(index);
    }
}

/* Make one entire pass through the compressed stream and build an index, with
   access points about every span bytes of uncompressed output -- span is
   chosen to balance the speed of random access against the memory requirements
   of the list, about 32K bytes per access point.  Note that data after the end
   of the first zlib or gzip stream in the file is ignored.  build_index()
   returns the number of access points on success (>= 1), Z_MEM_ERROR for out
   of memory, Z_DATA_ERROR for an error in the input file, or Z_ERRNO for a
   file read error.  On success, *built points to the resulting index. */
int build_index(FILE *in, off_t span, struct access **built)
{
    int ret;
    off_t totin, totout;        /* our own total counters to avoid 4GB limit */
    off_t last;                 /* totout value of last access point */
    struct access *index;       /* access points being generated */
    z_stream strm;
    unsigned char input[CHUNK];
    unsigned char window[WINSIZE];

    /* initialize inflate */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit2(&strm, 47);      /* automatic zlib or gzip decoding */
    if (ret != Z_OK)
        return ret;

    /* inflate the input, maintain a sliding window, and build an index -- this
       also validates the integrity of the compressed data using the check
       information at the end of the gzip or zlib stream */
    totin = totout = last = 0;
    index = NULL;               /* will be allocated by first addpoint() */
    strm.avail_out = 0;
    do {
        /* get some compressed data from input file */
        strm.avail_in = fread(input, 1, CHUNK, in);
        if (ferror(in)) {
            ret = Z_ERRNO;
            goto build_index_error;
        }
        if (strm.avail_in == 0) {
            ret = Z_DATA_ERROR;
            goto build_index_error;
        }
        strm.next_in = input;

        /* process all of that, or until end of stream */
        do {
            /* reset sliding window if necessary */
            if (strm.avail_out == 0) {
                strm.avail_out = WINSIZE;
                strm.next_out = window;
            }

            /* inflate until out of input, output, or at end of block --
               update the total input and output counters */
            totin += strm.avail_in;
            totout += strm.avail_out;
            ret = inflate(&strm, Z_BLOCK);      /* return at end of block */
            totin -= strm.avail_in;
            totout -= strm.avail_out;
            if (ret == Z_NEED_DICT)
                ret = Z_DATA_ERROR;
            if (ret == Z_MEM_ERROR || ret == Z_DATA_ERROR)
                goto build_index_error;
            if (ret == Z_STREAM_END)
                break;
            /* if at end of block, consider adding an index entry (note that if
               data_type indicates an end-of-block, then all of the
               uncompressed data from that block has been delivered, and none
               of the compressed data after that block has been consumed,
               except for up to seven bits) -- the totout == 0 provides an
               entry point after the zlib or gzip header, and assures that the
               index always has at least one access point; we avoid creating an
               access point after the last block by checking bit 6 of data_type
             */
            if ((strm.data_type & 128) && !(strm.data_type & 64) &&
                (totout == 0 || totout - last > span)) {
                index = addpoint(index, strm.data_type & 7, totin,
                                 totout, strm.avail_out, window);
                if (index == NULL) {
                    ret = Z_MEM_ERROR;
                    goto build_index_error;
                }
                last = totout;
            }
        } while (strm.avail_in != 0);
    } while (ret != Z_STREAM_END);

    /* ADD AP AFTER LAST BLOCK */
    index = addpoint(index, strm.data_type & 7, totin, totout, strm.avail_out, window);
    if (index == NULL) {
        ret = Z_MEM_ERROR;
        goto build_index_error;
    }

    /* clean up and return index (release unused entries in list) */
    (void)inflateEnd(&strm);
    index->idx_list = realloc(index->idx_list, sizeof(struct idx_point) * index->have);
    index->ucs_list = realloc(index->ucs_list, sizeof(struct ucs_point) * index->have);
    index->size = index->have;
    *built = index;
    return index->size;

    /* return error */
  build_index_error:
    (void)inflateEnd(&strm);
    if (index != NULL)
        free_index(index);
    return ret;
}

int write_index(struct access *index, FILE *idxFile, FILE *ucsFile)
{
	int ret;
	size_t lenIdx, lenUcs, i;
	struct idx_point *pIdx;
	struct ucs_point *pUcs;
	ret = 0;
	if (index == NULL || idxFile == NULL || ucsFile == NULL)
		return ret;
	for (i = 0; i < index->have; ++i)
	{
		pIdx = &index->idx_list[i];
		pUcs = &index->ucs_list[i];
		lenIdx = fwrite((void*) pIdx, sizeof(pIdx->out)+sizeof(pIdx->in), 1u, idxFile);
		lenIdx = fwrite((void*) &pIdx->bits, sizeof(pIdx->bits), 1u, idxFile);
		lenUcs = fwrite((void*) pUcs->window, WINSIZE, 1u, ucsFile);
		if (lenUcs == 1u && lenIdx == 1u)
			++ret;
		else
			break;
	}
	return ret;
}

int read_index(FILE *idxFile, struct access **built)
{
	int ret, bits;
	off_t totout, totin;
	size_t chunkSize;
	struct access *index;
    off_t idxBuffer[2];
	ret = 0;
	index = NULL;
	if (idxFile == NULL)
		return ret;
	while (1) {
		chunkSize = fread((void*) idxBuffer, sizeof(off_t), 2u, idxFile)
				+ fread((void*) &bits, sizeof(int), 1u, idxFile);
		if (feof(idxFile) || chunkSize < 3u)
			break;
		totout = idxBuffer[0];
		totin = idxBuffer[1];
		index = addpoint(index, bits, totin,
						 totout, 0, (unsigned char *) NULL);
		if (index == NULL) {
			ret = Z_MEM_ERROR;
			if (index != NULL)
				free_index(index);
			return ret;
		}
		++ret;
	}
    index->idx_list = realloc(index->idx_list, sizeof(struct idx_point) * index->have);
    index->size = index->have;
	*built = index;
	return index->size;
}

/*local int zindex_read(FILE *inFile, FILE *idxFile, FILE *ucsFile, unsigned char *buffer, size_t chunkSize, off_t from)
{
	int len;
	struct access *index;

	index = NULL;

	len = read_index(idxFile, &index);
	if (len<0)
		return len;
	len = extract(inFile, index, ucsFile, from, buffer, chunkSize);
	return len;
}*/

zindexPtr ziopen_auto(const char *zPath, const char *mode)
{
	char *idxExt;
	char *idxName;
	char *ucsExt;
	char *ucsName;
	zindexPtr idx;
	size_t argLen;

	if (!mode || !strlen(mode)) {
		fprintf(stderr,"** ERROR: invalid ziopen call with mode \"%s\"\n", mode ? mode : "NULL");
		return NULL;
	}
    if (mode[0]!='r')
    	return NULL; /* writing is not yet supported */

	argLen = strlen(zPath);
	idxExt = ".idx";
	idxName = (char *) calloc(argLen + strlen(idxExt) + 1, sizeof(char));
	if (idxName == NULL) {
		fprintf(stderr,"** ERROR: ziopen failed to alloc idxName\n");
		return NULL;
	}
	strcpy(idxName, zPath);
	strcpy(idxName+argLen, idxExt);
	ucsExt = ".idx.ucs";
	ucsName = (char *) calloc(argLen + strlen(ucsExt) + 1, sizeof(char));
	if (ucsName == NULL) {
		free(idxName);
		fprintf(stderr,"** ERROR: ziopen failed to alloc ucsName\n");
		return NULL;
	}
	strcpy(ucsName, zPath);
	strcpy(ucsName+argLen, ucsExt);

	idx = ziopen(zPath, idxName, ucsName, mode);
	free(ucsName);
	free(idxName);
	return idx;
}

zindexPtr ziopen(const char *zPath, const char *idxPath, const char *ucsPath, const char *mode)
{
	zindexPtr idx;

	if (!mode || !strlen(mode)) {
		fprintf(stderr,"** ERROR: invalid ziopen call with mode \"%s\"\n", mode ? mode : "NULL");
		return NULL;
	}
    if (mode[0]!='r')
    	return NULL; /* writing is not yet supported */

	idx = (zindexPtr) calloc(1,sizeof(struct zindex));
	if (idx == NULL) {
		fprintf(stderr,"** ERROR: ziopen failed to alloc zindex\n");
		return NULL;
	}
	idx->zFile = NULL;
	idx->idxFile = NULL;
	idx->ucsFile = NULL;
	idx->data = NULL;
	if ((idx->idxFile = fopen(idxPath, mode)) == NULL) {
		free(idx);
		/* Give no error message here, fall back automatically, index will not be used. */
		/* fprintf(stderr,"** ziopen: cannot open %s for read\n", idxPath); */
		return NULL;
	}
	else if ((idx->ucsFile = fopen(ucsPath, mode)) == NULL) {
		fclose(idx->idxFile);
		free(idx);
		fprintf(stderr,"** ziopen: cannot open %s for read\n", ucsPath);
		return NULL;
	}
	else if ((idx->zFile = fopen(zPath, mode)) == NULL) {
		fclose(idx->ucsFile);
		fclose(idx->idxFile);
		free(idx);
		fprintf(stderr,"** ziopen: cannot open %s for read\n", zPath);
		return NULL;
	}

	if ( read_index( idx->idxFile, &(idx->data) ) <= 0 ) {
		fclose(idx->zFile);
		fclose(idx->ucsFile);
		fclose(idx->idxFile);
		free(idx);
		fprintf(stderr,"** ziopen: index file %s empty or corrupted\n", idxPath);
		return NULL;
	}

	idx->pos = 0;
	idx->end = idx->data->idx_list[idx->data->have-1].out; /*last index entry is eof*/
	return idx;
}

zindexPtr zidopen(int zfd, int idxfd, int ucsfd, const char *mode)
{
#ifndef HAVE_FDOPEN
	return NULL;
#else
	zindexPtr idx;

	if (!mode || !strlen(mode)) {
		fprintf(stderr,"** ERROR: invalid zidopen call with mode \"%s\"\n", mode ? mode : "NULL");
		return NULL;
	}
    if (mode[0]!='r')
    	return NULL; /* writing is not yet supported */

	idx = (zindexPtr) calloc(1,sizeof(struct zindex));
	if (idx == NULL) {
		fprintf(stderr,"** ERROR: zidopen failed to alloc zindex\n");
		return NULL;
	}
	idx->zFile = NULL;
	idx->idxFile = NULL;
	idx->ucsFile = NULL;
	idx->data = NULL;
	if ((idx->idxFile = fdopen(idxfd, mode)) == NULL) {
		free(idx);
		fprintf(stderr,"** zidopen: cannot open idx file for read\n");
		return NULL;
	}
	else if ((idx->ucsFile = fdopen(ucsfd, mode)) == NULL) {
		fclose(idx->idxFile);
		free(idx);
		fprintf(stderr,"** zidopen: cannot open ucs file for read\n");
		return NULL;
	}
	else if ((idx->zFile = fdopen(zfd, mode)) == NULL) {
		fclose(idx->ucsFile);
		fclose(idx->idxFile);
		free(idx);
		fprintf(stderr,"** zidopen: cannot open gz file for read\n");
		return NULL;
	}

	if ( read_index( idx->idxFile, &(idx->data) ) <= 0 ) {
		fclose(idx->zFile);
		fclose(idx->ucsFile);
		fclose(idx->idxFile);
		free(idx);
		fprintf(stderr,"** zidopen: index file empty or corrupted\n");
		return NULL;
	}

	idx->pos = 0;
	idx->end = idx->data->idx_list[idx->data->have-1].out; /*last index entry is eof*/
	return idx;
#endif
}

int ziclose(zindexPtr * idx)
{
	int retval = 0;
	if ((*idx) == NULL)
		return retval;

	if ((*idx)->zFile!=NULL) { retval = fclose((*idx)->zFile); }
	if ((*idx)->idxFile!=NULL) { retval += fclose((*idx)->idxFile); }
	if ((*idx)->ucsFile!=NULL) { retval += fclose((*idx)->ucsFile); }

	free(*idx);
	*idx = NULL;

	return retval;
}

int ziread(zindexPtr idx, void* buf, unsigned len)
{
	int nread;

	if (idx==NULL)
		return 0;
	nread = extract(idx->zFile, idx->data, idx->ucsFile,
			 idx->pos, (unsigned char *)buf, len);
	if( nread < 0 ) return nread; /* returns -1 on error */
	idx->pos += nread;
	return nread;
}

int ziwrite(zindexPtr idx, const void* buf, unsigned len)
{
	fprintf(stderr,"** ziwrite: writing gz file with index not yet supported\n");
	return 0;
}

long ziseek(zindexPtr idx, long offset, int whence)
{
	if (idx==NULL)
		return 0;
	switch(whence) {
	  case SEEK_SET:
		  idx->pos = offset; break;
	  case SEEK_CUR:
		  idx->pos += offset; break;
	  case SEEK_END:
		  idx->pos = idx->end + offset; break;
	  default:
		  fprintf(stderr,"** ziseek: seek whence %i not supported\n", whence);
		  return -1;
	}
	if (idx->pos < 0) {
	  fprintf(stderr,"** ziseek: negative seek value (%li) changed to 0\n", idx->pos);
	  idx->pos = 0;
	}
	if (idx->pos > idx->end) {
	  fprintf(stderr,"** ziseek: seek past eof (%li) reverted to eof (%li)\n", idx->pos, idx->end);
	  idx->pos = idx->end;
	}
	return (long) idx->pos;
}

int zirewind(zindexPtr idx)
{
	if (idx==NULL)
		return 0;
	return (int) (idx->pos = 0);
}

long zitell(zindexPtr idx)
{
	if (idx==NULL)
		return 0;
	return (long) idx->pos;
}

int ziputs(zindexPtr idx, const char *str)
{
	fprintf(stderr,"** ziputs: writing gz file with index not yet supported\n");
	return 0;
}

char * zigets(zindexPtr idx, char* str, int size)
{
	int nread;
	if (idx==NULL)
		return NULL;
	nread = extract(idx->zFile, idx->data, idx->ucsFile,
					 idx->pos, (unsigned char *)str, size);
	if (nread == size)
	  return str;
	return NULL;
}

int ziflush(zindexPtr idx)
{
	fprintf(stderr,"** ziflush: writing gz file with index not yet supported\n");
	return 0;
}

int zieof(zindexPtr idx)
{
	if (idx==NULL)
		return 0;
	return idx->pos < idx->end ? 0 : 1;
}


int ziputc(zindexPtr idx, int c)
{
	fprintf(stderr,"** ziputc: writing gz file with index not yet supported\n");
	return 0;
}

int zigetc(zindexPtr idx)
{
	char ret;
	int nread;
	if (idx==NULL)
		return 0;
	nread = extract(idx->zFile, idx->data, idx->ucsFile,
					 idx->pos, (unsigned char *) &ret, 1);
	if (nread == 1)
	  return (int) ret;
	return 0;
}

#if !defined(WIN32)
int ziprintf(zindexPtr idx, const char *format, ...)
{
	fprintf(stderr,"** ziprintf: writing gz file with index not yet supported\n");
	return -1;
}
#endif
