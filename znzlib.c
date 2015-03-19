/** \file znzlib.c
    \brief Low level i/o interface to compressed and noncompressed files.
        Written by Mark Jenkinson, FMRIB

This library provides an interface to both compressed (gzip/zlib) and
uncompressed (normal) file IO.  The functions are written to have the
same interface as the standard file IO functions.

To use this library instead of normal file IO, the following changes
are required:
 - replace all instances of FILE* with znzFile
 - change the name of all function calls, replacing the initial character
   f with the znz  (e.g. fseek becomes znzseek)
   one exception is rewind() -> znzrewind()
 - add a third parameter to all calls to znzopen (previously fopen)
   that specifies whether to use compression (1) or not (0)
 - use znz_isnull rather than any (pointer == NULL) comparisons in the code
   for znzfile types (normally done after a return from znzopen)
 
NB: seeks for writable files with compression are quite restricted

NB: seeks for large files with compression got notably faster:
*****  Modified by Zalan Rajna on 18.03.2015
*****  For modifications: copyright 2015 Zalan Rajna under GNU GPLv3

 */

#include "znzlib.h"

/*
znzlib.c  (zipped or non-zipped library)

*****            This code is released to the public domain.            *****

*****  Author: Mark Jenkinson, FMRIB Centre, University of Oxford       *****
*****  Date:   September 2004                                           *****

*****  Neither the FMRIB Centre, the University of Oxford, nor any of   *****
*****  its employees imply any warranty of usefulness of this software  *****
*****  for any purpose, and do not assume any liability for damages,    *****
*****  incidental or otherwise, caused by any use of this document.     *****

*****  Modified by Zalan Rajna on 18.03.2015

*/


/* Note extra argument (use_compression) where 
   use_compression==0 is no compression
   use_compression!=0 uses zlib (gzip) compression
*/

znzFile znzopen(const char *path, const char *mode, int use_compression)
{
  znzFile file;
  file = (znzFile) calloc(1,sizeof(struct znzptr));
  if( file == NULL ){
     fprintf(stderr,"** ERROR: znzopen failed to alloc znzptr\n");
     return NULL;
  }

  file->nzfptr = NULL;

#ifdef HAVE_ZLIB
  file->zfptr = NULL;

  if (use_compression) {
    file->withz = 1;
    if ((file->idx = ziopen_auto(path,mode))==NULL ) {
    	if ((file->zfptr = gzopen(path,mode))==NULL ) {
    		free(file);
    		file = NULL;
    	}
    }
  } else {
#endif

    file->withz = 0;
    if((file->nzfptr = fopen(path,mode)) == NULL) {
      free(file);
      file = NULL;
    }

#ifdef HAVE_ZLIB
  }
#endif
  return file;
}


znzFile znzdopen(int fd, const char *mode, int use_compression)
{
  znzFile file;
  file = (znzFile) calloc(1,sizeof(struct znzptr));
  if( file == NULL ){
     fprintf(stderr,"** ERROR: znzdopen failed to alloc znzptr\n");
     return NULL;
  }
#ifdef HAVE_ZLIB
  if (use_compression) {
    file->withz = 1;
    file->zfptr = gzdopen(fd,mode);
    file->idx = NULL;
  } else {
#endif
    file->withz = 0;
#ifdef HAVE_FDOPEN
    file->nzfptr = fdopen(fd,mode);
#endif
#ifdef HAVE_ZLIB
    file->zfptr = NULL;
    file->idx = NULL;
  };
#endif
  return file;
}


int Xznzclose(znzFile * file)
{
  int retval = 0;
  if (*file!=NULL) {
#ifdef HAVE_ZLIB
	if ((*file)->idx != NULL) { retval = ziclose( &((*file)->idx) ); }
	if ((*file)->zfptr!=NULL) { retval = gzclose((*file)->zfptr); }
#endif
    if ((*file)->nzfptr!=NULL) { retval = fclose((*file)->nzfptr); }
                                                                                
    free(*file);
    *file = NULL;
  }
  return retval;
}


/* we already assume ints are 4 bytes */
#undef ZNZ_MAX_BLOCK_SIZE
#define ZNZ_MAX_BLOCK_SIZE (1<<30)

size_t znzread(void* buf, size_t size, size_t nmemb, znzFile file)
{
  size_t     remain = size*nmemb;
  char     * cbuf = (char *)buf;
  unsigned   n2read;
  int        nread;

  if (file==NULL) { return 0; }
#ifdef HAVE_ZLIB
  if (file->zfptr!=NULL || file->idx!= NULL) {
    /* gzread/write take unsigned int length, so maybe read in int pieces
       (noted by M Hanke, example given by M Adler)   6 July 2010 [rickr] */
    while( remain > 0 ) {
       n2read = (remain < ZNZ_MAX_BLOCK_SIZE) ? remain : ZNZ_MAX_BLOCK_SIZE;

       if (file->zfptr!=NULL)
    	   nread = gzread(file->zfptr, (void *)cbuf, n2read);
       else
    	   nread = ziread(file->idx, (void*)cbuf, n2read);

       if( nread < 0 ) return nread; /* returns -1 on error */

       remain -= nread;
       cbuf += nread;

       /* require reading n2read bytes, so we don't get stuck */
       if( nread < (int)n2read ) break;  /* return will be short */
    }

    /* warn of a short read that will seem complete */
    if( remain > 0 && remain < size )
       fprintf(stderr,"** znzread: read short by %u bytes\n",(unsigned)remain);

    return nmemb - remain/size;   /* return number of members processed */
  }
#endif
  return fread(buf,size,nmemb,file->nzfptr);
}

