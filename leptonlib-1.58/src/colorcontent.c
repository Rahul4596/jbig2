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
 *  colorcontent.c
 *                     
 *      Builds an image of the color content, on a per-pixel basis,
 *      as a measure of the amount of divergence of each color
 *      component (R,G,B) from gray.
 *         l_int32    pixColorContent()
 *
 *      Finds the 'amount' of color in an image, on a per-pixel basis,
 *      as a measure of the difference of the pixel color from gray.
 *         PIX       *pixColorMagnitude()
 *
 *      Finds the fraction of pixels with "color" that are not close to black
 *         l_int32    pixColorFraction()
 *
 *      Identifies images where color quantization will cause posterization
 *      due to the existence of many colors in low-gradient regions.
 *         l_int32    pixColorsForQuantization()
 *
 *      Finds the number of unique colors in an image
 *         l_int32    pixNumColors()
 *
 *  Color is tricky.  If we consider gray (r = g = b) to have no color
 *  content, how should we define the color content in each component
 *  of an arbitrary pixel, as well as the overall color magnitude?
 *
 *  I can think of three ways to define the color content in each component:
 *
 *  (1) Linear.  For each component, take the difference from the average
 *      of all three.
 *  (2) Linear.  For each component, take the difference from the average
 *      of the other two.
 *  (3) Nonlinear.  For each component, take the minimum of the differences
 *      from the other two.
 *
 *  How might one choose from among these?  Consider two different situations:
 *  (a) r = g = 0, b = 255            {255}   /255/
 *  (b) r = 0, g = 127, b = 255       {191}   /128/
 *  How much g is in each of these?  The three methods above give:
 *  (a)  1: 85   2: 127   3: 0        [85]
 *  (b)  1: 0    2: 0     3: 127      [0]
 *  How much b is in each of these?
 *  (a)  1: 170  2: 255   3: 255      [255]
 *  (b)  1: 127  2: 191   3: 127      [191]
 *  The number I'd "like" to give is in [].  (Please don't ask why, it's
 *  just a feeling.
 *
 *  So my preferences seem to be somewhere between (1) and (2).
 *  (3) is just too "decisive!"  Let's pick (2).
 *
 *  We also allow compensation for white imbalance.  For each
 *  component, we do a linear TRC (gamma = 1.0), where the black
 *  point remains at 0 and the white point is given by the input
 *  parameter.  This is equivalent to doing a global remapping,
 *  as with pixGlobalNormRGB(), followed by color content (or magnitude)
 *  computation, but without the overhead of first creating the
 *  white point normalized image.
 *
 *  Another useful property is the overall color magnitude in the pixel.
 *  For this there are again several choices, such as:
 *      (a) rms deviation from the mean
 *      (b) the average L1 deviation from the mean
 *      (c) the maximum (over components) of one of the color
 *          content measures given above.
 *
 *  For now, we will choose two of the methods in (c):
 *     L_MAX_DIFF_FROM_AVERAGE_2
 *        Define the color magnitude as the maximum over components
 *        of the difference between the component value and the
 *        average of the other two.  It is easy to show that
 *        this is equivalent to selecting the two component values
 *        that are closest to each other, averaging them, and
 *        using the distance from that average to the third component.
 *        For (a) and (b) above, this value is in {..}.
 *    L_MAX_MIN_DIFF_FROM_2
 *        Define the color magnitude as the maximum over components
 *        of the minimum difference between the component value and the
 *        other two values.  It is easy to show that this is equivalent
 *        to selecting the intermediate value of the three differences
 *        between the three components.  For (a) and (b) above,
 *        this value is in /../.
 */

#include <stdio.h>
#include <stdlib.h>
#include "allheaders.h"


/*!
 *  pixColorContent()
 *
 *      Input:  pixs  (32 bpp rgb or 8 bpp colormapped)
 *              rwhite, gwhite, bwhite (color value associated with white point)
 *              mingray (min gray value for which color is measured)
 *              &pixr (<optional return> 8 bpp red 'content')
 *              &pixg (<optional return> 8 bpp green 'content')
 *              &pixb (<optional return> 8 bpp blue 'content')
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) This returns the color content in each component, which is
 *          a measure of the deviation from gray, and is defined
 *          as the difference between the component and the average of
 *          the other two components.  See the discussion at the
 *          top of this file.
 *      (2) The three numbers (rwhite, gwhite and bwhite) can be thought
 *          of as the values in the image corresponding to white.
 *          They are used to compensate for an unbalanced color white point.
 *          They must either be all 0 or all non-zero.  To turn this
 *          off, set them all to 0.
 *      (3) If the maximum component after white point correction,
 *          max(r,g,b), is less than mingray, all color components
 *          for that pixel are set to zero.
 *          Use mingray = 0 to turn off this filtering of dark pixels.
 *      (4) Therefore, use 0 for all four input parameters if the color
 *          magnitude is to be calculated without either white balance
 *          correction or dark filtering.
 */
