/*====================================================================*
 -  Copyright (C) 2001 Leptonica.  All rights reserved.
 -  This software is distributed in the hope that it will be
 -  useful, but with NO WARRANTY OF ANY KIND.
 -  No author or distributor accepts responsibility to anyone for the
 -  consequences of using this software, or for whether it serves any
 -  particular purpose or works at all, unless he or she says so in
 -  writing.  Everyone is granted permission to copy, modify and
 -  redistribute this source code, for commercial or non-commercial
 -  purposes, with the following restrictions: (1) the origin of this
 -  source code must not be misrepresented; (2) modified versions must
 -  be plainly marked as such; and (3) this notice may not be removed
 -  or altered from any source or modified source distribution.
 *====================================================================*/

/*
 *  pnmio.c
 *
 *      Stream interface
 *          PIX             *pixReadStreamPnm()
 *          l_int32          pixWriteStreamPnm()
 *          l_int32          pixWriteStreamAsciiPnm()
 *
 *      Read/write to memory   [not on windows]
 *          PIX             *pixReadMemPnm()
 *          l_int32          pixWriteMemPnm()
 *
 *      Local helpers
 *          static l_int32   pnmReadNextAsciiValue();
 *          static l_int32   pnmSkipCommentLines();
 *       
 *      These are here by popular demand, with the help of Mattias
 *      Kregert (mattias@kregert.se), who provided the first implementation.
 *
 *      The pnm formats are exceedingly simple, because they have
 *      no compression and no colormaps.  They support images that
 *      are 1 bpp; 2, 4, 8 and 16 bpp grayscale; and rgb.
 *
 *      The original pnm formats ("ascii") are included for completeness,
 *      but their use is deprecated for all but tiny iconic images.
 *      They are extremely wasteful of memory; for example, the P1 binary
 *      ascii format is 16 times as big as the packed uncompressed
 *      format, because 2 characters are used to represent every bit
 *      (pixel) in the image.  Reading is slow because we check for extra
 *      white space and EOL at every sample value.
 * 
 *      The packed pnm formats ("raw") give file sizes similar to
 *      bmp files, which are uncompressed packed.  However, bmp
 *      are more flexible, because they can support colormaps.
 *
 *      We don't differentiate between the different types ("pbm",
 *      "pgm", "ppm") at the interface level, because this is really a
 *      "distinction without a difference."  You read a file, you get
 *      the appropriate Pix.  You write a file from a Pix, you get the
 *      appropriate type of file.  If there is a colormap on the Pix,
 *      and the Pix is more than 1 bpp, you get either an 8 bpp pgm
 *      or a 24 bpp RGB pnm, depending on whether the colormap colors
 *      are gray or rgb, respectively.
 *
 *      This follows the general policy that the I/O routines don't
 *      make decisions about the content of the image -- you do that
 *      with image processing before you write it out to file.
 *      The I/O routines just try to make the closest connection
 *      possible between the file and the Pix in memory.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "allheaders.h"

/* --------------------------------------------*/
#if  USE_PNMIO   /* defined in environ.h */
/* --------------------------------------------*/


static l_int32 pnmReadNextAsciiValue(FILE  *fp, l_int32 *pval);
static l_int32 pnmSkipCommentLines(FILE  *fp);

    /* a sanity check on the size read from file */
static const l_int32  MAX_PNM_WIDTH = 100000;
static const l_int32  MAX_PNM_HEIGHT = 100000;


/*--------------------------------------------------------------------*
 *                          Stream interface                          *
 *--------------------------------------------------------------------*/
/*!
 *  pixReadStreamPnm()
 *
 *      Input:  stream opened for read
 *      Return: pix, or null on error
 */
