/*
 * Rut
 *
 * Copyright (C) 2013  Intel Corporation
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

#ifndef _RIG_CAMERA_VIEW_H_
#define _RIG_CAMERA_VIEW_H_

#include "rut.h"

#include "rig-engine.h"

extern RutType rig_view_type;

typedef struct _RigCameraView RigCameraView;

#define RIG_CAMERA_VIEW(x) ((RigCameraView *) x)

RigCameraView *
rig_camera_view_new (RigEngine *engine);

void
rig_camera_view_set_scene (RigCameraView *view,
                           RutGraph *scene);

void
rig_camera_view_set_play_camera (RigCameraView *view,
                                 RutEntity *play_camera);

#endif /* _RIG_CAMERA_VIEW_H_ */
