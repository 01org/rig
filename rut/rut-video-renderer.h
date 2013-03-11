/*
 * Rut
 *
 * Copyright (C) 2012  Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RUT_VIDEO_RENDERER_H
#define RUT_VIDEO_RENDERER_H

#include <cogl/cogl.h>

typedef struct _RutGridVector
{
  float x, y, z;
  float s, t;
  float s1, s2, t1, t2;
  float s3, t3;
  float xs, ys;
}RutGridVector;

typedef struct _RutGridPolygon
{
  unsigned int indices[3];
}RutGridPolygon;

typedef struct _RutGridMesh
{
  RutGridPolygon *polygons;
  RutGridVector *vectors;
  int num_polygons;
  int num_vectors;
}RutGridMesh;

typedef struct _RutVideoRenderer
{
  int num_columns;
  int num_rows;
  RutGridMesh *grid;
  CoglAttribute *attributes[5];
  CoglIndices *indices;
}RutVideoRenderer;

RutVideoRenderer*
rut_video_renderer_new (CoglContext *ctx, int cols, int rows, float size);

#endif