PIX *
pixReadStreamPnm(FILE  *fp)
{
l_uint8    val8, rval8, gval8, bval8;
l_int32    w, h, d, bpl, wpl, i, j, type;
l_int32    maxval, val, rval, gval, bval;
l_uint32   rgbval;
l_uint32  *line, *data;
PIX       *pix;

    PROCNAME("pixReadStreamPnm");

    if (!fp)
        return (PIX *)ERROR_PTR("fp not defined", procName, NULL);

    fscanf(fp, "P%d\n", &type);
    if (type < 1 || type > 6)
        return (PIX *)ERROR_PTR("invalid pnm file", procName, NULL);

    if (pnmSkipCommentLines(fp))
        return (PIX *)ERROR_PTR("no data in file", procName, NULL);

    fscanf(fp, "%d %d\n", &w, &h);
    if (w <= 0 || h <= 0 || w > MAX_PNM_WIDTH || h > MAX_PNM_HEIGHT)
        return (PIX *)ERROR_PTR("invalid sizes", procName, NULL);

        /* Get depth of pix */
    if (type == 1 || type == 4)
        d = 1;
    else if (type == 2 || type == 5) {
        fscanf(fp, "%d\n", &maxval);
        if (maxval == 3)
            d = 2;
        else if (maxval == 15)
            d = 4;
        else if (maxval == 255)
            d = 8;
        else if (maxval == 0xffff)
            d = 16;
        else {
            fprintf(stderr, "maxval = %d\n", maxval);
            return (PIX *)ERROR_PTR("invalid maxval", procName, NULL);
        }
    }
    else {  /* type == 3 || type == 6; this is rgb  */
        fscanf(fp, "%d\n", &maxval);
        if (maxval != 255)
            L_WARNING_INT("unexpected maxval = %d", procName, maxval);
        d = 32;
    }
    
    if ((pix = pixCreate(w, h, d)) == NULL)
        return (PIX *)ERROR_PTR( "pix not made", procName, NULL);
    data = pixGetData(pix);
    wpl = pixGetWpl(pix);

        /* Old "ascii" format */
    if (type <= 3) {
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                if (type == 1 || type == 2) {
                    if (pnmReadNextAsciiValue(fp, &val))
                        return (PIX *)ERROR_PTR( "read abend", procName, pix);
                    pixSetPixel(pix, j, i, val);
                }
                else {  /* type == 3 */
                    if (pnmReadNextAsciiValue(fp, &rval))
                        return (PIX *)ERROR_PTR( "read abend", procName, pix);
                    if (pnmReadNextAsciiValue(fp, &gval))
                        return (PIX *)ERROR_PTR( "read abend", procName, pix);
                    if (pnmReadNextAsciiValue(fp, &bval))
                        return (PIX *)ERROR_PTR( "read abend", procName, pix);
                    composeRGBPixel(rval, gval, bval, &rgbval);
                    pixSetPixel(pix, j, i, rgbval);
                }
            }
        }
        return pix;
    }

        /* "raw" format; binary and grayscale */
    if (type == 4 || type == 5) {
        bpl = (d * w + 7) / 8;
        for (i = 0; i < h; i++) {
            line = data + i * wpl;
            for (j = 0; j < bpl; j++) {
                fread(&val8, 1, 1, fp);
                SET_DATA_BYTE(line, j, val8);
            }
        }
        return pix;
    }

        /* "raw" format, type == 6; rgb */
    for (i = 0; i < h; i++) {
        line = data + i * wpl;
        for (j = 0; j < wpl; j++) {
            fread(&rval8, 1, 1, fp);
            fread(&gval8, 1, 1, fp);
            fread(&bval8, 1, 1, fp);
            composeRGBPixel(rval8, gval8, bval8, &rgbval);
            line[j] = rgbval;
        }
    }
    return pix;
}


/*!
 *  pixWriteStreamPnm()
 *
 *      Input:  stream opened for write
 *              pix
 *      Return: 0 if OK; 1 on error
 *
 *  Writes "raw" packed format only:
 *      1 bpp --> pbm (P4)
 *      2, 4, 8, 16 bpp, no colormap or grayscale colormap --> pgm (P5)
 *      2, 4, 8 bpp with color-valued colormap, or rgb --> rgb ppm (P6)
 */
