/*
 * Rut
 *
 * Copyright (C) 2012 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _RUT_SCROLL_BAR_H_
#define _RUT_SCROLL_BAR_H_

#include "rut-context.h"
#include "rut-types.h"

typedef struct _RutScrollBar RutScrollBar;
#define RUT_SCROLL_BAR(X) ((RutScrollBar *)X)

extern RutType rut_scroll_bar_type;

void
_rut_scroll_bar_init_type (void);

RutScrollBar *
rut_scroll_bar_new (RutContext *ctx,
                    RutAxis axis,
                    float length,
                    float virtual_length,
                    float virtual_viewport_length);

/* Set the length of the scroll bar widget itself */
void
rut_scroll_bar_set_length (RutObject *scroll_bar,
                           float length);

/* How long is virtual length of the document being scrolled */
void
rut_scroll_bar_set_virtual_length (RutObject *scroll_bar,
                                   float virtual_length);

/* What is the length of the viewport into the document being scrolled */
void
rut_scroll_bar_set_virtual_viewport (RutObject *scroll_bar,
                                     float viewport_length);

void
rut_scroll_bar_set_virtual_offset (RutObject *scroll_bar,
                                   float viewport_offset);

float
rut_scroll_bar_get_virtual_offset (RutScrollBar *scroll_bar);

float
rut_scroll_bar_get_virtual_viewport (RutScrollBar *scroll_bar);

float
rut_scroll_bar_get_thickness (RutScrollBar *scroll_bar);

void
rut_scroll_bar_set_color (RutScrollBar *scroll_bar,
                          const CoglColor *color);

#endif /* _RUT_SCROLL_BAR_H_ */