l_int32
pixColorContent(PIX     *pixs,
                l_int32  rwhite,
                l_int32  gwhite,
                l_int32  bwhite,
                l_int32  mingray,
                PIX    **ppixr,
                PIX    **ppixg,
                PIX    **ppixb)
{
l_int32    w, h, d, i, j, wplc, wplr, wplg, wplb;
l_int32    rval, gval, bval, rgdiff, rbdiff, gbdiff, maxval, colorval;
l_int32   *rtab, *gtab, *btab;
l_uint32   pixel;
l_uint32  *datac, *datar, *datag, *datab, *linec, *liner, *lineg, *lineb;
NUMA      *nar, *nag, *nab;
PIX       *pixc;   /* rgb */
PIX       *pixr, *pixg, *pixb;   /* 8 bpp grayscale */
PIXCMAP   *cmap;

    PROCNAME("pixColorContent");

    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    if (!ppixr && !ppixg && !ppixb)
        return ERROR_INT("nothing to compute", procName, 1);
    if (mingray < 0) mingray = 0;
    pixGetDimensions(pixs, &w, &h, &d);
    if (mingray > 255)
        return ERROR_INT("mingray > 255", procName, 1);
    if (rwhite < 0 || gwhite < 0 || bwhite < 0)
        return ERROR_INT("some white vals are negative", procName, 1);
    if ((rwhite || gwhite || bwhite) && (rwhite * gwhite * bwhite == 0))
        return ERROR_INT("white vals not all zero or all nonzero", procName, 1);

    cmap = pixGetColormap(pixs);
    if (!cmap && d != 32)
        return ERROR_INT("pixs neither cmapped nor 32 bpp", procName, 1);
    if (cmap)
        pixc = pixRemoveColormap(pixs, REMOVE_CMAP_TO_FULL_COLOR);
    else
        pixc = pixClone(pixs);

    pixr = pixg = pixb = NULL;
    w = pixGetWidth(pixc);
    h = pixGetHeight(pixc);
    if (ppixr) {
        pixr = pixCreate(w, h, 8);
        datar = pixGetData(pixr);
        wplr = pixGetWpl(pixr);
        *ppixr = pixr;
    }
    if (ppixg) {
        pixg = pixCreate(w, h, 8);
        datag = pixGetData(pixg);
        wplg = pixGetWpl(pixg);
        *ppixg = pixg;
    }
    if (ppixb) {
        pixb = pixCreate(w, h, 8);
        datab = pixGetData(pixb);
        wplb = pixGetWpl(pixb);
        *ppixb = pixb;
    }

    datac = pixGetData(pixc);
    wplc = pixGetWpl(pixc);
    if (rwhite) {  /* all white pt vals are nonzero */
        nar = numaGammaTRC(1.0, 0, rwhite);
        rtab = numaGetIArray(nar);
        nag = numaGammaTRC(1.0, 0, gwhite);
        gtab = numaGetIArray(nag);
        nab = numaGammaTRC(1.0, 0, bwhite);
        btab = numaGetIArray(nab);
    }
    for (i = 0; i < h; i++) {
        linec = datac + i * wplc;
        if (pixr)
            liner = datar + i * wplr;
        if (pixg)
            lineg = datag + i * wplg;
        if (pixb)
            lineb = datab + i * wplb;
        for (j = 0; j < w; j++) {
            pixel = linec[j];
            extractRGBValues(pixel, &rval, &gval, &bval);
            if (rwhite) {  /* color correct for white point */
                rval = rtab[rval];
                gval = gtab[gval];
                bval = btab[bval];
            }
            if (mingray > 0) {  /* dark pixels have no color value */
                maxval = L_MAX(rval, gval);
                maxval = L_MAX(maxval, bval);
                if (maxval < mingray)
                    continue;  /* colorval = 0 for each component */
            }
            rgdiff = L_ABS(rval - gval);
            rbdiff = L_ABS(rval - bval);
            gbdiff = L_ABS(gval - bval);
            if (pixr) {
                colorval = (rgdiff + rbdiff) / 2;
                SET_DATA_BYTE(liner, j, colorval);
            }
            if (pixg) {
                colorval = (rgdiff + gbdiff) / 2;
                SET_DATA_BYTE(lineg, j, colorval);
            }
            if (pixb) {
                colorval = (rbdiff + gbdiff) / 2;
                SET_DATA_BYTE(lineb, j, colorval);
            }
        }
    }

    if (rwhite) {
        numaDestroy(&nar);
        numaDestroy(&nag);
        numaDestroy(&nab);
        FREE(rtab);
        FREE(gtab);
        FREE(btab);
    }
    pixDestroy(&pixc);
    return 0;
}


