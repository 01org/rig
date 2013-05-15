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

#ifndef _RIG_RENDERER_H_
#define _RIG_RENDERER_H_

typedef enum _RigPass
{
  RIG_PASS_COLOR_UNBLENDED,
  RIG_PASS_COLOR_BLENDED,
  RIG_PASS_SHADOW,
  RIG_PASS_DOF_DEPTH
} RigPass;

typedef struct _RigPaintContext
{
  RutPaintContext _parent;

  RigEngine *engine;

  GList *camera_stack;

  RigPass pass;

} RigPaintContext;

GArray *
rig_journal_new (void);

void
rig_camera_update_view (RigEngine *engine, RutEntity *camera, CoglBool shadow_pass);

void
rig_paint_camera_entity (RutEntity *camera, RigPaintContext *paint_ctx);

void
rig_renderer_dirty_entity_state (RutEntity *entity);

void
rig_entity_new_image_source (RutImageSource *source,
                             void *user_data);

void
rig_renderer_init (RigEngine *engine);

void
rig_renderer_fini (RigEngine *engine);

#endif /* _RIG_RENDERER_H_ */
