/*
 * Rut
 *
 * Rig Utilities
 *
 * Copyright (C) 2012 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _RUT_COLOR_H_
#define _RUT_COLOR_H_

#include <clib.h>

#include <cogl/cogl.h>

#include "rut-context.h"

bool
rut_color_init_from_string (RutContext *ctx,
                            CoglColor *color,
                            const char *str);

void
rut_color_init_from_uint32 (CoglColor *color, uint32_t value);

/**
 * rut_color_add:
 * @a: a #CoglColor
 * @b: a #CoglColor
 * @result: (out caller-allocates): return location for the result
 *
 * Adds @a to @b and saves the resulting color inside @result.
 *
 * The alpha channel of @result is set as as the maximum value
 * between the alpha channels of @a and @b.
 */
void
rut_color_add (const CoglColor *a,
               const CoglColor *b,
               CoglColor *result);

/**
 * rut_color_subtract:
 * @a: a #CoglColor
 * @b: a #CoglColor
 * @result: (out caller-allocates): return location for the result
 *
 * Subtracts @b from @a and saves the resulting color inside @result.
 *
 * This function assumes that the components of @a are greater than the
 * components of @b; the result is, otherwise, undefined.
 *
 * The alpha channel of @result is set as the minimum value
 * between the alpha channels of @a and @b.
 */
void
rut_color_subtract (const CoglColor *a,
                    const CoglColor *b,
                    CoglColor *result);

/**
 * rut_color_lighten:
 * @color: a #CoglColor
 * @result: (out caller-allocates): return location for the lighter color
 *
 * Lightens @color by a fixed amount, and saves the changed color
 * in @result.
 */
void
rut_color_lighten (const CoglColor *color,
		   CoglColor *result);

/**
 * rut_color_darken:
 * @color: a #CoglColor
 * @result: (out caller-allocates): return location for the darker color
 *
 * Darkens @color by a fixed amount, and saves the changed color
 * in @result.
 */
void
rut_color_darken (const CoglColor *color,
		  CoglColor *result);

/**
 * rut_color_to_hls:
 * @color: a #CoglColor
 * @hue: (out): return location for the hue value or %NULL
 * @luminance: (out): return location for the luminance value or %NULL
 * @saturation: (out): return location for the saturation value or %NULL
 *
 * Converts @color to the HLS format.
 *
 * The @hue value is in the 0 .. 360 range. The @luminance and
 * @saturation values are in the 0 .. 1 range.
 */
void
rut_color_to_hls (const CoglColor *color,
                  float *hue,
                  float *luminance,
                  float *saturation);

/**
 * rut_color_shade:
 * @color: a #CoglColor
 * @factor: the shade factor to apply
 * @result: (out caller-allocates): return location for the shaded color
 *
 * Shades @color by @factor and saves the modified color into @result.
 */
void
rut_color_shade (const CoglColor *color,
                 float factor,
                 CoglColor *result);

/**
 * rut_color_to_string:
 * @color: a #CoglColor
 *
 * Returns a textual specification of @color in the hexadecimal form
 * <literal>&num;rrggbbaa</literal>, where <literal>r</literal>,
 * <literal>g</literal>, <literal>b</literal> and <literal>a</literal> are
 * hexadecimal digits representing the red, green, blue and alpha components
 * respectively.
 *
 * Return value: (transfer full): a newly-allocated text string
 */
char *
rut_color_to_string (const CoglColor *color);

/**
 * rut_color_interpolate:
 * @initial: the initial #CoglColor
 * @final: the final #CoglColor
 * @progress: the interpolation progress
 * @result: (out): return location for the interpolation
 *
 * Interpolates between @initial and @final #CoglColor<!-- -->s
 * using @progress
 */
void
rut_color_interpolate (const CoglColor *initial,
                       const CoglColor *final,
                       float progress,
                       CoglColor *result);

#endif /* _RUT_COLOR_H_ */
