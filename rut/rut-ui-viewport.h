/*
 * Rut
 *
 * A tiny toolkit
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

#ifndef _RUT_UI_VIEWPORT_H_
#define _RUT_UI_VIEWPORT_H_

#include <cogl/cogl.h>

#include "rut-type.h"

typedef struct _RutUIViewport RutUIViewport;
#define RUT_UI_VIEWPORT(X) ((RutUIViewport *)X)

extern RutType rut_ui_viewport_type;

RutUIViewport *
rut_ui_viewport_new (RutContext *ctx,
                     float width,
                     float height);

void
rut_ui_viewport_add (RutUIViewport *ui_viewport,
                     RutObject *child);

void
rut_ui_viewport_get_size (RutUIViewport *ui_viewport,
                          float *width,
                          float *height);

void
rut_ui_viewport_set_doc_x (RutObject *ui_viewport, float doc_x);

void
rut_ui_viewport_set_doc_y (RutObject *ui_viewport, float doc_y);

void
rut_ui_viewport_set_doc_width (RutObject *ui_viewport, float doc_width);

void
rut_ui_viewport_set_doc_height (RutObject *ui_viewport, float doc_height);

void
rut_ui_viewport_set_doc_scale_x (RutUIViewport *ui_viewport, float doc_scale_x);

void
rut_ui_viewport_set_doc_scale_y (RutUIViewport *ui_viewport, float doc_scale_y);

float
rut_ui_viewport_get_width (RutUIViewport *ui_viewport);

float
rut_ui_viewport_get_height (RutUIViewport *ui_viewport);

float
rut_ui_viewport_get_doc_x (RutObject *ui_viewport);

float
rut_ui_viewport_get_doc_y (RutObject *ui_viewport);

float
rut_ui_viewport_get_doc_scale_x (RutUIViewport *ui_viewport);

float
rut_ui_viewport_get_doc_scale_y (RutUIViewport *ui_viewport);

const CoglMatrix *
rut_ui_viewport_get_doc_matrix (RutUIViewport *ui_viewport);

void
rut_ui_viewport_set_x_pannable (RutObject *ui_viewport,
                                CoglBool pannable);

CoglBool
rut_ui_viewport_get_x_pannable (RutObject *ui_viewport);

void
rut_ui_viewport_set_y_pannable (RutObject *ui_viewport,
                                CoglBool pannable);

CoglBool
rut_ui_viewport_get_y_pannable (RutObject *ui_viewport);

/**
 * rut_ui_viewport_set_sync_widget:
 * @ui_viewport: The viewport object
 * @widget: An object implementing the RutSizable interface
 *
 * Sets a widget to use to specify the doc size. The viewport will
 * track the preferred size of the widget and set the doc to the same
 * size whenever it changes.
 *
 * If the viewport is not pannable on the x-axis then the width of
 * this widget will kept in sync with the width of the viewport.
 * Similarly if the viewport is not pannable on the y-axis then the
 * height of this widget will kept in sync with the height of the
 * viewport.
 *
 * The sync widget should typically be a child of the doc.
 */
void
rut_ui_viewport_set_sync_widget (RutObject *ui_viewport,
                                 RutObject *widget);

void
rut_ui_viewport_set_scroll_bar_color (RutUIViewport *ui_viewport,
                                      const CoglColor *color);

#endif /* _RUT_UI_VIEWPORT_H_ */
