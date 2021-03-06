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
 *  maze.c
 *
 *      This is a game with a pedagogical slant.  A maze is represented
 *      by a binary image.  The ON pixels (fg) are walls.  The goal is
 *      to navigate on OFF pixels (bg), using Manhattan steps
 *      (N, S, E, W), between arbitrary start and end positions.
 *      The problem is thus to find the shortest route between two points
 *      in a binary image that are 4-connected in the bg.  This is done
 *      with a breadth-first search, implemented with a queue.
 *      We also use a queue of pointers to generate the maze (image).
 *
 *          PIX             *generateBinaryMaze()
 *          static MAZEEL   *mazeelCreate()
 *
 *          PIX             *searchBinaryMaze()
 *          static l_int32   localSearchForBackground()
 *
 *      Generalizing a maze to a grayscale image, the search is
 *      now for the "shortest" or least cost path, for some given
 *      cost function.
 *
 *          PIX             *searchGrayMaze()
 *
 *      Display functions
 *          PIX             *pixDisplayPta()
 *          PIX             *pixDisplayPtaa()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "allheaders.h"

static const l_int32  MIN_MAZE_WIDTH = 50;
static const l_int32  MIN_MAZE_HEIGHT = 50;

static const l_float32  DEFAULT_WALL_PROBABILITY = 0.65;
static const l_float32  DEFAULT_ANISOTROPY_RATIO = 0.25;

enum {  /* direction from parent to newly created element */
    START_LOC = 0,
    DIR_NORTH = 1,
    DIR_SOUTH = 2,
    DIR_WEST = 3,
    DIR_EAST = 4
};

struct MazeElement {
    l_float32  distance;
    l_int32    x;
    l_int32    y;
    l_uint32   val;  /* value of maze pixel at this location */
    l_int32    dir;  /* direction from parent to child */
};
typedef struct MazeElement  MAZEEL;


static MAZEEL *mazeelCreate(l_int32  x, l_int32  y, l_int32  dir);
static l_int32 localSearchForBackground(PIX  *pix, l_int32  *px,
                                        l_int32  *py, l_int32  maxrad);

#ifndef  NO_CONSOLE_IO
#define  DEBUG_PATH    0
#define  DEBUG_MAZE    0
#endif  /* ~NO_CONSOLE_IO */


/*---------------------------------------------------------------------*
 *             Binary maze generation as cellular automaton            *
 *---------------------------------------------------------------------*/
/*!
 *  generateBinaryMaze()
 *
 *      Input:  w, h  (size of maze)
 *              xi, yi  (initial location)
 *              wallps (probability that a pixel to the side is ON)
 *              ranis (ratio of prob that pixel in forward direction
 *                     is a wall to the probability that pixel in
 *                     side directions is a wall)
 *      Return: pix, or null on error
 *
 *  Notes:
 *      (1) We have two input probability factors that determine the
 *          density of walls and average length of straight passages.
 *          When ranis < 1.0, you are more likely to generate a wall
 *          to the side than going forward.  Enter 0.0 for either if
 *          you want to use the default values.
 *      (2) This is a type of percolation problem, and exhibits
 *          different phases for different parameters wallps and ranis.
 *          For larger values of these parameters, regions in the maze
 *          are not explored because the maze generator walls them
 *          off and cannot get through.  The boundary between the
 *          two phases in this two-dimensional parameter space goes
 *          near these values:
 *                wallps       ranis
 *                0.35         1.00
 *                0.40         0.85
 *                0.45         0.70
 *                0.50         0.50
 *                0.55         0.40
 *                0.60         0.30
 *                0.65         0.25
 *                0.70         0.19
 *                0.75         0.15
 *                0.80         0.11
 *      (3) Because here is a considerable amount of overhead in calling
 *          pixGetPixel() and pixSetPixel(), this function can be sped
 *          up with little effort using raster line pointers and the
 *          GET_DATA* and SET_DATA* macros.
 */