l_int32
pixWriteStreamPnm(FILE  *fp,
                  PIX   *pix)
{
l_uint8    byteval, rval, gval, bval;
l_int32    h, w, d, ds, i, j, wpls, bpl, maxval;
l_uint32  *datas, *lines;
PIX       *pixs;

    PROCNAME("pixWriteStreamPnm");

    if (!fp)
        return ERROR_INT("fp not defined", procName, 1);
    if (!pix)
        return ERROR_INT("pix not defined", procName, 1);

    pixGetDimensions(pix, &w, &h, &d);
    if (d != 1 && d != 2 && d != 4 && d != 8 && d != 16 && d != 32)
        return ERROR_INT("d not in {1,2,4,8,16,32}", procName, 1);

        /* If a colormap exists, remove and convert to grayscale or rgb */
    if (pixGetColormap(pix) != NULL)
        pixs = pixRemoveColormap(pix, REMOVE_CMAP_BASED_ON_SRC);
    else
        pixs = pixClone(pix);
    ds =  pixGetDepth(pixs);
    datas = pixGetData(pixs);
    wpls = pixGetWpl(pixs);

    if (ds == 1) {  /* binary */
        fprintf(fp, "P4\n# Raw PBM file written by leptonlib (www.leptonica.com)\n%d %d\n", w, h);

        bpl = (w + 7) / 8;
        for (i = 0; i < h; i++) {
            lines = datas + i * wpls;
            for (j = 0; j < bpl; j++) {
                byteval = GET_DATA_BYTE(lines, j);
                fwrite(&byteval, 1, 1, fp);
            }
        }
    }
    else if (ds == 2 || ds == 4 || ds == 8 || ds == 16) {  /* grayscale */
        maxval = (1 << ds) - 1;
        fprintf(fp, "P5\n# Raw PGM file written by leptonlib (www.leptonica.com)\n%d %d\n%d\n", w, h, maxval);

        bpl = (ds * w + 7) / 8;
        for (i = 0; i < h; i++) {
            lines = datas + i * wpls;
            for (j = 0; j < bpl; j++) {
                byteval = GET_DATA_BYTE(lines, j);
                fwrite(&byteval, 1, 1, fp);
            }
        }
    }
    else {  /* rgb color */
        fprintf(fp, "P6\n# Raw PPM file written by leptonlib (www.leptonica.com)\n%d %d\n255\n", w, h);

        for (i = 0; i < h; i++) {
            lines = datas + i * wpls;
            for (j = 0; j < wpls; j++) {
                rval = GET_DATA_BYTE(lines + j, COLOR_RED);
                gval = GET_DATA_BYTE(lines + j, COLOR_GREEN);
                bval = GET_DATA_BYTE(lines + j, COLOR_BLUE);
                fwrite(&rval, 1, 1, fp);
                fwrite(&gval, 1, 1, fp);
                fwrite(&bval, 1, 1, fp);
            }
        }
    }

    pixDestroy(&pixs);
    return 0;
}


/*!
 *  pixWriteStreamAsciiPnm()
 *
 *      Input:  stream opened for write
 *              pix
 *      Return: 0 if OK; 1 on error
 *
 *  Writes "ascii" format only:
 *      1 bpp --> pbm (P1)
 *      2, 4, 8, 16 bpp, no colormap or grayscale colormap --> pgm (P2)
 *      2, 4, 8 bpp with color-valued colormap, or rgb --> rgb ppm (P3)
 */