/*!
 *  pixColorMagnitude()
 *
 *      Input:  pixs  (32 bpp rgb or 8 bpp colormapped)
 *              rwhite, gwhite, bwhite (color value associated with white point)
 *              type (chooses the method for calculating the color magnitude:
 *                    L_MAX_DIFF_FROM_AVERAGE_2, MAX_MIN_DIFF_FROM_2)
 *      Return: pixd (8 bpp, amount of color in each source pixel),
 *                    or NULL on error
 *
 *  Notes:
 *      (1) For an RGB image, a gray pixel is one where all three components
 *          are equal.  We define the amount of color in an RGB pixel by
 *          considering the absolute value of the differences between the
 *          components, of which there are three.  Consider the two largest
 *          of these differences.  The pixel component in common to these
 *          two differences is the color farthest from the other two.
 *          The color magnitude in an RGB pixel can be taken as:
 *              * the average of these two differences; i.e., the
 *                average distance from the two components that are
 *                nearest to each other to the third component, or
 *              * the minimum value of these two differences; i.e., the
 *                maximum over all components of the minimum distance
 *                from that component to the other two components.
 *      (2) As an example, suppose that R and G are the closest in
 *          magnitude.  Then the color is determined as either:
 *              * the average distance of B from these two; namely,
 *                (|B - R| + |B - G|) / 2, which can also be found
 *                from |B - (R + G) / 2|, or
 *              * the minimum distance of B from these two; namely,
 *                min(|B - R|, |B - G|).
 *      (3) The three numbers (rwhite, gwhite and bwhite) can be thought
 *          of as the values in the image corresponding to white.
 *          They are used to compensate for an unbalanced color white point.
 *          They must either be all 0 or all non-zero.  To turn this
 *          off, set them all to 0.
 *      (4) We allow the following methods for choosing the color
 *          magnitude from the three components:
 *              * L_MAX_DIFF_FROM_AVERAGE_2
 *              * L_MAX_MIN_DIFF_FROM_2
 *          These are described above in (1) and (2), as well as at
 *          the top of this file.
 */