PIX *
generateBinaryMaze(l_int32  w,
                   l_int32  h,
                   l_int32  xi,
                   l_int32  yi,
                   l_float32  wallps,
                   l_float32  ranis)
{
l_int32    x, y, dir;
l_uint32   val;
l_float32  frand, wallpf, testp;
MAZEEL    *el, *elp;
PIX       *pixd;  /* the destination maze */
PIX       *pixm;  /* for bookkeeping, to indicate pixels already visited */
PQUEUE    *pq;

    if (w < MIN_MAZE_WIDTH)
        w = MIN_MAZE_WIDTH;
    if (h < MIN_MAZE_HEIGHT)
        h = MIN_MAZE_HEIGHT;
    if (xi <= 0 || xi >= w)
        xi = w / 6;
    if (yi <= 0 || yi >= h)
        yi = h / 5;
    if (wallps < 0.05 || wallps > 0.95)
        wallps = DEFAULT_WALL_PROBABILITY;
    if (ranis < 0.05 || ranis > 1.0)
        ranis = DEFAULT_ANISOTROPY_RATIO;
    wallpf = wallps * ranis;

#if  DEBUG_MAZE
    fprintf(stderr, "(w, h) = (%d, %d), (xi, yi) = (%d, %d)\n", w, h, xi, yi);
    fprintf(stderr, "Using: prob(wall) = %7.4f, anisotropy factor = %7.4f\n",
            wallps, ranis);
#endif  /* DEBUG_MAZE */

        /* these are initialized to OFF */
    pixd = pixCreate(w, h, 1);
    pixm = pixCreate(w, h, 1);

    pq = pqueueCreate(0);

        /* prime the queue with the first pixel; it is OFF */
    el = mazeelCreate(xi, yi, START_LOC);
    pixSetPixel(pixm, xi, yi, 1);  /* mark visited */
    pqueueAdd(pq, el);

        /* while we're at it ... */
    while (pqueueGetCount(pq) > 0) {
        elp = (MAZEEL *)pqueueRemove(pq);
        x = elp->x;
        y = elp->y;
        dir = elp->dir;
        if (x > 0) {  /* check west */
            pixGetPixel(pixm, x - 1, y, &val);
            if (val == 0) {  /* not yet visited */
                pixSetPixel(pixm, x - 1, y, 1);  /* mark visited */
                frand = (l_float32)rand() / (l_float32)RAND_MAX;
                testp = wallps;
                if (dir == DIR_WEST)
                    testp = wallpf;
                if (frand <= testp) {  /* make it a wall */
                    pixSetPixel(pixd, x - 1, y, 1);
                }
                else {  /* not a wall */
                    el = mazeelCreate(x - 1, y, DIR_WEST);
                    pqueueAdd(pq, el);
                }
            }
        }
        if (y > 0) {  /* check north */
            pixGetPixel(pixm, x, y - 1, &val);
            if (val == 0) {  /* not yet visited */
                pixSetPixel(pixm, x, y - 1, 1);  /* mark visited */
                frand = (l_float32)rand() / (l_float32)RAND_MAX;
                testp = wallps;
                if (dir == DIR_NORTH)
                    testp = wallpf;
                if (frand <= testp) {  /* make it a wall */
                    pixSetPixel(pixd, x, y - 1, 1);
                }
                else {  /* not a wall */
                    el = mazeelCreate(x, y - 1, DIR_NORTH);
                    pqueueAdd(pq, el);
                }
            }
        }
        if (x < w - 1) {  /* check east */
            pixGetPixel(pixm, x + 1, y, &val);
            if (val == 0) {  /* not yet visited */
                pixSetPixel(pixm, x + 1, y, 1);  /* mark visited */
                frand = (l_float32)rand() / (l_float32)RAND_MAX;
                testp = wallps;
                if (dir == DIR_EAST)
                    testp = wallpf;
                if (frand <= testp) {  /* make it a wall */
                    pixSetPixel(pixd, x + 1, y, 1);
                }
                else {  /* not a wall */
                    el = mazeelCreate(x + 1, y, DIR_EAST);
                    pqueueAdd(pq, el);
                }
            }
        }
        if (y < h - 1) {  /* check south */
            pixGetPixel(pixm, x, y + 1, &val);
            if (val == 0) {  /* not yet visited */
                pixSetPixel(pixm, x, y + 1, 1);  /* mark visited */
                frand = (l_float32)rand() / (l_float32)RAND_MAX;
                testp = wallps;
                if (dir == DIR_SOUTH)
                    testp = wallpf;
                if (frand <= testp) {  /* make it a wall */
                    pixSetPixel(pixd, x, y + 1, 1);
                }
                else {  /* not a wall */
                    el = mazeelCreate(x, y + 1, DIR_SOUTH);
                    pqueueAdd(pq, el);
                }
            }
        }
        FREE(elp);
    }

    pqueueDestroy(&pq, TRUE);
    pixDestroy(&pixm);
    return pixd;
}


static MAZEEL *
mazeelCreate(l_int32  x,
             l_int32  y,
             l_int32  dir)
{
MAZEEL *el;
  
    el = (MAZEEL *)CALLOC(1, sizeof(MAZEEL));
    el->x = x;
    el->y = y;
    el->dir = dir;
    return el;
}


/*---------------------------------------------------------------------*
 *                           Binary maze search                        *
 *---------------------------------------------------------------------*/
