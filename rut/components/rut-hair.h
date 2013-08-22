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

#ifndef __RUT_HAIR_H__
#define __RUT_HAIR_H__

#include "rut-entity.h"
#include "rut-model.h"

enum {
  RUT_HAIR_SHELL_POSITION_BLENDED,
  RUT_HAIR_SHELL_POSITION_UNBLENDED,
  RUT_HAIR_SHELL_POSITION_SHADOW,
  RUT_HAIR_LENGTH,
  RUT_HAIR_N_UNIFORMS
};

enum {
  RUT_HAIR_PROP_LENGTH,
  RUT_HAIR_PROP_DETAIL,
  RUT_HAIR_PROP_DENSITY,
  RUT_HAIR_PROP_THICKNESS,
  RUT_HAIR_N_PROPS
};

typedef struct _RutHair RutHair;
#define RUT_HAIR(p) ((RutHair *)(p))
extern RutType rut_hair_type;

struct _RutHair
{
  RutObjectProps _parent;

  int ref_count;

  RutComponentableProps component;
  RutContext *ctx;
  CoglTexture *circle;
  CoglTexture *fin_texture;
  float *shell_positions;
  GArray *shell_textures;
  GArray *particles;

  float length;
  int n_shells;
  int n_textures;
  int density;
  float thickness;
  int uniform_locations[4];

  RutSimpleIntrospectableProps introspectable;
  RutProperty properties[RUT_HAIR_N_PROPS];

  unsigned int dirty_shell_textures: 1;
  unsigned int dirty_fin_texture: 1;
  unsigned int dirty_hair_positions: 1;
};

void
_rut_hair_init_type (void);

RutHair *
rut_hair_new (RutContext *ctx);

float
rut_hair_get_length (RutObject *obj);

void
rut_hair_set_length (RutObject *obj,
                     float length);

int
rut_hair_get_n_shells (RutObject *obj);

void
rut_hair_set_n_shells (RutObject *obj,
                       int n_shells);

int
rut_hair_get_density (RutObject *obj);

void
rut_hair_set_density (RutObject *obj,
                      int density);

float
rut_hair_get_thickness (RutObject *obj);

void
rut_hair_set_thickness (RutObject *obj,
                        float thickness);

void
rut_hair_update_state (RutHair *hair);

float
rut_hair_get_shell_position (RutObject *obj,
                             int shell);

void
rut_hair_set_uniform_location (RutObject *obj,
                               CoglPipeline *pln,
                               int uniform);

void
rut_hair_set_uniform_float_value (RutObject *obj,
                                  CoglPipeline *pln,
                                  int uniform,
                                  float value);

#endif