l_int32
pixWriteStreamAsciiPnm(FILE  *fp,
                       PIX   *pix)
{
char       buffer[256];
l_uint8    cval[3];
l_int32    h, w, d, ds, i, j, k, maxval, count;
l_uint32   val;
PIX       *pixs;

    PROCNAME("pixWriteStreamAsciiPnm");

    if (!fp)
        return ERROR_INT("fp not defined", procName, 1);
    if (!pix)
        return ERROR_INT("pix not defined", procName, 1);

    pixGetDimensions(pix, &w, &h, &d);
    if (d != 1 && d != 2 && d != 4 && d != 8 && d != 16 && d != 32)
        return ERROR_INT("d not in {1,2,4,8,16,32}", procName, 1);

        /* If a colormap exists, remove and convert to grayscale or rgb */
    if (pixGetColormap(pix) != NULL)
        pixs = pixRemoveColormap(pix, REMOVE_CMAP_BASED_ON_SRC);
    else
        pixs = pixClone(pix);
    ds =  pixGetDepth(pixs);

    if (ds == 1) {  /* binary */
        fprintf(fp, "P1\n# Ascii PBM file written by leptonlib (www.leptonica.com)\n%d %d\n", w, h);

        count = 0;
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                pixGetPixel(pixs, j, i, &val);
                if (val == 0)
                    fputc('0', fp);
                else  /* val == 1 */
                    fputc('1', fp);
                fputc(' ', fp);
                count += 2;
                if (count >= 70)
                    fputc('\n', fp);
            }
        }
    }
    else if (ds == 2 || ds == 4 || ds == 8 || ds == 16) {  /* grayscale */
        maxval = (1 << ds) - 1;
        fprintf(fp, "P2\n# Ascii PGM file written by leptonlib (www.leptonica.com)\n%d %d\n%d\n", w, h, maxval);

        count = 0;
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                pixGetPixel(pixs, j, i, &val);
                if (ds == 2) {
                    sprintf(buffer, "%1d ", val);
                    fwrite(buffer, 1, 2, fp);
                    count += 2;
                }
                else if (ds == 4) {
                    sprintf(buffer, "%2d ", val);
                    fwrite(buffer, 1, 3, fp);
                    count += 3;
                }
                else if (ds == 8) {
                    sprintf(buffer, "%3d ", val);
                    fwrite(buffer, 1, 4, fp);
                    count += 4;
                }
                else {  /* ds == 16 */
                    sprintf(buffer, "%5d ", val);
                    fwrite(buffer, 1, 6, fp);
                    count += 6;
                }
                if (count >= 60) {
                    fputc('\n', fp);
                    count = 0;
                }
            }
        }
    }
    else {  /* rgb color */
        fprintf(fp, "P3\n# Ascii PPM file written by leptonlib (www.leptonica.com)\n%d %d\n255\n", w, h);

        count = 0;
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                pixGetPixel(pixs, j, i, &val);
                cval[0] = GET_DATA_BYTE(&val, COLOR_RED);
                cval[1] = GET_DATA_BYTE(&val, COLOR_GREEN);
                cval[2] = GET_DATA_BYTE(&val, COLOR_BLUE);
                for (k = 0; k < 3; k++) {
                    sprintf(buffer, "%3d ", cval[k]);
                    fwrite(buffer, 1, 4, fp);
                    count += 4;
                    if (count >= 60) {
                        fputc('\n', fp);
                        count = 0;
                    }
                }
            }
        }
    }

    pixDestroy(&pixs);
    return 0;
}


/*---------------------------------------------------------------------*
 *                         Read/write to memory                        *
 *---------------------------------------------------------------------*/
#ifdef HAVE_CONFIG_H
#include "config_auto.h"
#endif  /* HAVE_CONFIG_H */

#if HAVE_FMEMOPEN

extern FILE *open_memstream(char **data, size_t *size);
extern FILE *fmemopen(void *data, size_t size, const char *mode);

/*!
 *  pixReadMemPnm()
 *
 *      Input:  cdata (const; pnm-encoded)
 *              size (of data)
 *      Return: pix, or null on error
 *
 *  Notes:
 *      (1) The @size byte of @data must be a null character.
 */