/*!
 *  searchBinaryMaze()
 *
 *      Input:  pixs (1 bpp, maze)
 *              xi, yi  (beginning point; use same initial point
 *                       that was used to generate the maze)
 *              xf, yf  (end point, or close to it)
 *              &ppixd (<optional return> maze with path illustrated, or
 *                     if no path possible, the part of the maze
 *                     that was searched)
 *      Return: pta (shortest path), or null if either no path
 *              exists or on error
 *
 *  Note: Because here is a considerable amount of overhead in calling
 *        pixGetPixel() and pixSetPixel(), this function can be sped
 *        up with little effort using raster line pointers and the
 *        GET_DATA* and SET_DATA* macros.
 *
 *  Commentary:
 *      The goal is to find the shortest path between beginning and
 *      end points, without going through walls, and there are many
 *      ways to solve this problem.
 *
 *      We use a queue to implement a breadth-first search.  Two auxiliary
 *      "image" data structures can be used: one to mark the visited
 *      pixels and one to give the direction to the parent for each
 *      visited pixels.  The first structure is used to avoid putting
 *      pixels on the queue more than once, and the second is used
 *      for retracing back to the origin, like the breadcrumbs in
 *      Hansel and Gretel.  Each pixel taken off the queue is destroyed
 *      after it is used to locate the allowed neighbors.  In fact,
 *      only one distance image is required, if you initialize it
 *      to some value that signifies "not yet visited."  (We use
 *      a binary image for marking visited pixels because it is clearer.)
 *      This method for a simple search of a binary maze is implemented in
 *      searchBinaryMaze().
 *
 *      An alternative method would store the (manhattan) distance from
 *      the start point with each pixel on the queue.  The children
 *      of each pixel get a distance one larger than the parent.  These
 *      values can be stored in an auxiliary distance map image
 *      that is constructed simultaneously with the search.  Once the
 *      end point is reached, the distance map is used to backtrack
 *      along a minimum path.  There may be several equal length
 *      minimum paths, any one of which can be chosen this way.
 *
 */
