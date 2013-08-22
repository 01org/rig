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
 * License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef _RUT_MODEL_H_
#define _RUT_MODEL_H_

#include <stdint.h>

#include <cogl/cogl.h>

#include "rut-entity.h"
#include "rut-mesh.h"

#define RUT_MODEL(p) ((RutModel *)(p))
typedef struct _RutModel RutModel;
typedef struct _RutModelPrivate RutModelPrivate;
extern RutType rut_model_type;

typedef enum _RutModelType
{
  RUT_MODEL_TYPE_TEMPLATE,
  RUT_MODEL_TYPE_FILE,
} RutModelType;

struct _RutModel
{
  RutObjectProps _parent;

  int ref_count;
  RutContext *ctx;

  RutComponentableProps component;

  RutModelType type;

  RutAsset *asset;

  RutMesh *mesh;

  float min_x;
  float max_x;
  float min_y;
  float max_y;
  float min_z;
  float max_z;

  CoglPrimitive *primitive;

  CoglBool builtin_normals;
  CoglBool builtin_tex_coords;

  /* TODO: I think maybe we should make RutHair and RutModel mutually
   * exclusive and move all of this stuff to RutHair instead. */
  bool is_hair_model;
  RutModelPrivate *priv;
  RutMesh *patched_mesh;
  RutMesh *fin_mesh;
  CoglPrimitive *fin_primitive;
  float default_hair_length;
};

void
_rut_model_init_type (void);

RutModel *
rut_model_new_from_mesh (RutContext *ctx,
                         RutMesh *mesh,
                         CoglBool needs_normals,
                         CoglBool needs_tex_coords);

RutModel *
rut_model_new_from_asset (RutContext *ctx,
                          RutAsset *asset,
                          CoglBool needs_normals,
                          CoglBool needs_tex_coords);

RutModel *
rut_model_new_for_hair (RutModel *base);

RutMesh *
rut_model_get_mesh (RutObject *self);

CoglPrimitive *
rut_model_get_primitive (RutObject *object);

CoglPrimitive *
rut_model_get_fin_primitive (RutObject *object);

float
rut_model_get_default_hair_length (RutObject *object);

#endif /* _RUT_MODEL_H_ */