size_t znzwrite(const void* buf, size_t size, size_t nmemb, znzFile file)
{
  size_t     remain = size*nmemb;
  char     * cbuf = (char *)buf;
  unsigned   n2write;
  int        nwritten;

  if (file==NULL) { return 0; }
#ifdef HAVE_ZLIB
  if (file->zfptr!=NULL || file->idx!=NULL) {
    while( remain > 0 ) {
       n2write = (remain < ZNZ_MAX_BLOCK_SIZE) ? remain : ZNZ_MAX_BLOCK_SIZE;

       if (file->zfptr!=NULL)
    	   nwritten = gzwrite(file->zfptr, (void *)cbuf, n2write);
       else
    	   nwritten = ziwrite(file->idx, (void*)cbuf, n2write);

       /* gzread returns 0 on error, but in case that ever changes... */
       if( nwritten < 0 ) return nwritten;

       remain -= nwritten;
       cbuf += nwritten;

       /* require writing n2write bytes, so we don't get stuck */
       if( nwritten < (int)n2write ) break;
    }

    /* warn of a short write that will seem complete */
    if( remain > 0 && remain < size )
      fprintf(stderr,"** znzwrite: write short by %u bytes\n",(unsigned)remain);

    return nmemb - remain/size;   /* return number of members processed */
  }
#endif
  return fwrite(buf,size,nmemb,file->nzfptr);
}

long znzseek(znzFile file, long offset, int whence)
{
  if (file==NULL) { return 0; }
#ifdef HAVE_ZLIB
  if (file->zfptr!=NULL) return (long) gzseek(file->zfptr,offset,whence);
  if (file->idx!=NULL) return (long) ziseek(file->idx,offset,whence);
#endif
  return fseek(file->nzfptr,offset,whence);
}

int znzrewind(znzFile stream)
{
  if (stream==NULL) { return 0; }
#ifdef HAVE_ZLIB
  /* On some systems, gzrewind() fails for uncompressed files.
     Use gzseek(), instead.               10, May 2005 [rickr]

     if (stream->zfptr!=NULL) return gzrewind(stream->zfptr);
  */
  if (stream->zfptr!=NULL) return (int)gzseek(stream->zfptr, 0L, SEEK_SET);
  if (stream->idx!=NULL) return (int) zirewind(stream->idx);
#endif
  rewind(stream->nzfptr);
  return 0;
}

long znztell(znzFile file)
{
  if (file==NULL) { return 0; }
#ifdef HAVE_ZLIB
  if (file->zfptr!=NULL) return (long) gztell(file->zfptr);
  if (file->idx!=NULL) return (long) zitell(file->idx);
#endif
  return ftell(file->nzfptr);
}

int znzputs(const char * str, znzFile file)
{
  if (file==NULL) { return 0; }
#ifdef HAVE_ZLIB
  if (file->zfptr!=NULL) return gzputs(file->zfptr,str);
  if (file->idx!=NULL) return ziputs(file->idx,str);
#endif
  return fputs(str,file->nzfptr);
}


char * znzgets(char* str, int size, znzFile file)
{
  if (file==NULL) { return NULL; }
#ifdef HAVE_ZLIB
  if (file->zfptr!=NULL) return gzgets(file->zfptr,str,size);
  if (file->idx!=NULL) return zigets(file->idx,str,size);
#endif
  return fgets(str,size,file->nzfptr);
}


int znzflush(znzFile file)
{
  if (file==NULL) { return 0; }
#ifdef HAVE_ZLIB
  if (file->zfptr!=NULL) return gzflush(file->zfptr,Z_SYNC_FLUSH);
  if (file->idx!=NULL) return ziflush(file->idx);
#endif
  return fflush(file->nzfptr);
}


int znzeof(znzFile file)
{
  if (file==NULL) { return 0; }
#ifdef HAVE_ZLIB
  if (file->zfptr!=NULL) return gzeof(file->zfptr);
  if (file->idx!=NULL) return zieof(file->idx);
#endif
  return feof(file->nzfptr);
}


int znzputc(int c, znzFile file)
{
  if (file==NULL) { return 0; }
#ifdef HAVE_ZLIB
  if (file->zfptr!=NULL) return gzputc(file->zfptr,c);
  if (file->idx!=NULL) return ziputc(file->idx,c);
#endif
  return fputc(c,file->nzfptr);
}


int znzgetc(znzFile file)
{
  if (file==NULL) { return 0; }
#ifdef HAVE_ZLIB
  if (file->zfptr!=NULL) return gzgetc(file->zfptr);
  if (file->idx!=NULL) return zigetc(file->idx);
#endif
  return fgetc(file->nzfptr);
}

#if !defined (WIN32)
int znzprintf(znzFile stream, const char *format, ...)
{
  int retval=0;
  char *tmpstr;
  va_list va;
  if (stream==NULL) { return 0; }
  va_start(va, format);
#ifdef HAVE_ZLIB
  if (stream->zfptr!=NULL || stream->idx!=NULL) {
    int size;  /* local to HAVE_ZLIB block */
    size = strlen(format) + 1000000;  /* overkill I hope */
    tmpstr = (char *)calloc(1, size);
    if( tmpstr == NULL ){
       fprintf(stderr,"** ERROR: znzprintf failed to alloc %d bytes\n", size);
       return retval;
    }
    vsprintf(tmpstr,format,va);

    if (stream->zfptr!=NULL)
    	retval = gzprintf(stream->zfptr,"%s",tmpstr);
    else
    	retval = ziprintf(stream->idx,"%s",tmpstr);

    free(tmpstr);
  } else
#endif
  {
	  retval=vfprintf(stream->nzfptr,format,va);
  }
  va_end(va);
  return retval;
}

#endif