PTA *
searchBinaryMaze(PIX     *pixs,
                 l_int32  xi,
                 l_int32  yi, 
                 l_int32  xf,
                 l_int32  yf,
                 PIX    **ppixd)
{
l_int32    i, j, x, y, w, h, found;
l_uint32   val, rpixel, gpixel, bpixel;
MAZEEL    *el, *elp;
PIX       *pixd;  /* the shortest path written on the maze image */
PIX       *pixm;  /* for bookkeeping, to indicate pixels already visited */
PIX       *pixp;  /* for bookkeeping, to indicate direction to parent */
PQUEUE    *pq;
PTA       *pta;

    PROCNAME("searchBinaryMaze");

    if (ppixd) *ppixd = NULL;
    if (!pixs)
        return (PTA *)ERROR_PTR("pixs not defined", procName, NULL);
    if (pixGetDepth(pixs) != 1)
        return (PTA *)ERROR_PTR("pixs not 1 bpp", procName, NULL);
    w = pixGetWidth(pixs);
    h = pixGetHeight(pixs);
    if (xi <= 0 || xi >= w)
        return (PTA *)ERROR_PTR("xi not valid", procName, NULL);
    if (yi <= 0 || yi >= h)
        return (PTA *)ERROR_PTR("yi not valid", procName, NULL);
    pixGetPixel(pixs, xi, yi, &val);
    if (val != 0)
        return (PTA *)ERROR_PTR("(xi,yi) not bg pixel", procName, NULL);
    pixd = NULL;
    pta = NULL;

        /* find a bg pixel near input point (xf, yf) */
    localSearchForBackground(pixs, &xf, &yf, 5);

#if  DEBUG_MAZE
    fprintf(stderr, "(xi, yi) = (%d, %d), (xf, yf) = (%d, %d)\n",
            xi, yi, xf, yf);
#endif  /* DEBUG_MAZE */

    pixm = pixCreate(w, h, 1);  /* initialized to OFF */
    pixp = pixCreate(w, h, 8);  /* direction to parent stored as enum val */

    pq = pqueueCreate(0);

        /* prime the queue with the first pixel; it is OFF */
    el = mazeelCreate(xi, yi, 0);  /* don't need direction here */
    pixSetPixel(pixm, xi, yi, 1);  /* mark visited */
    pqueueAdd(pq, el);

        /* fill up the pix storing directions to parents,
         * stopping when we hit the point (xf, yf)  */
    found = FALSE;
    while (pqueueGetCount(pq) > 0) {
        elp = (MAZEEL *)pqueueRemove(pq);
        x = elp->x;
        y = elp->y;
        if (x == xf && y == yf) {
            found = TRUE;
            FREE(elp);
            break;
        }
            
        if (x > 0) {  /* check to west */
            pixGetPixel(pixm, x - 1, y, &val);
            if (val == 0) {  /* not yet visited */
                pixSetPixel(pixm, x - 1, y, 1);  /* mark visited */
                pixGetPixel(pixs, x - 1, y, &val);
                if (val == 0) {  /* bg, not a wall */
                    pixSetPixel(pixp, x - 1, y, DIR_EAST);  /* parent to E */
                    el = mazeelCreate(x - 1, y, 0);
                    pqueueAdd(pq, el);
                }
            }
        }
        if (y > 0) {  /* check north */
            pixGetPixel(pixm, x, y - 1, &val);
            if (val == 0) {  /* not yet visited */
                pixSetPixel(pixm, x, y - 1, 1);  /* mark visited */
                pixGetPixel(pixs, x, y - 1, &val);
                if (val == 0) {  /* bg, not a wall */
                    pixSetPixel(pixp, x, y - 1, DIR_SOUTH);  /* parent to S */
                    el = mazeelCreate(x, y - 1, 0);
                    pqueueAdd(pq, el);
                }
            }
        }
        if (x < w - 1) {  /* check east */
            pixGetPixel(pixm, x + 1, y, &val);
            if (val == 0) {  /* not yet visited */
                pixSetPixel(pixm, x + 1, y, 1);  /* mark visited */
                pixGetPixel(pixs, x + 1, y, &val);
                if (val == 0) {  /* bg, not a wall */
                    pixSetPixel(pixp, x + 1, y, DIR_WEST);  /* parent to W */
                    el = mazeelCreate(x + 1, y, 0);
                    pqueueAdd(pq, el);
                }
            }
        }
        if (y < h - 1) {  /* check south */
            pixGetPixel(pixm, x, y + 1, &val);
            if (val == 0) {  /* not yet visited */
                pixSetPixel(pixm, x, y + 1, 1);  /* mark visited */
                pixGetPixel(pixs, x, y + 1, &val);
                if (val == 0) {  /* bg, not a wall */
                    pixSetPixel(pixp, x, y + 1, DIR_NORTH);  /* parent to N */
                    el = mazeelCreate(x, y + 1, 0);
                    pqueueAdd(pq, el);
                }
            }
        }
        FREE(elp);
    }

    pqueueDestroy(&pq, TRUE);
    pixDestroy(&pixm);

    if (ppixd) {
        pixd = pixUnpackBinary(pixs, 32, 1);
        *ppixd = pixd;
    }
    composeRGBPixel(255, 0, 0, &rpixel);  /* start point */
    composeRGBPixel(0, 255, 0, &gpixel);
    composeRGBPixel(0, 0, 255, &bpixel);  /* end point */

    if (!found) {
        L_INFO(" No path found", procName);
        if (pixd) {  /* paint all visited locations */
            for (i = 0; i < h; i++) {
                for (j = 0; j < w; j++) {
                    pixGetPixel(pixp, j, i, &val);
                    if (val != 0 && pixd)
                        pixSetPixel(pixd, j, i, gpixel);
                }
            }
        }
    }
    else {   /* write path onto pixd */
        L_INFO(" Path found", procName);
        pta = ptaCreate(0);
        x = xf;
        y = yf;
        while (1) {
            ptaAddPt(pta, x, y);
            if (x == xi && y == yi)
                break;
            if (pixd)
                pixSetPixel(pixd, x, y, gpixel);
            pixGetPixel(pixp, x, y, &val);
            if (val == DIR_NORTH)
                y--;
            else if (val == DIR_SOUTH)
                y++;
            else if (val == DIR_EAST)
                x++;
            else if (val == DIR_WEST)
                x--;
        }
    }
    if (pixd) {
        pixSetPixel(pixd, xi, yi, rpixel);
        pixSetPixel(pixd, xf, yf, bpixel);
    }

    pixDestroy(&pixp);
    return pta;
}


/*!
 *  localSearchForBackground()
 *
 *      Input:  &x, &y (starting position for search; return found position)
 *              maxrad (max distance to search from starting location)
 *      Return: 0 if bg pixel found; 1 if not found
 */
