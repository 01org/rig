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

#ifndef __RUT_POINTALISM_GRID_H__
#define __RUT_POINTALISM_GRID_H__

#include "rut-entity.h"

typedef struct _RutPointalismGridSlice RutPointalismGridSlice;
#define RUT_POINTALISM_GRID_SLICE(X) ((RutPointalismGridSlice *)X)
extern RutType _rut_pointalism_grid_slice_type;

enum {
  RUT_POINTALISM_GRID_PROP_SCALE,
  RUT_POINTALISM_GRID_PROP_Z,
  RUT_POINTALISM_GRID_PROP_LIGHTER,
  RUT_POINTALISM_GRID_PROP_CELL_SIZE,
  RUT_POINTALISM_GRID_N_PROPS
};

struct _RutPointalismGridSlice
{
  RutObjectProps _parent;
  int ref_count;
  CoglPrimitive *primitive;
  CoglIndices *indices;
};

void
_rut_pointalism_grid_slice_init_type (void);

typedef struct _RutPointalismGrid RutPointalismGrid;
#define RUT_POINTALISM_GRID(p) ((RutPointalismGrid *)(p))
extern RutType rut_pointalism_grid_type;

struct _RutPointalismGrid
{
  RutObjectProps _parent;

  int ref_count;

  RutComponentableProps component;

  RutContext *ctx;

  RutPointalismGridSlice *slice;

  RutMesh *pick_mesh;

  float pointalism_scale;
  float pointalism_z;
  CoglBool pointalism_lighter;

  RutSimpleIntrospectableProps introspectable;
  RutProperty properties[RUT_POINTALISM_GRID_N_PROPS];
  float cell_size;
  int tex_width;
  int tex_height;
};

void
_rut_pointalism_grid_init_type (void);

RutPointalismGrid *
rut_pointalism_grid_new (RutContext *ctx,
                         float size,
                         int tex_width,
                         int tex_height);

CoglPrimitive *
rut_pointalism_grid_get_primitive (RutObject *object);

RutMesh *
rut_pointalism_grid_get_pick_mesh (RutObject *self);

float
rut_pointalism_grid_get_scale (RutObject *obj);

void
rut_pointalism_grid_set_scale (RutObject *obj,
                               float scale);

float
rut_pointalism_grid_get_z (RutObject *obj);

void
rut_pointalism_grid_set_z (RutObject *obj,
                           float z);

CoglBool
rut_pointalism_grid_get_lighter (RutObject *obj);

void
rut_pointalism_grid_set_lighter (RutObject *obj,
                                 CoglBool lighter);
                              
float
rut_pointalism_grid_get_cell_size (RutObject *obj);

void
rut_pointalism_grid_set_cell_size (RutObject *obj,
                                   float rows);

#endif /* __RUT_POINTALISM_GRID_H__ */