PIX *
pixColorMagnitude(PIX     *pixs,
                  l_int32  rwhite,
                  l_int32  gwhite,
                  l_int32  bwhite,
                  l_int32  type)
{
l_int32    w, h, d, i, j, wplc, wpld;
l_int32    rval, gval, bval, rdist, gdist, bdist, colorval;
l_int32    rgdist, rbdist, gbdist, mindist, maxdist;
l_int32   *rtab, *gtab, *btab;
l_uint32   pixel;
l_uint32  *datac, *datad, *linec, *lined;
NUMA      *nar, *nag, *nab;
PIX       *pixc, *pixd;
PIXCMAP   *cmap;

    PROCNAME("pixColorMagnitude");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    pixGetDimensions(pixs, &w, &h, &d);
    if (type != L_MAX_DIFF_FROM_AVERAGE_2 && type != L_MAX_MIN_DIFF_FROM_2)
        return (PIX *)ERROR_PTR("invalid type", procName, NULL);
    if (rwhite < 0 || gwhite < 0 || bwhite < 0)
        return (PIX *)ERROR_PTR("some white vals are negative", procName, NULL);
    if ((rwhite || gwhite || bwhite) && (rwhite * gwhite * bwhite == 0))
        return (PIX *)ERROR_PTR("white vals not all zero or all nonzero",
                                procName, NULL);

    cmap = pixGetColormap(pixs);
    if (!cmap && d != 32)
        return (PIX *)ERROR_PTR("pixs not cmapped or 32 bpp", procName, NULL);
    if (cmap)
        pixc = pixRemoveColormap(pixs, REMOVE_CMAP_TO_FULL_COLOR);
    else
        pixc = pixClone(pixs);

    pixd = pixCreate(w, h, 8);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);
    datac = pixGetData(pixc);
    wplc = pixGetWpl(pixc);
    if (rwhite) {  /* all white pt vals are nonzero */
        nar = numaGammaTRC(1.0, 0, rwhite);
        rtab = numaGetIArray(nar);
        nag = numaGammaTRC(1.0, 0, gwhite);
        gtab = numaGetIArray(nag);
        nab = numaGammaTRC(1.0, 0, bwhite);
        btab = numaGetIArray(nab);
    }
    for (i = 0; i < h; i++) {
        linec = datac + i * wplc;
        lined = datad + i * wpld;
        for (j = 0; j < w; j++) {
            pixel = linec[j];
            extractRGBValues(pixel, &rval, &gval, &bval);
            if (rwhite) {  /* color correct for white point */
                rval = rtab[rval];
                gval = gtab[gval];
                bval = btab[bval];
            }
            if (type == L_MAX_DIFF_FROM_AVERAGE_2) {
                rdist = ((gval + bval ) / 2 - rval);
                rdist = L_ABS(rdist);
                gdist = ((rval + bval ) / 2 - gval);
                gdist = L_ABS(gdist);
                bdist = ((rval + gval ) / 2 - bval);
                bdist = L_ABS(bdist);
                colorval = L_MAX(rdist, gdist);
                colorval = L_MAX(colorval, bdist);
            }
            else {  /* type == L_MAX_MIN_DIFF_FROM_2; choose intermed dist */
                rgdist = L_ABS(rval - gval);
                rbdist = L_ABS(rval - bval);
                gbdist = L_ABS(gval - bval);
                maxdist = L_MAX(rgdist, rbdist);
                if (gbdist >= maxdist)
                    colorval = maxdist;
                else {  /* gbdist is smallest or intermediate */
                    mindist = L_MIN(rgdist, rbdist);
                    colorval = L_MAX(mindist, gbdist);
                }
            }
            SET_DATA_BYTE(lined, j, colorval);
        }
    }

    if (rwhite) {
        numaDestroy(&nar);
        numaDestroy(&nag);
        numaDestroy(&nab);
        FREE(rtab);
        FREE(gtab);
        FREE(btab);
    }
    pixDestroy(&pixc);
    return pixd;
}


/*!
 *  pixColorFraction()
 *
 *      Input:  pixs  (32 bpp rgb)
 *              darkthresh (dark threshold for minimum of average value to
 *                          be considered in the statistics; typ. 20)
 *              lightthresh (threshold near white, above which the pixel
 *                           is not considered in the statistics; typ. 248)
 *              diffthresh (thresh for the maximum difference from
 *                          the average of component values)
 *              factor (subsampling factor)
 *              &pixfract (<return> fraction of pixels in intermediate
 *                         brightness range that were considered
 *                         for color content)
 *              &colorfract (<return> fraction of pixels that meet the
 *                           criterion for sufficient color; 0.0 on error)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) This function is asking the question: to what extent does the
 *          image appear to have color?   The amount of color a pixel
 *          appears to have depends on both the deviation of the
 *          individual components from their average and on the average
 *          intensity itself.  For example, the color will be much more
 *          obvious with a small deviation from white than the same
 *          deviation from black.
 *      (2) Any pixel that meets these three tests is considered a
 *          colorful pixel:
 *            (a) the average of components must equal or exceed @darkthresh
 *            (b) the average of components must be not exceed @lightthresh
 *            (c) at least one component must differ from the average
 *                by at least @diffthresh
 *      (3) The dark pixels are removed from consideration because
 *          they don't appear to have color.
 *      (4) The very lightest pixels are removed because if an image
 *          has a lot of "white", the color fraction will be artificially
 *          low, even if all the other pixels are colorful.
 *      (5) If either pixfract or colorfract is very small, this
 *          indicates an image with little or no color.
 *      (6) One use of this function is as a preprocessing step for median
 *          cut quantization (colorquant2.c), which does a very poor job
 *          splitting the color space into rectangular volume elements when
 *          all the pixels are near the diagonal of the color cube.
 */