static l_int32
localSearchForBackground(PIX  *pix,
                         l_int32  *px,
                         l_int32  *py,
                         l_int32  maxrad)
{
l_int32   x, y, w, h, r, i, j;
l_uint32  val;

    x = *px;
    y = *py;
    pixGetPixel(pix, x, y, &val);
    if (val == 0) return 0;

        /* For each value of r, restrict the search to the boundary
         * pixels in a square centered on (x,y), clipping to the
         * image boundaries if necessary.  */
    w = pixGetWidth(pix);
    h = pixGetHeight(pix);
    for (r = 1; r < maxrad; r++) {
        for (i = -r; i <= r; i++) {
            if (y + i < 0 || y + i >= h)
                continue;
            for (j = -r; j <= r; j++) {
                if (x + j < 0 || x + j >= w)
                    continue;
                if (L_ABS(i) != r && L_ABS(j) != r)  /* not on "r ring" */
                    continue;
                pixGetPixel(pix, x + j, y + i, &val);
                if (val == 0) {
                    *px = x + j;
                    *py = y + i;
                    return 0;
                }
            }
        }
    }
    return 1;
}



/*---------------------------------------------------------------------*
 *                            Gray maze search                         *
 *---------------------------------------------------------------------*/
/*!
 *  searchGrayMaze()
 *
 *      Input:  pixs (1 bpp, maze)
 *              xi, yi  (beginning point; use same initial point
 *                       that was used to generate the maze)
 *              xf, yf  (end point, or close to it)
 *              &ppixd (<optional return> maze with path illustrated, or
 *                     if no path possible, the part of the maze
 *                     that was searched)
 *      Return: pta (shortest path), or null if either no path
 *              exists or on error
 *
 *  Note: Because here is a considerable amount of overhead in calling
 *        pixGetPixel() and pixSetPixel(), this function can be sped
 *        up with little effort using raster line pointers and the
 *        GET_DATA* and SET_DATA* macros.
 *
 *  Commentary:
 *      Consider first a slight generalization of the binary maze
 *      search problem.  Suppose that you can go through walls,
 *      but the cost is higher (say, an increment of 3 to go into
 *      a wall pixel rather than 1)?  You're still trying to find
 *      the shortest path.  One way to do this is with an ordered
 *      queue, and a simple way to visualize an ordered queue is as 
 *      a set of stacks, each stack being marked with the distance
 *      of each pixel in the stack from the start.  We place the
 *      start pixel in stack 0, pop it, and process its 4 children.
 *      Each pixel is given a distance that is incremented from that
 *      of its parent (0 in this case), depending on if it is a wall
 *      pixel or not.  That value may be recorded on a distance map,
 *      according to the algorithm below.  For children of the first
 *      pixel, those not on a wall go in stack 1, and wall
 *      children go in stack 3.  Stack 0 being emptied, the process
 *      then continues with pixels being popped from stack 1.
 *      Here is the algorithm for each child pixel.  The pixel's
 *      distance value, were it to be placed on a stack, is compared
 *      with the value for it that is on the distance map.  There
 *      are three possible cases:
 *         (1) If the pixel has not yet been registered, it is pushed
 *             on its stack and the distance is written to the map.
 *         (2) If it has previously been registered with a higher distance,
 *             the distance on the map is relaxed to that of the
 *             current pixel, which is then placed on its stack.
 *         (3) If it has previously been registered with an equal
 *             or lower value, the pixel is discarded.
 *      The pixels are popped and processed successively from
 *      stack 1, and when stack 1 is empty, popping starts on stack 2.
 *      This continues until the destination pixel is popped off
 *      a stack.   The minimum path is then derived from the distance map,
 *      going back from the end point as before.  This is just Dijkstra's
 *      algorithm for a directed graph; here, the underlyaing graph
 *      (consisting of the pixels and four edges connecting each pixel
 *      to its 4-neighbor) is a special case of a directed graph, where
 *      each edge is bi-directional.  The implementation of this generalized
 *      maze search is left as an exercise to the reader.
 *
 *      Let's generalize a bit further.  Suppose the "maze" is just
 *      a grayscale image -- think of it as an elevation map.  The cost
 *      of moving on this surface depends on the height, or the gradient,
 *      or whatever you want.  All that is required is that the cost
 *      is specified and non-negative on each link between adjacent
 *      pixels.  Now the problem becomes: find the least cost path
 *      moving on this surface between two specified end points.
 *      For example, if the cost across an edge between two pixels
 *      depends on the "gradient", you can use:
 *           cost = 1 + L_ABS(deltaV)
 *      where deltaV is the difference in value between two adjacent
 *      pixels.  If the costs are all integers, we can still use an array
 *      of stacks to avoid ordering the queue (e.g., by using a heap sort.)
 *      This is a neat problem, because you don't even have to build a
 *      maze -- you can can use it on any grayscale image!
 *    
 *      Rather than using an array of stacks, a more practical
 *      approach is to implement with a priority queue, which is
 *      a queue that is sorted so that the elements with the largest
 *      (or smallest) key values always comes off first.  The
 *      priority queue is efficiently implemented as a heap, and
 *      this is how we do it.  Suppose you run the algorithm
 *      using a priority queue, doing the bookkeeping with an
 *      auxiliary image data structure that saves the distance of
 *      each pixel put on the queue as before, according to the method
 *      described above.  We implement it as a 2-way choice by
 *      initializing the distance array to a large value and putting
 *      a pixel on the queue if its distance is less than the value
 *      found on the array.  When you finally pop the end pixel from
 *      the queue, you're done, and you can trace the path backward,
 *      either always going downhill or using an auxiliary image to
 *      give you the direction to go at each step.  This is implemented
 *      here in searchGrayMaze().
 *
 *      Do we really have to use a sorted queue?  Can we solve this
 *      generalized maze with an unsorted queue of pixels?  (Or even
 *      an unsorted stack, doing a depth-first search?)
 *      Consider a different algorithm for this generalized maze, where
 *      we travel again breadth first, but this time use a single,
 *      unsorted queue.  An auxiliary image is used as before to
 *      store the distances and to determine if pixels get pushed
 *      on the stack or dropped.  As before, we must allow pixels
 *      to be revisited, with relaxation of the distance if a shorter
 *      path arrives later.  As a result, we will in general have
 *      multiple instances of the same pixel on the stack with different
 *      distances.  However, because the queue is not ordered, some of
 *      these pixels will be popped when another instance with a lower
 *      distance is still on the stack.  Here, we're just popping them
 *      in the order they go on, rather than setting up a priority
 *      based on minimum distance.  Thus, unlike the priority queue,
 *      when a pixel is popped we have to check the distance map to
 *      see if a pixel with a lower distance has been put on the queue,
 *      and, if so, we discard the pixel we just popped.  So the
 *      "while" loop looks like this:
 *        - pop a pixel from the queue
 *        - check its distance against the distance stored in the
 *          distance map; if larger, discard
 *        - otherwise, for each of its neighbors:
 *            - compute its distance from the start pixel
 *            - compare this distance with that on the distance map:
 *                - if the distance map value higher, relax the distance
 *                  and push the pixel on the queue
 *                - if the distance map value is lower, discard the pixel
 *
 *      How does this loop terminate?  Before, with an ordered queue,
 *      it terminates when you pop the end pixel.  But with an unordered
 *      queue (or stack), the first time you hit the end pixel, the
 *      distance is not guaranteed to be correct, because the pixels
 *      along the shortest path may not have yet been visited and relaxed.
 *      Because the shortest path can theoretically go anywhere,
 *      we must keep going.  How do we know when to stop?   Dijkstra
 *      uses an ordered queue to systematically remove nodes from
 *      further consideration.  With an unordered queue, the brute
 *      force answer is: stop when the queue (or stack) is empty,
 *      because then every pixel in the image has been assigned its
 *      minimum "distance" from the start pixel.
 *
 *      But surely, you ask, can't we stop sooner?  What if the
 *      start and end pixels are very close to each other?
 *      OK, suppose they are, and you have very high walls and a
 *      long snaking level path that is actually the minimum cost.
 *      That long path can wind back and forth across the entire
 *      maze many times before ending up at the end point, which
 *      could be just over a wall from the start.  With the unordered
 *      queue, you very quickly get a high distance for the end
 *      pixel, which will be relaxed to the minimum distance only
 *      after all the pixels of the path have been visited, many
 *      of them multiple times.  So that's the price for not ordering
 *      the queue!
 */
