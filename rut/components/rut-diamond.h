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

#ifndef __RUT_DIAMOND_H__
#define __RUT_DIAMOND_H__

#include "rut-entity.h"

typedef struct _RutDiamondSlice RutDiamondSlice;
#define RUT_DIAMOND_SLICE(X) ((RutDiamondSlice *)X)
extern RutType _rut_diamond_slice_type;

struct _RutDiamondSlice
{
  RutObjectProps _parent;
  int ref_count;

  CoglMatrix rotate_matrix;

  float size;

  RutMesh *mesh;

};

void
_rut_diamond_slice_init_type (void);

typedef struct _RutDiamond RutDiamond;
#define RUT_DIAMOND(p) ((RutDiamond *)(p))
extern RutType rut_diamond_type;

struct _RutDiamond
{
  RutObjectProps _parent;

  int ref_count;

  RutComponentableProps component;

  RutContext *ctx;

  RutDiamondSlice *slice;

  RutMesh *pick_mesh;

  int size;
};

void
_rut_diamond_init_type (void);

RutDiamond *
rut_diamond_new (RutContext *ctx,
                 float size,
                 int tex_width,
                 int tex_height);

float
rut_diamond_get_size (RutDiamond *diamond);

CoglPrimitive *
rut_diamond_get_primitive (RutObject *object);

void
rut_diamond_apply_mask (RutDiamond *diamond,
                        CoglPipeline *pipeline);

RutMesh *
rut_diamond_get_pick_mesh (RutObject *self);

#endif /* __RUT_DIAMOND_H__ */
