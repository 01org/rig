/*
 * Rig
 *
 * UI Engine & Editor
 *
 * Copyright (C) 2012  Intel Corporation
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

#ifndef __RUT_ENTITY_H__
#define __RUT_ENTITY_H__

#include <stdint.h>

#include <cogl/cogl.h>

#include <rut.h>

#include "rig-types.h"

typedef struct _rut_component_t rut_component_t;

typedef struct _rig_entity_t rig_entity_t;
extern rut_type_t rig_entity_type;

typedef enum {
    RUT_COMPONENT_TYPE_CAMERA,
    RUT_COMPONENT_TYPE_LIGHT,
    RUT_COMPONENT_TYPE_GEOMETRY,
    RUT_COMPONENT_TYPE_MATERIAL,
    RUT_COMPONENT_TYPE_HAIR,
    RUT_COMPONENT_TYPE_INPUT,
    RUT_COMPONENT_TYPE_CODE,
    RUT_N_COMPNONENTS
} rut_component_type_t;

typedef struct _rut_componentable_props_t {
    rut_component_type_t type;
    rig_entity_t *
    entity; /* back pointer to the entity the component belongs to */
} rut_componentable_props_t;

typedef struct _rut_componentable_vtable_t {
    rut_object_t *(*copy)(rut_object_t *component);
} rut_componentable_vtable_t;

typedef enum {
    RUT_ENTITY_FLAG_NONE = 0,
    RUT_ENTITY_FLAG_DIRTY = 1 << 0,
    RUT_ENTITY_FLAG_CAST_SHADOW = 1 << 1,
} rig_entity_flag_t;

enum {
    RUT_ENTITY_PROP_LABEL,
    RUT_ENTITY_PROP_POSITION,
    RUT_ENTITY_PROP_ROTATION,
    RUT_ENTITY_PROP_SCALE,
    RUT_ENTITY_N_PROPS
};

struct _rig_entity_t {
    rut_object_base_t _base;

    rut_shell_t *shell;

    char *label;

    rut_graphable_props_t graphable;

    /* private fields */
    float position[3];
    cg_quaternion_t rotation;
    float scale; /* uniform scaling only */
    cg_matrix_t transform;

    c_ptr_array_t *components;

    void *renderer_priv;

    rut_introspectable_props_t introspectable;
    rut_property_t properties[RUT_ENTITY_N_PROPS];

    unsigned int dirty : 1;
};

void _rig_entity_init_type(void);

rig_entity_t *rig_entity_new(rut_shell_t *shell);

rig_entity_t *rig_entity_copy(rig_entity_t *entity);

rut_property_context_t *rig_entity_get_property_context(rig_entity_t *entity);

const char *rig_entity_get_label(rut_object_t *entity);

void rig_entity_set_label(rut_object_t *entity, const char *label);

float rig_entity_get_x(rut_object_t *entity);

void rig_entity_set_x(rut_object_t *entity, float x);

float rig_entity_get_y(rut_object_t *entity);

void rig_entity_set_y(rut_object_t *entity, float y);

float rig_entity_get_z(rut_object_t *entity);

void rig_entity_set_z(rut_object_t *entity, float z);

const float *rig_entity_get_position(rut_object_t *entity);

void rig_entity_set_position(rut_object_t *entity, const float position[3]);

void rig_entity_get_transformed_position(rig_entity_t *entity,
                                         float position[3]);

const cg_quaternion_t *rig_entity_get_rotation(rut_object_t *entity);

void rig_entity_set_rotation(rut_object_t *entity,
                             const cg_quaternion_t *rotation);

void rig_entity_apply_rotations(rut_object_t *entity,
                                cg_quaternion_t *rotations);

void rig_entity_get_rotations(rut_object_t *entity, cg_quaternion_t *rotation);

void rig_entity_get_view_rotations(rut_object_t *entity,
                                   rut_object_t *camera_entity,
                                   cg_quaternion_t *rotation);

float rig_entity_get_scale(rut_object_t *entity);

void rig_entity_set_scale(rut_object_t *entity, float scale);

float rig_entity_get_scales(rut_object_t *entity);

const cg_matrix_t *rig_entity_get_transform(rut_object_t *self);

void rig_entity_add_component(rig_entity_t *entity, rut_object_t *component);

void rig_entity_remove_component(rig_entity_t *entity, rut_object_t *component);

void rig_entity_translate(rig_entity_t *entity, float tx, float tz, float ty);

void
rig_entity_set_translate(rig_entity_t *entity, float tx, float ty, float tz);

void rig_entity_rotate_x_axis(rig_entity_t *entity, float x_angle);
void rig_entity_rotate_y_axis(rig_entity_t *entity, float y_angle);
void rig_entity_rotate_z_axis(rig_entity_t *entity, float z_angle);

rut_object_t *rig_entity_get_component(rig_entity_t *entity,
                                       rut_component_type_t type);

void rig_entity_foreach_component(rig_entity_t *entity,
                                  bool (*callback)(rut_object_t *component,
                                                   void *user_data),
                                  void *user_data);

void rig_entity_foreach_component_safe(rig_entity_t *entity,
                                       bool (*callback)(rut_object_t *component,
                                                        void *user_data),
                                       void *user_data);

void rig_entity_notify_changed(rig_entity_t *entity);

void rig_entity_reap(rig_entity_t *entity, rig_engine_t *engine);

void rig_component_reap(rut_object_t *component, rig_engine_t *engine);

/* Assuming the given entity has an associated camera component this
 * updates the camera component's view transform according to the
 * current transformation of the entity. */
void rig_entity_set_camera_view_from_transform(rig_entity_t *camera);

#endif /* __RUT_ENTITY_H__ */