PTA *
searchGrayMaze(PIX     *pixs,
               l_int32  xi,
               l_int32  yi, 
               l_int32  xf,
               l_int32  yf,
               PIX    **ppixd)
{
l_int32   x, y, w, h;
l_uint32  val, valr, vals, rpixel, gpixel, bpixel;
l_int32   cost, dist, distparent, sival, sivals;
MAZEEL   *el, *elp;
PIX      *pixd;  /* optionally plot the path on this RGB version of pixs */
PIX      *pixr;  /* for bookkeeping, to indicate the minimum distance */
                 /* to pixels already visited */
PIX      *pixp;  /* for bookkeeping, to indicate direction to parent */
PHEAP    *ph;
PTA      *pta;

    PROCNAME("searchGrayMaze");

    if (ppixd) *ppixd = NULL;
    if (!pixs)
        return (PTA *)ERROR_PTR("pixs not defined", procName, NULL);
    if (pixGetDepth(pixs) != 8)
        return (PTA *)ERROR_PTR("pixs not 8 bpp", procName, NULL);
    w = pixGetWidth(pixs);
    h = pixGetHeight(pixs);
    if (xi <= 0 || xi >= w)
        return (PTA *)ERROR_PTR("xi not valid", procName, NULL);
    if (yi <= 0 || yi >= h)
        return (PTA *)ERROR_PTR("yi not valid", procName, NULL);
    pixd = NULL;
    pta = NULL;

    pixr = pixCreate(w, h, 32);
    pixSetAll(pixr);  /* initialize to max value */
    pixp = pixCreate(w, h, 8);  /* direction to parent stored as enum val */

    ph = pheapCreate(0, L_SORT_INCREASING);  /* always remove closest pixels */

        /* prime the heap with the first pixel */
    pixGetPixel(pixs, xi, yi, &val);
    el = mazeelCreate(xi, yi, 0);  /* don't need direction here */
    el->distance = 0;
    pixGetPixel(pixs, xi, yi, &val);
    el->val = val;
    pixSetPixel(pixr, xi, yi, 0);  /* distance is 0 */
    pheapAdd(ph, el);

        /* breadth-first search with priority queue (implemented by
           a heap), labeling direction to parents in pixp and minimum
           distance to visited pixels in pixr.  Stop when we pull
           the destination point (xf, yf) off the queue. */
    while (pheapGetCount(ph) > 0) {
        elp = (MAZEEL *)pheapRemove(ph);
        if (!elp)
            return (PTA *)ERROR_PTR("heap broken!!", procName, NULL);
        x = elp->x;
        y = elp->y;
        if (x == xf && y == yf) {  /* exit condition */
            FREE(elp);
            break;
        }
        distparent = (l_int32)elp->distance;
        val = elp->val;
        sival = val;
            
        if (x > 0) {  /* check to west */
            pixGetPixel(pixs, x - 1, y, &vals);
            pixGetPixel(pixr, x - 1, y, &valr);
            sivals = (l_int32)vals;
            cost = 1 + L_ABS(sivals - sival);  /* cost to move to this pixel */
            dist = distparent + cost;
            if (dist < valr) {  /* shortest path so far to this pixel */
                pixSetPixel(pixr, x - 1, y, dist);  /* new distance */
                pixSetPixel(pixp, x - 1, y, DIR_EAST);  /* parent to E */
                el = mazeelCreate(x - 1, y, 0);
                el->val = vals;
                el->distance = dist;
                pheapAdd(ph, el);
            }
        }
        if (y > 0) {  /* check north */
            pixGetPixel(pixs, x, y - 1, &vals);
            pixGetPixel(pixr, x, y - 1, &valr);
            sivals = (l_int32)vals;
            cost = 1 + L_ABS(sivals - sival);  /* cost to move to this pixel */
            dist = distparent + cost;
            if (dist < valr) {  /* shortest path so far to this pixel */
                pixSetPixel(pixr, x, y - 1, dist);  /* new distance */
                pixSetPixel(pixp, x, y - 1, DIR_SOUTH);  /* parent to S */
                el = mazeelCreate(x, y - 1, 0);
                el->val = vals;
                el->distance = dist;
                pheapAdd(ph, el);
            }
        }
        if (x < w - 1) {  /* check east */
            pixGetPixel(pixs, x + 1, y, &vals);
            pixGetPixel(pixr, x + 1, y, &valr);
            sivals = (l_int32)vals;
            cost = 1 + L_ABS(sivals - sival);  /* cost to move to this pixel */
            dist = distparent + cost;
            if (dist < valr) {  /* shortest path so far to this pixel */
                pixSetPixel(pixr, x + 1, y, dist);  /* new distance */
                pixSetPixel(pixp, x + 1, y, DIR_WEST);  /* parent to W */
                el = mazeelCreate(x + 1, y, 0);
                el->val = vals;
                el->distance = dist;
                pheapAdd(ph, el);
            }
        }
        if (y < h - 1) {  /* check south */
            pixGetPixel(pixs, x, y + 1, &vals);
            pixGetPixel(pixr, x, y + 1, &valr);
            sivals = (l_int32)vals;
            cost = 1 + L_ABS(sivals - sival);  /* cost to move to this pixel */
            dist = distparent + cost;
            if (dist < valr) {  /* shortest path so far to this pixel */
                pixSetPixel(pixr, x, y + 1, dist);  /* new distance */
                pixSetPixel(pixp, x, y + 1, DIR_NORTH);  /* parent to N */
                el = mazeelCreate(x, y + 1, 0);
                el->val = vals;
                el->distance = dist;
                pheapAdd(ph, el);
            }
        }
        FREE(elp);
    }

    pheapDestroy(&ph, TRUE);

    if (ppixd) {
        pixd = pixConvert8To32(pixs);
        *ppixd = pixd;
    }
    composeRGBPixel(255, 0, 0, &rpixel);  /* start point */
    composeRGBPixel(0, 255, 0, &gpixel);
    composeRGBPixel(0, 0, 255, &bpixel);  /* end point */

    x = xf;
    y = yf;
    pta = ptaCreate(0);
    while (1) {
        ptaAddPt(pta, x, y);
        if (x == xi && y == yi)
            break;
        if (pixd)
            pixSetPixel(pixd, x, y, gpixel);
        pixGetPixel(pixp, x, y, &val);
        if (val == DIR_NORTH)
            y--;
        else if (val == DIR_SOUTH)
            y++;
        else if (val == DIR_EAST)
            x++;
        else if (val == DIR_WEST)
            x--;
        pixGetPixel(pixr, x, y, &val);

#if  DEBUG_PATH
        fprintf(stderr, "(x,y) = (%d, %d); dist = %d\n", x, y, val);
#endif  /* DEBUG_PATH */

    }
    if (pixd) {
        pixSetPixel(pixd, xi, yi, rpixel);
        pixSetPixel(pixd, xf, yf, bpixel);
    }

    pixDestroy(&pixp);
    pixDestroy(&pixr);
    return pta;
}