l_int32
pixColorFraction(PIX        *pixs,
                 l_int32     darkthresh,
                 l_int32     lightthresh,
                 l_int32     diffthresh,
                 l_int32     factor,
                 l_float32  *ppixfract,
                 l_float32  *pcolorfract)
{
l_int32    i, j, w, h, wpl, rval, gval, bval, ave;
l_int32    total, npix, ncolor, rdiff, gdiff, bdiff, maxdiff;
l_uint32   pixel;
l_uint32  *data, *line;

    PROCNAME("pixColorFraction");

    if (!ppixfract || !pcolorfract)
        return ERROR_INT("&pixfract and &colorfract not both defined",
                         procName, 1);
    *ppixfract = 0.0;
    *pcolorfract = 0.0;
    if (!pixs || pixGetDepth(pixs) != 32)
        return ERROR_INT("pixs not defined or not 32 bpp", procName, 1);

    pixGetDimensions(pixs, &w, &h, NULL);
    data = pixGetData(pixs);
    wpl = pixGetWpl(pixs);
    npix = ncolor = total = 0;
    for (i = 0; i < h; i += factor) {
        line = data + i * wpl;
        for (j = 0; j < w; j += factor) {
            total++;
            pixel = line[j];
            extractRGBValues(pixel, &rval, &gval, &bval);
            ave = (l_int32)(0.333 * (rval + gval + bval));
            if (ave < darkthresh || ave > lightthresh)
                continue;
            npix++;
            rdiff = L_ABS(rval - ave);
            gdiff = L_ABS(gval - ave);
            bdiff = L_ABS(bval - ave);
            maxdiff = L_MAX(rdiff, gdiff);
            maxdiff = L_MAX(maxdiff, bdiff);
            if (maxdiff >= diffthresh)
                ncolor++;
        }
    }

    if (npix == 0) {
        L_WARNING("No pixels found for consideration", procName);
        return 0;
    }
    *ppixfract = (l_float32)npix / (l_float32)total;
    *pcolorfract = (l_float32)ncolor / (l_float32)npix;
    return 0;
}


/*!
 *  pixColorsForQuantization()
 *      Input:  pixs (32 bpp rgb)
 *              thresh (binary threshold on edge gradient; 0 for default)
 *              &ncolors (<return> the number of colors found)
 *      Return: 0 if OK, 1 on error.
 *
 *  Notes:
 *      (1) Color quantization is often useful to achieve highly
 *          compressed images with little visible distortion.  However,
 *          color washes (regions of low gradient) can defeat this
 *          approach to high compression.  How can one determine if
 *          an image is expected to compress well using color quantization?
 *          We use the fact that color washes, when quantized with level 4
 *          octcubes, typically result in both posterization
 *          (visual boundaries between regions of uniform color) and
 *          the occupancy of many level 4 octcubes.
 *      (2) This function finds a measure of the number of colors that are
 *          found in low-gradient regions of an image.  By its
 *          magnitude relative to some threshold (not specified in
 *          this function), it gives a good indication of whether color
 *          quantization will generate posterization.   This number
 *          is larger for images with regions of slowly varying color.
 *          Such images, if color quantized, may require dithering 
 *          to avoid posterization, and lossless compression is then
 *          expected to be poor.
 *      (3) The number of colors returned increases monotonically with the
 *          threshold @thresh on the edge gradient.  In use, an
 *          input threshold is chosen.  The number of occupied level 4
 *          octubes is found, and if this is sufficiently large,
 *          quantization without dithering can be expected to have a
 *          poor visual result.
 *      (4) When using the default threshold on the gradient (15),
 *          images where ncolors is greater than about 25 will compress
 *          poorly with either lossless compression or dithered
 *          quantization, and they can expect to be posterized with
 *          non-dithered quantization.
 *      (5) An alternative method, which finds the actual number of
 *          different (r,g,b) colors in the low-gradient regions (rather than
 *          the number of occupied level 4 octcubes), does not
 *          discriminate well, because very small color changes
 *          (e.g., due to jpeg compression) will cause a large number
 *          of colors to be found, even for regions that are visually
 *          of a single color.
 */