PIX *
pixReadMemPnm(const l_uint8  *cdata,
              size_t          size)
{
l_uint8  *data;
FILE     *fp;
PIX      *pix;

    PROCNAME("pixReadMemPnm");

    if (!cdata)
        return (PIX *)ERROR_PTR("cdata not defined", procName, NULL);

    data = (l_uint8 *)cdata;  /* we're really not going to change this */
    if ((fp = fmemopen(data, size, "r")) == NULL)
        return (PIX *)ERROR_PTR("stream not opened", procName, NULL);
    pix = pixReadStreamPnm(fp);
    fclose(fp);
    return pix;
}


/*!
 *  pixWriteMemPnm()
 *
 *      Input:  &data (<return> data of tiff compressed image)
 *              &size (<return> size of returned data)
 *              pix
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) See pixWriteStreamPnm() for usage.  This version writes to
 *          memory instead of to a file stream.
 */
l_int32
pixWriteMemPnm(l_uint8  **pdata,
               size_t    *psize,
               PIX       *pix)
{
l_int32  ret;
FILE    *fp;

    PROCNAME("pixWriteMemPnm");

    if (!pdata)
        return ERROR_INT("&data not defined", procName, 1 );
    if (!psize)
        return ERROR_INT("&size not defined", procName, 1 );
    if (!pix)
        return ERROR_INT("&pix not defined", procName, 1 );

    if ((fp = open_memstream((char **)pdata, psize)) == NULL)
        return ERROR_INT("stream not opened", procName, 1);
    ret = pixWriteStreamPnm(fp, pix);
    fclose(fp);
    return ret;
}

#else

PIX *
pixReadMemPnm(const l_uint8  *cdata,
              size_t          size)
{
    return (PIX *)ERROR_PTR(
        "pnm read from memory not implemented on this platform",
        "pixReadMemPnm", NULL);
}


l_int32
pixWriteMemPnm(l_uint8  **pdata,
               size_t    *psize,
               PIX       *pix)
{
    return ERROR_INT(
        "pnm write to memory not implemented on this platform",
        "pixWriteMemPnm", 1);
}

#endif  /* HAVE_FMEMOPEN */


/*--------------------------------------------------------------------*
 *                          Static helpers                            *
 *--------------------------------------------------------------------*/
/*!
 *  pnmReadNextAsciiValue()
 *
 *      Return: 0 if OK, 1 on error or EOF.
 *
 *  Notes:
 *      (1) This reads the next sample value in ascii from the the file.
 */
static l_int32
pnmReadNextAsciiValue(FILE  *fp,
                      l_int32 *pval)
{
l_int32   c;

    PROCNAME("pnmReadNextAsciiValue");

    if (!fp)
        return ERROR_INT("stream not open", procName, 1);
    if (!pval)
        return ERROR_INT("&val not defined", procName, 1);
    *pval = 0;
    do {  /* skip whitespace */
        if ((c = fgetc(fp)) == EOF)
            return 1;
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');

    fseek(fp, -1L, SEEK_CUR);        /* back up one byte */
    fscanf(fp, "%d", pval);
    return 0;
}


/*!
 *  pnmSkipCommentLines()
 *
 *      Return: 0 if OK, 1 on error or EOF
 *
 *  Notes:
 *      (1) Comment lines begin with '#'
 *      (2) Usage: caller should check return value for EOF
 */
static l_int32 
pnmSkipCommentLines(FILE  *fp)
{
l_int32  c;

    PROCNAME("pnmSkipCommentLines");

    if (!fp)
        return ERROR_INT("stream not open", procName, 1);
    if ((c = fgetc(fp)) == EOF)
        return 1;
    if (c == '#') {
        do {  /* each line starting with '#' */
            do {  /* this entire line */
                if ((c = fgetc(fp)) == EOF)
                    return 1;
            } while (c != '\n');
            if ((c = fgetc(fp)) == EOF)
                return 1;
        } while (c == '#');
    }

        /* Back up one byte */
    fseek(fp, -1L, SEEK_CUR);
    return 0;
}

/* --------------------------------------------*/
#endif  /* USE_PNMIO */
/* --------------------------------------------*/