/*---------------------------------------------------------------------*
 *                            Display Path(s)                          *
 *---------------------------------------------------------------------*/
/*!
 *  pixDisplayPta()
 *
 *      Input:  pixs (1, 2, 4, 8, 16 or 32 bpp)
 *              pta (of path to be plotted)
 *      Return: pixd (32 bpp RGB version of pixs, with path in green),
 *              or null on error
 */
PIX *
pixDisplayPta(PIX  *pixs,
              PTA  *pta)
{
l_int32   i, n, x, y;
l_uint32  rpixel, gpixel, bpixel;
PIX      *pixd;

    PROCNAME("pixDisplayPta");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (!pta)
        return (PIX *)ERROR_PTR("pta not defined", procName, NULL);

    if ((pixd = pixConvertTo32(pixs)) == NULL) 
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    composeRGBPixel(255, 0, 0, &rpixel);  /* start point */
    composeRGBPixel(0, 255, 0, &gpixel);
    composeRGBPixel(0, 0, 255, &bpixel);  /* end point */

    n = ptaGetCount(pta);
    for (i = 0; i < n; i++) {
        ptaGetIPt(pta, i, &x, &y);
        if (i == 0)
            pixSetPixel(pixd, x, y, rpixel);
        else if (i < n - 1)
            pixSetPixel(pixd, x, y, gpixel);
        else
            pixSetPixel(pixd, x, y, bpixel);
    }

    return pixd;
}