l_int32
pixColorsForQuantization(PIX      *pixs,
                         l_int32   thresh,
                         l_int32  *pncolors)
{
PIX  *pixs2, *pixg2, *pixe2, *pixb2, *pixm2;

    PROCNAME("pixColorsForQuantization");

    if (!pncolors)
        return ERROR_INT("&ncolors not defined", procName, 1);
    *pncolors = 0;
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    if (pixGetDepth(pixs) != 32)
        return ERROR_INT("pixs not 32 bpp", procName, 1);
    if (thresh <= 0) 
        thresh = 15;  /* default */

        /* Scale down 2x; get edges on grayscale version;
         * binarize and dilate with a 7x7 brick Sel to get mask over
         * all pixels that are within a small distance from the
         * nearest edge pixel. */
    pixs2 = pixScaleAreaMap2(pixs);
    pixg2 = pixConvertRGBToLuminance(pixs2);
    pixe2 = pixSobelEdgeFilter(pixg2, L_ALL_EDGES);
    pixb2 = pixThresholdToBinary(pixe2, thresh);
    pixInvert(pixb2, pixb2);
    pixm2 = pixMorphSequence(pixb2, "d7.7", 0);

        /* Set all those pixels to white.  Then count the
         * number of occupied level 4 octcubes for the remaining pixels. */
    pixSetMasked(pixs2, pixm2, 0xffffffff);
    pixNumberOccupiedOctcubes(pixs2, 4, pncolors);

    pixDestroy(&pixs2);
    pixDestroy(&pixg2);
    pixDestroy(&pixe2);
    pixDestroy(&pixb2);
    pixDestroy(&pixm2);
    return 0;
}


/*!
 *  pixNumColors()
 *      Input:  pixs (2, 4, 8, 32 bpp)
 *              &ncolors (<return> the number of colors found, or 0 if
 *                        there are more than 256)
 *      Return: 0 if OK, 1 on error.
 *
 *  Notes:
 *      (1) This returns the actual number of colors found in the image,
 *          even if there is a colormap.  If the number of colors differs
 *          from the number of entries in the colormap, a warning is issued.
 *      (2) For d = 2, 4 or 8 bpp grayscale, this returns the number
 *          of colors found in the image in 'ncolors'.
 *      (3) For d = 32 bpp (rgb), if the number of colors is
 *          greater than 256, this returns 0 in 'ncolors'.
 */
l_int32
pixNumColors(PIX      *pixs,
             l_int32  *pncolors)
{
l_int32    w, h, d, i, j, wpl, hashsize, sum, count;
l_int32    rval, gval, bval, val;
l_int32   *inta;
l_uint32   pixel;
l_uint32  *data, *line;
PIXCMAP   *cmap;

    PROCNAME("pixNumColors");

    if (!pncolors)
        return ERROR_INT("&ncolors not defined", procName, 1);
    *pncolors = 0;
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 2 && d != 4 && d != 8 && d != 32)
        return ERROR_INT("d not in {2, 4, 8, 32}", procName, 1);

    data = pixGetData(pixs);
    wpl = pixGetWpl(pixs);
    sum = 0;
    if (d != 32) {  /* grayscale */
        inta = (l_int32 *)CALLOC(256, sizeof(l_int32));
        for (i = 0; i < h; i++) {
            line = data + i * wpl;
            for (j = 0; j < w; j++) {
                if (d == 8)
                    val = GET_DATA_BYTE(line, j);
                else if (d == 4)
                    val = GET_DATA_QBIT(line, j);
                else  /* d == 2 */
                    val = GET_DATA_DIBIT(line, j);
                inta[val] = 1;
            }
        }
        for (i = 0; i < 256; i++)
            if (inta[i]) sum++;
        *pncolors = sum;
        FREE(inta);

        if ((cmap = pixGetColormap(pixs)) != NULL) {
            count = pixcmapGetCount(cmap);
            if (sum != count) 
                L_WARNING_INT("colormap size %d differs from actual colors",
                              procName, count);
        }
        return 0;
    }

        /* 32 bpp rgb; quit if we get above 256 colors */
    hashsize = 5507;  /* big and prime; collisions are not likely */
    inta = (l_int32 *)CALLOC(hashsize, sizeof(l_int32));
    for (i = 0; i < h; i++) {
        line = data + i * wpl;
        for (j = 0; j < w; j++) {
            pixel = line[j];
            extractRGBValues(pixel, &rval, &gval, &bval);
            val = (137 * rval + 269 * gval + 353 * bval) % hashsize;
            if (inta[val] == 0) {
                inta[val] = 1;
                sum++;
                if (sum > 256) {
                    FREE(inta);
                    return 0;
                }
            }
        }
    }

    *pncolors = sum;
    FREE(inta);
    return 0;
}