/*!
 *  pixDisplayPtaa()
 *
 *      Input:  pixs (1, 2, 4, 8, 16 or 32 bpp)
 *              ptaa (array of paths to be plotted)
 *      Return: pixd (32 bpp RGB version of pixs, with paths plotted
 *                    in different colors), or null on error
 */
PIX *
pixDisplayPtaa(PIX   *pixs,
               PTAA  *ptaa)
{
l_int32    i, j, npta, npt, x, y;
l_int32    rv, gv, bv;
l_uint32  *pixela;
PIX       *pixd;
PTA       *pta;

    PROCNAME("pixDisplayPtaa");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (!ptaa)
        return (PIX *)ERROR_PTR("ptaa not defined", procName, NULL);
    npta = ptaaGetCount(ptaa);
    if (npta == 0)
        return (PIX *)ERROR_PTR("no pta", procName, NULL);

    if ((pixd = pixConvertTo32(pixs)) == NULL) 
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);

        /* make a colormap for the paths; this one approximates
         * three functions that are linear for each color over
         * half the number of paths. */
    pixela = (l_uint32 *)CALLOC(npta, sizeof(l_uint32));
    for (i = 0; i < npta; i++) {
        rv = L_MAX(0, 255 -  255 * (2 * i) / (npta + 1));
        bv = L_MIN(255, L_MAX(0, (255 * (3 + 2 * i - npta) / (npta + 1))));
        if (i < npta / 2)
            gv = L_MIN(255, (255 * 2 * i) / (npta + 1));
        else
            gv = L_MIN(255, L_MAX(0, 255 - 255 * (2 * i - npta) / npta));
/*        fprintf(stderr, "rv = %d, gv = %d, bv = %d\n", rv, gv, bv); */
        composeRGBPixel(rv, gv, bv, &pixela[i]);
    }

    for (i = 0; i < npta; i++) {
        pta = ptaaGetPta(ptaa, i, L_CLONE);
        npt = ptaGetCount(pta);
        for (j = 0; j < npt; j++) {
            ptaGetIPt(pta, j, &x, &y);
            pixSetPixel(pixd, x, y, pixela[i]);
        }
        ptaDestroy(&pta);
    }

    FREE(pixela);
    return pixd;
}



