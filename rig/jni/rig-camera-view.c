/*
 * Rig
 *
 * Copyright (C) 2013 Intel Corporation.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>

#include <rut.h>

#include "rig-engine.h"
#include "rig-view.h"
#include "rig-renderer.h"

typedef struct
{
  RutEntity *origin_offset; /* negative offset */
  RutEntity *dev_scale; /* scale to fit device coords */
  RutEntity *screen_pos; /* position screen in edit view */
} RigCameraViewDeviceTransforms;

struct _RigCameraView
{
  RutObjectProps _parent;

  RigEngine *engine;

  RutContext *context;

  int ref_count;

  RutGraphableProps graphable;
  RutPaintableProps paintable;

  float width, height;

  CoglPipeline *bg_pipeline;

  RutGraph *scene;

  float origin[3];
  //float saved_origin[3];

  float device_scale;

  RutEntity *view_camera_to_origin; /* move to origin */
  RutEntity *view_camera_rotate; /* armature rotate rotate */
  RutEntity *view_camera_armature; /* armature length */
  RutEntity *view_camera_2d_view; /* setup 2d view, origin top-left */
  RigCameraViewDeviceTransforms view_device_transforms;

  RutEntity *play_camera;
  RutCamera *play_camera_component;
  RigCameraViewDeviceTransforms play_device_transforms;
  /* This entity is added as a child of all of the play device
   * transforms. During paint the camera component is temporarily
   * stolen from the play camera entity so that it can be transformed
   * with the device transforms */
  RutEntity *play_dummy_entity;

  RutEntity *view_camera;
  RutCamera *view_camera_component;
  float view_camera_z;
  RutInputRegion *input_region;

  float last_viewport_x;
  float last_viewport_y;
  CoglBool dirty_viewport_size;
};

RutType rig_camera_view_type;

static void
unref_device_transforms (RigCameraViewDeviceTransforms *transforms)
{
  rut_refable_unref (transforms->origin_offset);
  rut_refable_unref (transforms->dev_scale);
  rut_refable_unref (transforms->screen_pos);
}

static void
_rig_camera_view_free (void *object)
{
  RigCameraView *view = object;

  rig_camera_view_set_scene (view, NULL);
  rig_camera_view_set_play_camera (view, NULL);

  rut_shell_remove_pre_paint_callback (view->context->shell, view);

  rut_refable_unref (view->context);

  rut_graphable_destroy (view);

  rut_refable_unref (view->view_camera_to_origin);
  rut_refable_unref (view->view_camera_rotate);
  rut_refable_unref (view->view_camera_armature);
  rut_refable_unref (view->view_camera_2d_view);
  rut_refable_unref (view->view_camera);
  rut_refable_unref (view->view_camera_component);
  unref_device_transforms (&view->view_device_transforms);

  rut_refable_unref (view->play_dummy_entity);
  unref_device_transforms (&view->play_device_transforms);

  g_slice_free (RigCameraView, view);
}

RutRefCountableVTable _rig_camera_view_ref_countable_vtable = {
  rut_refable_simple_ref,
  rut_refable_simple_unref,
  _rig_camera_view_free
};

static RutGraphableVTable _rig_camera_view_graphable_vtable = {
  NULL, /* child removed */
  NULL, /* child addded */
  NULL /* parent changed */
};

static void
paint_overlays (RigCameraView *view,
                RutPaintContext *paint_ctx)
{
  RigEngine *engine = view->engine;
  CoglFramebuffer *fb = rut_camera_get_framebuffer (paint_ctx->camera);
  CoglBool need_camera_flush = FALSE;
  CoglBool draw_pick_ray = FALSE;
  CoglBool draw_tools = FALSE;
  RutCamera *suspended_camera = paint_ctx->camera;

  if (engine->debug_pick_ray && engine->picking_ray)
    {
      draw_pick_ray = TRUE;
      need_camera_flush = TRUE;
    }

  if (!_rig_in_device_mode && !engine->play_mode)
    {
      draw_tools = TRUE;
      need_camera_flush = TRUE;
    }

  if (need_camera_flush)
    {
      suspended_camera = paint_ctx->camera;
      rut_camera_suspend (suspended_camera);

      rut_camera_flush (view->view_camera_component);
    }

  /* Use this to visualize the depth-of-field alpha buffer... */
#if 0
  CoglPipeline *pipeline = cogl_pipeline_new (engine->ctx->cogl_context);
  cogl_pipeline_set_layer_texture (pipeline, 0, engine->dof.depth_pass);
  cogl_pipeline_set_blend (pipeline, "RGBA=ADD(SRC_COLOR, 0)", NULL);
  cogl_framebuffer_draw_rectangle (fb,
                                   pipeline,
                                   0, 0,
                                   200, 200);
#endif

  /* Use this to visualize the shadow_map */
#if 0
  CoglPipeline *pipeline = cogl_pipeline_new (engine->ctx->cogl_context);
  cogl_pipeline_set_layer_texture (pipeline, 0, engine->shadow_map);
  //cogl_pipeline_set_layer_texture (pipeline, 0, engine->shadow_color);
  cogl_pipeline_set_blend (pipeline, "RGBA=ADD(SRC_COLOR, 0)", NULL);
  cogl_framebuffer_draw_rectangle (fb,
                                   pipeline,
                                   0, 0,
                                   200, 200);
#endif

  if (draw_pick_ray)
    {
      cogl_framebuffer_draw_primitive (fb,
                                       engine->picking_ray_color,
                                       engine->picking_ray);
    }

#ifdef RIG_EDITOR_ENABLED
  if (draw_tools)
    {
      rut_util_draw_jittered_primitive3f (fb, engine->grid_prim, 0.5, 0.5, 0.5);

      if (engine->selected_entity)
        {
          rut_tool_update (engine->tool, engine->selected_entity);
          rut_tool_draw (engine->tool, fb);
        }
    }
#endif /* RIG_EDITOR_ENABLED */

  if (need_camera_flush)
    {
      rut_camera_end_frame (view->view_camera_component);
      rut_camera_resume (suspended_camera);
    }
}

static void
copy_device_transforms (RigCameraViewDeviceTransforms *dst,
                        RigCameraViewDeviceTransforms *src)
{
  rut_entity_set_position (dst->origin_offset,
                           rut_entity_get_position (src->origin_offset));
  rut_entity_set_scale (dst->dev_scale,
                        rut_entity_get_scale (src->dev_scale));
  rut_entity_set_position (dst->screen_pos,
                           rut_entity_get_position (src->screen_pos));
}

static void
prepare_play_camera_for_view (RigCameraView *view)
{
  RutCamera *play_camera = view->play_camera_component;
  RutCamera *view_camera = view->view_camera_component;

  rut_camera_set_projection_mode (play_camera,
                                  RUT_PROJECTION_PERSPECTIVE);
  rut_camera_set_field_of_view (play_camera,
                                rut_camera_get_field_of_view (view_camera));
  rut_camera_set_near_plane (play_camera,
                             rut_camera_get_near_plane (view_camera));
  rut_camera_set_far_plane (play_camera,
                            rut_camera_get_far_plane (view_camera));

  copy_device_transforms (&view->play_device_transforms,
                          &view->view_device_transforms);

  /* Temporarily move the play camera component to our dummy entity so
   * that it will be positioned with the device transforms after the
   * camera entity's transform */
  rut_entity_remove_component (view->play_camera, play_camera);
  rut_entity_add_component (view->play_dummy_entity, play_camera);
}

static void
reset_play_camera (RigCameraView *view)
{
  rut_entity_remove_component (view->play_dummy_entity,
                               view->play_camera_component);
  rut_entity_add_component (view->play_camera,
                            view->play_camera_component);
}

static void
_rut_camera_view_paint (RutObject *object,
                        RutPaintContext *paint_ctx)
{
  RigCameraView *view = object;
  RigEngine *engine = view->engine;
  RutCamera *suspended_camera = paint_ctx->camera;
  RigPaintContext *rig_paint_ctx = (RigPaintContext *)paint_ctx;
  CoglFramebuffer *fb = rut_camera_get_framebuffer (paint_ctx->camera);
  RutEntity *camera;
  RutCamera *camera_component;
  CoglBool need_play_camera_reset = FALSE;
  float x, y, z;

  if (view->scene == NULL)
    return;

#ifdef RIG_EDITOR_ENABLED
  if (!_rig_in_device_mode && !engine->play_mode)
    {
      camera = view->view_camera;
      camera_component = view->view_camera_component;
    }
  else
#endif /* RIG_EDITOR_ENABLED */
    {
      if (view->play_camera == NULL)
        return;

      prepare_play_camera_for_view (view);

      camera = view->play_dummy_entity;
      camera_component = view->play_camera_component;
      need_play_camera_reset = TRUE;
    }

  rut_camera_set_framebuffer (camera_component, fb);

  cogl_framebuffer_draw_rectangle (fb,
                                   view->bg_pipeline,
                                   0, 0,
                                   view->width,
                                   view->height);

  rut_camera_suspend (suspended_camera);

  rig_paint_ctx->pass = RIG_PASS_SHADOW;
  rig_camera_update_view (engine, engine->light, TRUE);
  rig_paint_camera_entity (engine->light, rig_paint_ctx);

  x = y = z = 0;
  rut_graphable_fully_transform_point (view,
                                       paint_ctx->camera,
                                       &x, &y, &z);

  x = RUT_UTIL_NEARBYINT (x);
  y = RUT_UTIL_NEARBYINT (y);

  /* XXX: if the viewport width/height get changed during allocation
   * then we should probably use a dirty flag so we can defer
   * the viewport update to here. */
  if (camera_component != view->view_camera_component)
    rut_camera_set_viewport (camera_component,
                             x, y, view->width, view->height);
  else if (view->last_viewport_x != x ||
           view->last_viewport_y != y ||
           view->dirty_viewport_size)
    {
      rut_camera_set_viewport (camera_component,
                               x, y, view->width, view->height);

      view->last_viewport_x = x;
      view->last_viewport_y = y;
      view->dirty_viewport_size = FALSE;
    }

  rig_camera_update_view (engine, camera, FALSE);

  if (engine->enable_dof)
    {
      const float *viewport = rut_camera_get_viewport (camera_component);
      int width = viewport[2];
      int height = viewport[3];
      int save_viewport_x = viewport[0];
      int save_viewport_y = viewport[1];
      CoglFramebuffer *pass_fb;

      rut_dof_effect_set_framebuffer_size (engine->dof, width, height);

      pass_fb = rut_dof_effect_get_depth_pass_fb (engine->dof);
      rut_camera_set_framebuffer (camera_component, pass_fb);
      rut_camera_set_viewport (camera_component, 0, 0, width, height);

      rut_camera_flush (camera_component);
      cogl_framebuffer_clear4f (pass_fb,
                                COGL_BUFFER_BIT_COLOR|COGL_BUFFER_BIT_DEPTH,
                                1, 1, 1, 1);
      rut_camera_end_frame (camera_component);

      rig_paint_ctx->pass = RIG_PASS_DOF_DEPTH;
      rig_paint_camera_entity (camera, rig_paint_ctx);

      pass_fb = rut_dof_effect_get_color_pass_fb (engine->dof);
      rut_camera_set_framebuffer (camera_component, pass_fb);

      rut_camera_flush (camera_component);
      cogl_framebuffer_clear4f (pass_fb,
                                COGL_BUFFER_BIT_COLOR|COGL_BUFFER_BIT_DEPTH,
                                1, 1, 1, 1);
      rut_camera_end_frame (camera_component);

      rig_paint_ctx->pass = RIG_PASS_COLOR_UNBLENDED;
      rig_paint_camera_entity (camera, rig_paint_ctx);

      rig_paint_ctx->pass = RIG_PASS_COLOR_BLENDED;
      rig_paint_camera_entity (camera, rig_paint_ctx);

      rut_camera_set_framebuffer (camera_component, fb);
      rut_camera_set_viewport (camera_component,
                               save_viewport_x,
                               save_viewport_y,
                               width, height);

      rut_camera_resume (suspended_camera);
      rut_dof_effect_draw_rectangle (engine->dof,
                                     fb,
                                     0, 0, view->width, view->height);
    }
  else
    {
      rig_paint_ctx->pass = RIG_PASS_COLOR_UNBLENDED;
      rig_paint_camera_entity (camera, rig_paint_ctx);

      rig_paint_ctx->pass = RIG_PASS_COLOR_BLENDED;
      rig_paint_camera_entity (camera, rig_paint_ctx);
      rut_camera_resume (suspended_camera);
    }

  /* XXX: At this point the original, suspended, camera has been resumed */

  paint_overlays (view, paint_ctx);

  if (need_play_camera_reset)
    reset_play_camera (view);
}

static RutPaintableVTable _rig_camera_view_paintable_vtable = {
  _rut_camera_view_paint
};

static void
matrix_view_2d_in_frustum (CoglMatrix *matrix,
                           float left,
                           float right,
                           float bottom,
                           float top,
                           float z_near,
                           float z_2d,
                           float width_2d,
                           float height_2d)
{
  float left_2d_plane = left / z_near * z_2d;
  float right_2d_plane = right / z_near * z_2d;
  float bottom_2d_plane = bottom / z_near * z_2d;
  float top_2d_plane = top / z_near * z_2d;

  float width_2d_start = right_2d_plane - left_2d_plane;
  float height_2d_start = top_2d_plane - bottom_2d_plane;

  /* Factors to scale from framebuffer geometry to frustum
   * cross-section geometry. */
  float width_scale = width_2d_start / width_2d;
  float height_scale = height_2d_start / height_2d;

  //cogl_matrix_translate (matrix,
  //                       left_2d_plane, top_2d_plane, -z_2d);
  cogl_matrix_translate (matrix,
                         left_2d_plane, top_2d_plane, 0);

  cogl_matrix_scale (matrix, width_scale, -height_scale, width_scale);
}

static void
matrix_view_2d_in_perspective (CoglMatrix *matrix,
                               float fov_y,
                               float aspect,
                               float z_near,
                               float z_2d,
                               float width_2d,
                               float height_2d)
{
  float top = z_near * tan (fov_y * G_PI / 360.0);

  matrix_view_2d_in_frustum (matrix,
                             -top * aspect,
                             top * aspect,
                             -top,
                             top,
                             z_near,
                             z_2d,
                             width_2d,
                             height_2d);
}

/* Assuming a symmetric perspective matrix is being used for your
 * projective transform then for a given z_2d distance within the
 * projective frustrum this convenience function determines how
 * we can use an entity transform to move from a normalized coordinate
 * space with (0,0) in the center of the screen to a non-normalized
 * 2D coordinate space with (0,0) at the top-left of the screen.
 *
 * Note: It assumes the viewport aspect ratio matches the desired
 * aspect ratio of the 2d coordinate space which is why we only
 * need to know the width of the 2d coordinate space.
 *
 */
void
get_entity_transform_for_2d_view (float fov_y,
                                  float aspect,
                                  float z_near,
                                  float z_2d,
                                  float width_2d,
                                  float *dx,
                                  float *dy,
                                  float *dz,
                                  CoglQuaternion *rotation,
                                  float *scale)
{
  float top = z_near * tan (fov_y * G_PI / 360.0);
  float left = -top * aspect;
  float right = top * aspect;

  float left_2d_plane = left / z_near * z_2d;
  float right_2d_plane = right / z_near * z_2d;
  float top_2d_plane = top / z_near * z_2d;

  float width_2d_start = right_2d_plane - left_2d_plane;

  *dx = left_2d_plane;
  *dy = top_2d_plane;
  *dz = 0;
  //*dz = -z_2d;

  /* Factors to scale from framebuffer geometry to frustum
   * cross-section geometry. */
  *scale = width_2d_start / width_2d;

  cogl_quaternion_init_from_z_rotation (rotation, 180);
}

static void
update_view_and_projection (RigCameraView *view)
{
  RigEngine *engine = view->engine;
  float fovy = 10; /* y-axis field of view */
  float aspect = (float)view->width/(float)view->height;
  float z_near = 10; /* distance to near clipping plane */
  float z_far = 100; /* distance to far clipping plane */
  float x = 0, y = 0, z_2d = 30, w = 1;
  CoglMatrix inverse;
  float dx, dy, dz, scale;
  CoglQuaternion rotation;

  engine->z_2d = z_2d; /* position to 2d plane */

  cogl_matrix_init_identity (&engine->main_view);
  matrix_view_2d_in_perspective (&engine->main_view,
                                 fovy, aspect, z_near, engine->z_2d,
                                 view->width,
                                 view->height);

  rut_camera_set_projection_mode (view->view_camera_component,
                                  RUT_PROJECTION_PERSPECTIVE);
  rut_camera_set_field_of_view (view->view_camera_component, fovy);
  rut_camera_set_near_plane (view->view_camera_component, z_near);
  rut_camera_set_far_plane (view->view_camera_component, z_far);

  /* Handle the z_2d translation by changing the length of the
   * camera's armature.
   */
  cogl_matrix_get_inverse (&engine->main_view,
                           &inverse);
  cogl_matrix_transform_point (&inverse,
                               &x, &y, &z_2d, &w);

  view->view_camera_z = z_2d / view->device_scale;
  rut_entity_set_translate (view->view_camera_armature, 0, 0, view->view_camera_z);
  //rut_entity_set_translate (view->view_camera_armature, 0, 0, 0);

  get_entity_transform_for_2d_view (fovy,
                                    aspect,
                                    z_near,
                                    engine->z_2d,
                                    view->width,
                                    &dx,
                                    &dy,
                                    &dz,
                                    &rotation,
                                    &scale);

  rut_entity_set_translate (view->view_camera_2d_view, -dx, -dy, -dz);
  rut_entity_set_rotation (view->view_camera_2d_view, &rotation);
  rut_entity_set_scale (view->view_camera_2d_view, 1.0 / scale);
}

static void
update_device_transforms (RigCameraView *view)
{
  RigEngine *engine = view->engine;
  float screen_aspect;
  float main_aspect;

  screen_aspect = engine->device_width / engine->device_height;
  main_aspect = view->width / view->height;

  if (screen_aspect < main_aspect) /* screen is slimmer and taller than the main area */
    {
      engine->screen_area_height = view->height;
      engine->screen_area_width = engine->screen_area_height * screen_aspect;

      rut_entity_set_translate (view->view_device_transforms.screen_pos,
                                -(view->width / 2.0) + (engine->screen_area_width / 2.0),
                                0, 0);
    }
  else
    {
      engine->screen_area_width = view->width;
      engine->screen_area_height = engine->screen_area_width / screen_aspect;

      rut_entity_set_translate (view->view_device_transforms.screen_pos,
                                0,
                                -(view->height / 2.0) + (engine->screen_area_height / 2.0),
                                0);
    }

  /* NB: We know the screen area matches the device aspect ratio so we can use
   * a uniform scale here... */
  view->device_scale = engine->screen_area_width / engine->device_width;

  rut_entity_set_scale (view->view_device_transforms.dev_scale,
                        1.0 / view->device_scale);

  /* Setup projection for main content view */
  update_view_and_projection (view);
}

static void
allocate_cb (RutObject *graphable,
             void *user_data)
{
  RigCameraView *view = RIG_CAMERA_VIEW (graphable);
  RigEngine *engine = view->engine;

  update_device_transforms (view);

#ifdef RIG_EDITOR_ENABLED
  if (!_rig_in_device_mode)
    {
      rut_arcball_init (&engine->arcball,
                        view->width / 2,
                        view->height / 2,
                        sqrtf (view->width *
                               view->width +
                               view->height *
                               view->height) / 2);
    }
#endif /* RIG_EDITOR_ENABLED */

  rut_input_region_set_rectangle (view->input_region,
                                  0, 0, view->width, view->height);
}

static void
queue_allocation (RigCameraView *view)
{
  rut_shell_add_pre_paint_callback (view->context->shell,
                                    view,
                                    allocate_cb,
                                    NULL /* user_data */);
}

static void
rig_camera_view_set_size (void *object,
                          float width,
                          float height)
{
  RigCameraView *view = object;

  if (width == view->width && height == view->height)
    return;

  view->width = width;
  view->height = height;

  view->dirty_viewport_size = TRUE;

  queue_allocation (view);
}

static void
rig_camera_view_get_preferred_width (void *sizable,
                                     float for_height,
                                     float *min_width_p,
                                     float *natural_width_p)
{
  RigCameraView *view = RIG_CAMERA_VIEW (sizable);
  RigEngine *engine = view->engine;

  if (min_width_p)
    *min_width_p = 0;
  if (natural_width_p)
    *natural_width_p = MAX (engine->device_width, engine->device_height);
}

static void
rig_camera_view_get_preferred_height (void *sizable,
                                      float for_width,
                                      float *min_height_p,
                                      float *natural_height_p)
{
  RigCameraView *view = RIG_CAMERA_VIEW (sizable);
  RigEngine *engine = view->engine;

  if (min_height_p)
    *min_height_p = 0;
  if (natural_height_p)
    *natural_height_p = MAX (engine->device_width, engine->device_height);
}

static void
rig_camera_view_get_size (void *object,
                          float *width,
                          float *height)
{
  RigCameraView *view = object;

  *width = view->width;
  *height = view->height;
}

static RutSizableVTable _rig_camera_view_sizable_vtable = {
  rig_camera_view_set_size,
  rig_camera_view_get_size,
  rig_camera_view_get_preferred_width,
  rig_camera_view_get_preferred_height,
  NULL
};

static void
_rig_camera_view_init_type (void)
{
  rut_type_init (&rig_camera_view_type, "RigCameraView");
  rut_type_add_interface (&rig_camera_view_type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (RigCameraView, ref_count),
                          &_rig_camera_view_ref_countable_vtable);
  rut_type_add_interface (&rig_camera_view_type,
                          RUT_INTERFACE_ID_GRAPHABLE,
                          offsetof (RigCameraView, graphable),
                          &_rig_camera_view_graphable_vtable);
  rut_type_add_interface (&rig_camera_view_type,
                          RUT_INTERFACE_ID_PAINTABLE,
                          offsetof (RigCameraView, paintable),
                          &_rig_camera_view_paintable_vtable);
  rut_type_add_interface (&rig_camera_view_type,
                          RUT_INTERFACE_ID_SIZABLE,
                          0, /* no implied properties */
                          &_rig_camera_view_sizable_vtable);
}

static void
update_camera_position (RigCameraView *view)
{
  rut_entity_set_position (view->view_camera_to_origin,
                           view->origin);

  rut_entity_set_translate (view->view_camera_armature, 0, 0, view->view_camera_z);

  rut_shell_queue_redraw (view->context->shell);
}

static void
scene_translate_cb (RutEntity *entity,
                    float start[3],
                    float rel[3],
                    void *user_data)
{
  RigCameraView *view = user_data;

  view->origin[0] = start[0] - rel[0];
  view->origin[1] = start[1] - rel[1];
  view->origin[2] = start[2] - rel[2];

  update_camera_position (view);
}

static void
entity_translate_done_cb (RutEntity *entity,
                          CoglBool moved,
                          float start[3],
                          float rel[3],
                          void *user_data)
{
  RigCameraView *view = user_data;
  RigEngine *engine = view->engine;

  /* If the entity hasn't actually moved then we'll ignore it. It that
   * case the user is presumably just trying to select and entity we
   * don't want it to modify the transition */
  if (moved)
    {
      rig_undo_journal_move_and_log (engine->undo_journal,
                                     FALSE, /* mergable */
                                     entity,
                                     start[0] + rel[0],
                                     start[1] + rel[1],
                                     start[2] + rel[2]);

      rig_reload_position_inspector (engine, entity);

      rut_shell_queue_redraw (engine->ctx->shell);
    }
}

static void
entity_translate_cb (RutEntity *entity,
                     float start[3],
                     float rel[3],
                     void *user_data)
{
  RigCameraView *view = user_data;
  RigEngine *engine = view->engine;

  rut_entity_set_translate (entity,
                            start[0] + rel[0],
                            start[1] + rel[1],
                            start[2] + rel[2]);

  rig_reload_position_inspector (engine, entity);

  rut_shell_queue_redraw (engine->ctx->shell);
}

typedef void (*EntityTranslateCallback)(RutEntity *entity,
                                        float start[3],
                                        float rel[3],
                                        void *user_data);

typedef void (*EntityTranslateDoneCallback)(RutEntity *entity,
                                            CoglBool moved,
                                            float start[3],
                                            float rel[3],
                                            void *user_data);

typedef struct _EntityTranslateGrabClosure
{
  RigEngine *engine;

  /* pointer position at start of grab */
  float grab_x;
  float grab_y;

  /* entity position at start of grab */
  float entity_grab_pos[3];
  RutEntity *entity;

  /* set as soon as a move event is encountered so that we can detect
   * situations where a grab is started but nothing actually moves */
  CoglBool moved;

  float x_vec[3];
  float y_vec[3];

  EntityTranslateCallback entity_translate_cb;
  EntityTranslateDoneCallback entity_translate_done_cb;
  void *user_data;
} EntityTranslateGrabClosure;

static RutInputEventStatus
entity_translate_grab_input_cb (RutInputEvent *event,
                                void *user_data)

{
  EntityTranslateGrabClosure *closure = user_data;
  RutEntity *entity = closure->entity;
  RigEngine *engine = closure->engine;

  if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
      float x = rut_motion_event_get_x (event);
      float y = rut_motion_event_get_y (event);
      float move_x, move_y;
      float rel[3];
      float *x_vec = closure->x_vec;
      float *y_vec = closure->y_vec;

      move_x = x - closure->grab_x;
      move_y = y - closure->grab_y;

      rel[0] = x_vec[0] * move_x;
      rel[1] = x_vec[1] * move_x;
      rel[2] = x_vec[2] * move_x;

      rel[0] += y_vec[0] * move_y;
      rel[1] += y_vec[1] * move_y;
      rel[2] += y_vec[2] * move_y;

      if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_UP)
        {
          if (closure->entity_translate_done_cb)
            closure->entity_translate_done_cb (entity,
                                               closure->moved,
                                               closure->entity_grab_pos,
                                               rel,
                                               closure->user_data);

          rut_shell_ungrab_input (engine->ctx->shell,
                                  entity_translate_grab_input_cb,
                                  user_data);

          g_slice_free (EntityTranslateGrabClosure, user_data);

          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
      else if (rut_motion_event_get_action (event) == RUT_MOTION_EVENT_ACTION_MOVE)
        {
          closure->moved = TRUE;

          closure->entity_translate_cb (entity,
                                        closure->entity_grab_pos,
                                        rel,
                                        closure->user_data);

          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static void
unproject_window_coord (RutCamera *camera,
                        const CoglMatrix *modelview,
                        const CoglMatrix *inverse_modelview,
                        float object_coord_z,
                        float *x,
                        float *y)
{
  const CoglMatrix *projection = rut_camera_get_projection (camera);
  const CoglMatrix *inverse_projection =
    rut_camera_get_inverse_projection (camera);
  //float z;
  float ndc_x, ndc_y, ndc_z, ndc_w;
  float eye_x, eye_y, eye_z, eye_w;
  const float *viewport = rut_camera_get_viewport (camera);

  /* Convert object coord z into NDC z */
  {
    float tmp_x, tmp_y, tmp_z;
    const CoglMatrix *m = modelview;
    float z, w;

    tmp_x = m->xz * object_coord_z + m->xw;
    tmp_y = m->yz * object_coord_z + m->yw;
    tmp_z = m->zz * object_coord_z + m->zw;

    m = projection;
    z = m->zx * tmp_x + m->zy * tmp_y + m->zz * tmp_z + m->zw;
    w = m->wx * tmp_x + m->wy * tmp_y + m->wz * tmp_z + m->ww;

    ndc_z = z / w;
  }

  /* Undo the Viewport transform, putting us in Normalized Device Coords */
  ndc_x = (*x - viewport[0]) * 2.0f / viewport[2] - 1.0f;
  ndc_y = ((viewport[3] - 1 + viewport[1] - *y) * 2.0f / viewport[3] - 1.0f);

  /* Undo the Projection, putting us in Eye Coords. */
  ndc_w = 1;
  cogl_matrix_transform_point (inverse_projection,
                               &ndc_x, &ndc_y, &ndc_z, &ndc_w);
  eye_x = ndc_x / ndc_w;
  eye_y = ndc_y / ndc_w;
  eye_z = ndc_z / ndc_w;
  eye_w = 1;

  /* Undo the Modelview transform, putting us in Object Coords */
  cogl_matrix_transform_point (inverse_modelview,
                               &eye_x,
                               &eye_y,
                               &eye_z,
                               &eye_w);

  *x = eye_x;
  *y = eye_y;
  //*z = eye_z;
}

static CoglBool
translate_grab_entity (RigEngine *engine,
                       RutCamera *camera,
                       RutEntity *entity,
                       float grab_x,
                       float grab_y,
                       EntityTranslateCallback translate_cb,
                       EntityTranslateDoneCallback done_cb,
                       void *user_data)
{
  EntityTranslateGrabClosure *closure =
    g_slice_new (EntityTranslateGrabClosure);
  RutEntity *parent = rut_graphable_get_parent (entity);
  CoglMatrix parent_transform;
  CoglMatrix inverse_transform;
  float origin[3] = {0, 0, 0};
  float unit_x[3] = {1, 0, 0};
  float unit_y[3] = {0, 1, 0};
  float x_vec[3];
  float y_vec[3];
  float entity_x, entity_y, entity_z;
  float w;

  if (!parent)
    return FALSE;

  rut_graphable_get_modelview (parent, camera, &parent_transform);

  if (!cogl_matrix_get_inverse (&parent_transform, &inverse_transform))
    {
      g_warning ("Failed to get inverse transform of entity");
      return FALSE;
    }

  /* Find the z of our selected entity in eye coordinates */
  entity_x = 0;
  entity_y = 0;
  entity_z = 0;
  w = 1;
  cogl_matrix_transform_point (&parent_transform,
                               &entity_x, &entity_y, &entity_z, &w);

  //g_print ("Entity origin in eye coords: %f %f %f\n", entity_x, entity_y, entity_z);

  /* Convert unit x and y vectors in screen coordinate
   * into points in eye coordinates with the same z depth
   * as our selected entity */

  unproject_window_coord (camera,
                          &engine->identity, &engine->identity,
                          entity_z, &origin[0], &origin[1]);
  origin[2] = entity_z;
  //g_print ("eye origin: %f %f %f\n", origin[0], origin[1], origin[2]);

  unproject_window_coord (camera,
                          &engine->identity, &engine->identity,
                          entity_z, &unit_x[0], &unit_x[1]);
  unit_x[2] = entity_z;
  //g_print ("eye unit_x: %f %f %f\n", unit_x[0], unit_x[1], unit_x[2]);

  unproject_window_coord (camera,
                          &engine->identity, &engine->identity,
                          entity_z, &unit_y[0], &unit_y[1]);
  unit_y[2] = entity_z;
  //g_print ("eye unit_y: %f %f %f\n", unit_y[0], unit_y[1], unit_y[2]);


  /* Transform our points from eye coordinates into entity
   * coordinates and convert into input mapping vectors */

  w = 1;
  cogl_matrix_transform_point (&inverse_transform,
                               &origin[0], &origin[1], &origin[2], &w);
  w = 1;
  cogl_matrix_transform_point (&inverse_transform,
                               &unit_x[0], &unit_x[1], &unit_x[2], &w);
  w = 1;
  cogl_matrix_transform_point (&inverse_transform,
                               &unit_y[0], &unit_y[1], &unit_y[2], &w);


  x_vec[0] = unit_x[0] - origin[0];
  x_vec[1] = unit_x[1] - origin[1];
  x_vec[2] = unit_x[2] - origin[2];

  //g_print (" =========================== Entity coords: x_vec = %f, %f, %f\n",
  //         x_vec[0], x_vec[1], x_vec[2]);

  y_vec[0] = unit_y[0] - origin[0];
  y_vec[1] = unit_y[1] - origin[1];
  y_vec[2] = unit_y[2] - origin[2];

  //g_print (" =========================== Entity coords: y_vec = %f, %f, %f\n",
  //         y_vec[0], y_vec[1], y_vec[2]);

  closure->engine = engine;
  closure->grab_x = grab_x;
  closure->grab_y = grab_y;

  memcpy (closure->entity_grab_pos,
          rut_entity_get_position (entity),
          sizeof (float) * 3);

  closure->entity = entity;
  closure->entity_translate_cb = translate_cb;
  closure->entity_translate_done_cb = done_cb;
  closure->moved = FALSE;
  closure->user_data = user_data;

  memcpy (closure->x_vec, x_vec, sizeof (float) * 3);
  memcpy (closure->y_vec, y_vec, sizeof (float) * 3);

  rut_shell_grab_input (engine->ctx->shell,
                        camera,
                        entity_translate_grab_input_cb,
                        closure);

  return TRUE;
}

static void
print_quaternion (const CoglQuaternion *q,
                  const char *label)
{
  float angle = cogl_quaternion_get_rotation_angle (q);
  float axis[3];
  cogl_quaternion_get_rotation_axis (q, axis);
  g_print ("%s: [%f (%f, %f, %f)]\n", label, angle, axis[0], axis[1], axis[2]);
}

static CoglPrimitive *
create_line_primitive (float a[3], float b[3])
{
  CoglVertexP3 engine[2];
  CoglAttributeBuffer *attribute_buffer;
  CoglAttribute *attributes[1];
  CoglPrimitive *primitive;

  engine[0].x = a[0];
  engine[0].y = a[1];
  engine[0].z = a[2];
  engine[1].x = b[0];
  engine[1].y = b[1];
  engine[1].z = b[2];

  attribute_buffer = cogl_attribute_buffer_new (rut_cogl_context,
                                                2 * sizeof (CoglVertexP3),
                                                engine);

  attributes[0] = cogl_attribute_new (attribute_buffer,
                                      "cogl_position_in",
                                      sizeof (CoglVertexP3),
                                      offsetof (CoglVertexP3, x),
                                      3,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

  primitive = cogl_primitive_new_with_attributes (COGL_VERTICES_MODE_LINES,
                                                  2, attributes, 1);

  cogl_object_unref (attribute_buffer);
  cogl_object_unref (attributes[0]);

  return primitive;
}

static void
transform_ray (CoglMatrix *transform,
               bool        inverse_transform,
               float       ray_origin[3],
               float       ray_direction[3])
{
  CoglMatrix inverse, normal_matrix, *m;

  m = transform;
  if (inverse_transform)
    {
      cogl_matrix_get_inverse (transform, &inverse);
      m = &inverse;
    }

  cogl_matrix_transform_points (m,
                                3, /* num components for input */
                                sizeof (float) * 3, /* input stride */
                                ray_origin,
                                sizeof (float) * 3, /* output stride */
                                ray_origin,
                                1 /* n_points */);

  cogl_matrix_get_inverse (m, &normal_matrix);
  cogl_matrix_transpose (&normal_matrix);

  rut_util_transform_normal (&normal_matrix,
                             &ray_direction[0],
                             &ray_direction[1],
                             &ray_direction[2]);
}

static CoglPrimitive *
create_picking_ray (RigEngine            *engine,
                    CoglFramebuffer *fb,
                    float            ray_position[3],
                    float            ray_direction[3],
                    float            length)
{
  CoglPrimitive *line;
  float points[6];

  points[0] = ray_position[0];
  points[1] = ray_position[1];
  points[2] = ray_position[2];
  points[3] = ray_position[0] + length * ray_direction[0];
  points[4] = ray_position[1] + length * ray_direction[1];
  points[5] = ray_position[2] + length * ray_direction[2];

  line = create_line_primitive (points, points + 3);

  return line;
}

typedef struct _PickContext
{
  RutCamera *camera;
  CoglFramebuffer *fb;
  float *ray_origin;
  float *ray_direction;
  RutEntity *selected_entity;
  float selected_distance;
  int selected_index;
} PickContext;

static RutTraverseVisitFlags
entitygraph_pre_pick_cb (RutObject *object,
                         int depth,
                         void *user_data)
{
  PickContext *pick_ctx = user_data;
  CoglFramebuffer *fb = pick_ctx->fb;

  /* XXX: It could be nice if Cogl exposed matrix stacks directly, but for now
   * we just take advantage of an arbitrary framebuffer matrix stack so that
   * we can avoid repeated accumulating the transform of ancestors when
   * traversing between scenegraph nodes that have common ancestors.
   */
  if (rut_object_is (object, RUT_INTERFACE_ID_TRANSFORMABLE))
    {
      const CoglMatrix *matrix = rut_transformable_get_matrix (object);
      cogl_framebuffer_push_matrix (fb);
      cogl_framebuffer_transform (fb, matrix);
    }

  if (rut_object_get_type (object) == &rut_entity_type)
    {
      RutEntity *entity = object;
      RutComponent *geometry;
      RutMesh *mesh;
      int index;
      float distance;
      bool hit;
      float transformed_ray_origin[3];
      float transformed_ray_direction[3];
      CoglMatrix transform;

      if (!rut_entity_get_visible (entity))
        return RUT_TRAVERSE_VISIT_CONTINUE;

      geometry = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_GEOMETRY);

      /* Get a model we can pick against */
      if (!(geometry &&
            rut_object_is (geometry, RUT_INTERFACE_ID_PICKABLE) &&
            (mesh = rut_pickable_get_mesh (geometry))))
        return RUT_TRAVERSE_VISIT_CONTINUE;

      /* transform the ray into the model space */
      memcpy (transformed_ray_origin,
              pick_ctx->ray_origin, 3 * sizeof (float));
      memcpy (transformed_ray_direction,
              pick_ctx->ray_direction, 3 * sizeof (float));

      cogl_framebuffer_get_modelview_matrix (fb, &transform);

      transform_ray (&transform,
                     TRUE, /* inverse of the transform */
                     transformed_ray_origin,
                     transformed_ray_direction);

      /* intersect the transformed ray with the model engine */
      hit = rut_util_intersect_mesh (mesh,
                                     transformed_ray_origin,
                                     transformed_ray_direction,
                                     &index,
                                     &distance);

      if (hit)
        {
          const CoglMatrix *view = rut_camera_get_view_transform (pick_ctx->camera);
          float w = 1;

          /* to compare intersection distances we find the actual point of ray
           * intersection in model coordinates and transform that into eye
           * coordinates */

          transformed_ray_direction[0] *= distance;
          transformed_ray_direction[1] *= distance;
          transformed_ray_direction[2] *= distance;

          transformed_ray_direction[0] += transformed_ray_origin[0];
          transformed_ray_direction[1] += transformed_ray_origin[1];
          transformed_ray_direction[2] += transformed_ray_origin[2];

          cogl_matrix_transform_point (&transform,
                                       &transformed_ray_direction[0],
                                       &transformed_ray_direction[1],
                                       &transformed_ray_direction[2],
                                       &w);
          cogl_matrix_transform_point (view,
                                       &transformed_ray_direction[0],
                                       &transformed_ray_direction[1],
                                       &transformed_ray_direction[2],
                                       &w);
          distance = transformed_ray_direction[2];

          if (distance > pick_ctx->selected_distance)
            {
              pick_ctx->selected_entity = entity;
              pick_ctx->selected_distance = distance;
              pick_ctx->selected_index = index;
            }
        }
    }

  return RUT_TRAVERSE_VISIT_CONTINUE;
}

static RutTraverseVisitFlags
entitygraph_post_pick_cb (RutObject *object,
                          int depth,
                          void *user_data)
{
  if (rut_object_is (object, RUT_INTERFACE_ID_TRANSFORMABLE))
    {
      PickContext *pick_ctx = user_data;
      cogl_framebuffer_pop_matrix (pick_ctx->fb);
    }

  return RUT_TRAVERSE_VISIT_CONTINUE;
}

static void
move_entity_to_camera (RigCameraView *view,
                       RutEntity *entity)
{
  RigEngine *engine = view->engine;
  RigUndoJournal *sub_journal = rig_undo_journal_new (engine);
  float camera_position[3];
  CoglMatrix parent_transform;
  CoglMatrix inverse_parent_transform;
  CoglQuaternion camera_rotation;
  RutProperty *rotation_property =
    rut_introspectable_lookup_property (entity, "rotation");
  RutBoxed boxed_rotation;
  RutObject *parent;

  /* Get the world position of the view camera */
  memset (camera_position, 0, sizeof (camera_position));
  rut_entity_get_transformed_position (view->view_camera_armature,
                                       camera_position);

  /* Get the transform of the parent of the entity so we can calculate
   * a position relative to the parent */
  cogl_matrix_init_identity (&parent_transform);
  parent = rut_graphable_get_parent (entity);
  if (parent)
    rut_graphable_apply_transform (parent, &parent_transform);

  /* Transform the camera position by the inverse of the entity's
   * parent transform so that we will have a position in the
   * coordinate space of the entity */
  if (cogl_matrix_get_inverse (&parent_transform, &inverse_parent_transform))
    {
      cogl_matrix_transform_points (&inverse_parent_transform,
                                    3, /* n_components */
                                    sizeof (float) * 3, /* stride_in */
                                    camera_position, /* points_in */
                                    sizeof (float) * 3, /* stride_out */
                                    camera_position, /* points_out */
                                    1 /* n_points */);

      rig_undo_journal_move_and_log (sub_journal,
                                     FALSE, /* mergable */
                                     entity,
                                     camera_position[0],
                                     camera_position[1],
                                     camera_position[2]);
    }

  /* Copy the camera's rotation. FIXME: this should probably also try
   * to counteract the entity's parent rotations to match what it does
   * for the positioning */
  rut_entity_get_rotations (view->view_camera_armature,
                            &camera_rotation);

  boxed_rotation.type = RUT_PROPERTY_TYPE_QUATERNION;
  boxed_rotation.d.quaternion_val = camera_rotation;

  rig_undo_journal_set_property_and_log (sub_journal,
                                         FALSE,
                                         &boxed_rotation,
                                         rotation_property);

  rig_undo_journal_log_subjournal (engine->undo_journal,
                                   sub_journal);
}

static RutEntity *
pick (RigEngine *engine,
      RutCamera *camera,
      CoglFramebuffer *fb,
      float ray_origin[3],
      float ray_direction[3])
{
  PickContext pick_ctx;

  pick_ctx.camera = camera;
  pick_ctx.fb = fb;
  pick_ctx.selected_distance = -G_MAXFLOAT;
  pick_ctx.selected_entity = NULL;
  pick_ctx.ray_origin = ray_origin;
  pick_ctx.ray_direction = ray_direction;

  /* We are hijacking the framebuffer's matrix to track the graphable
   * transforms so we need to initialise it to a known state */
  cogl_framebuffer_identity_matrix (fb);

  rut_graphable_traverse (engine->scene,
                          RUT_TRAVERSE_DEPTH_FIRST,
                          entitygraph_pre_pick_cb,
                          entitygraph_post_pick_cb,
                          &pick_ctx);

  if (pick_ctx.selected_entity)
    {
      g_message ("Hit entity, triangle #%d, distance %.2f",
                 pick_ctx.selected_index, pick_ctx.selected_distance);
    }

  return pick_ctx.selected_entity;
}


static RutInputEventStatus
input_cb (RutInputEvent *event,
          void *user_data)
{
  RigCameraView *view = user_data;
  RigEngine *engine = view->engine;

  if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
      RutMotionEventAction action = rut_motion_event_get_action (event);
      RutModifierState modifiers = rut_motion_event_get_modifier_state (event);
      float x = rut_motion_event_get_x (event);
      float y = rut_motion_event_get_y (event);
      RutButtonState state;

      rut_tool_set_camera (engine->tool, view->view_camera);

      rut_camera_transform_window_coordinate (view->view_camera_component,
                                              &x, &y);

      state = rut_motion_event_get_button_state (event);

      if (action == RUT_MOTION_EVENT_ACTION_DOWN &&
          state == RUT_BUTTON_STATE_1)
        {
          /* pick */
          RutCamera *camera;
          float ray_position[3], ray_direction[3], screen_pos[2],
                z_far, z_near;
          const float *viewport;
          const CoglMatrix *inverse_projection;
          //CoglMatrix *camera_transform;
          const CoglMatrix *camera_view;
          CoglMatrix camera_transform;
          RutObject *picked_entity;

          camera = rut_entity_get_component (view->view_camera,
                                             RUT_COMPONENT_TYPE_CAMERA);
          viewport = rut_camera_get_viewport (RUT_CAMERA (camera));
          z_near = rut_camera_get_near_plane (RUT_CAMERA (camera));
          z_far = rut_camera_get_far_plane (RUT_CAMERA (camera));
          inverse_projection =
            rut_camera_get_inverse_projection (RUT_CAMERA (camera));

#if 0
          camera_transform = rut_entity_get_transform (view->view_camera);
#else
          camera_view = rut_camera_get_view_transform (camera);
          cogl_matrix_get_inverse (camera_view, &camera_transform);
#endif

          screen_pos[0] = x;
          screen_pos[1] = y;

          rut_util_create_pick_ray (viewport,
                                    inverse_projection,
                                    &camera_transform,
                                    screen_pos,
                                    ray_position,
                                    ray_direction);

          if (engine->debug_pick_ray)
            {
              float x1 = 0, y1 = 0, z1 = z_near, w1 = 1;
              float x2 = 0, y2 = 0, z2 = z_far, w2 = 1;
              float len;

              if (engine->picking_ray)
                cogl_object_unref (engine->picking_ray);

              /* FIXME: This is a hack, we should intersect the ray with
               * the far plane to decide how long the debug primitive
               * should be */
              cogl_matrix_transform_point (&camera_transform,
                                           &x1, &y1, &z1, &w1);
              cogl_matrix_transform_point (&camera_transform,
                                           &x2, &y2, &z2, &w2);
              len = z2 - z1;

              engine->picking_ray = create_picking_ray (engine,
                                                      rut_camera_get_framebuffer (camera),
                                                      ray_position,
                                                      ray_direction,
                                                      len);
            }

          picked_entity = pick (engine,
                                camera,
                                rut_camera_get_framebuffer (camera),
                                ray_position,
                                ray_direction);

          rig_set_selected_entity (engine, picked_entity);

          /* If we have selected an entity then initiate a grab so the
           * entity can be moved with the mouse...
           */
          if (engine->selected_entity)
            {
              if (!translate_grab_entity (engine,
                                          rut_input_event_get_camera (event),
                                          engine->selected_entity,
                                          rut_motion_event_get_x (event),
                                          rut_motion_event_get_y (event),
                                          entity_translate_cb,
                                          entity_translate_done_cb,
                                          view))
                return RUT_INPUT_EVENT_STATUS_UNHANDLED;
            }

          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
      else if (action == RUT_MOTION_EVENT_ACTION_DOWN &&
               state == RUT_BUTTON_STATE_2 &&
               ((modifiers & RUT_MODIFIER_SHIFT_ON) == 0))
        {
          //engine->saved_rotation = *rut_entity_get_rotation (view->view_camera);
          engine->saved_rotation = *rut_entity_get_rotation (view->view_camera_rotate);

          cogl_quaternion_init_identity (&engine->arcball.q_drag);

          //rut_arcball_mouse_down (&engine->arcball, engine->width - x, y);
          rut_arcball_mouse_down (&engine->arcball, view->width - x, view->height - y);
          //g_print ("Arcball init, mouse = (%d, %d)\n", (int)(engine->width - x), (int)(engine->height - y));

          //print_quaternion (&engine->saved_rotation, "Saved Quaternion");
          //print_quaternion (&engine->arcball.q_drag, "Arcball Initial Quaternion");
          //engine->button_down = TRUE;

          engine->grab_x = x;
          engine->grab_y = y;
          //memcpy (view->saved_origin, view->origin, sizeof (view->origin));

          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
      else if (action == RUT_MOTION_EVENT_ACTION_MOVE &&
               state == RUT_BUTTON_STATE_2 &&
               modifiers & RUT_MODIFIER_SHIFT_ON)
        {
          if (!translate_grab_entity (engine,
                                      rut_input_event_get_camera (event),
                                      view->view_camera_to_origin,
                                      rut_motion_event_get_x (event),
                                      rut_motion_event_get_y (event),
                                      scene_translate_cb,
                                      NULL,
                                      view))
            return RUT_INPUT_EVENT_STATUS_UNHANDLED;
#if 0
          float origin[3] = {0, 0, 0};
          float unit_x[3] = {1, 0, 0};
          float unit_y[3] = {0, 1, 0};
          float x_vec[3];
          float y_vec[3];
          float dx;
          float dy;
          float translation[3];

          rut_entity_get_transformed_position (view->view_camera,
                                               origin);
          rut_entity_get_transformed_position (view->view_camera,
                                               unit_x);
          rut_entity_get_transformed_position (view->view_camera,
                                               unit_y);

          x_vec[0] = origin[0] - unit_x[0];
          x_vec[1] = origin[1] - unit_x[1];
          x_vec[2] = origin[2] - unit_x[2];

            {
              CoglMatrix transform;
              rut_graphable_get_transform (view->view_camera, &transform);
              cogl_debug_matrix_print (&transform);
            }
          g_print (" =========================== x_vec = %f, %f, %f\n",
                   x_vec[0], x_vec[1], x_vec[2]);

          y_vec[0] = origin[0] - unit_y[0];
          y_vec[1] = origin[1] - unit_y[1];
          y_vec[2] = origin[2] - unit_y[2];

          //dx = (x - engine->grab_x) * (view->view_camera_z / 100.0f);
          //dy = -(y - engine->grab_y) * (view->view_camera_z / 100.0f);
          dx = (x - engine->grab_x);
          dy = -(y - engine->grab_y);

          translation[0] = dx * x_vec[0];
          translation[1] = dx * x_vec[1];
          translation[2] = dx * x_vec[2];

          translation[0] += dy * y_vec[0];
          translation[1] += dy * y_vec[1];
          translation[2] += dy * y_vec[2];

          view->origin[0] = engine->saved_origin[0] + translation[0];
          view->origin[1] = engine->saved_origin[1] + translation[1];
          view->origin[2] = engine->saved_origin[2] + translation[2];

          update_camera_position (engine);

          g_print ("Translate %f %f dx=%f, dy=%f\n",
                   x - engine->grab_x,
                   y - engine->grab_y,
                   dx, dy);
#endif
          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }
      else if (action == RUT_MOTION_EVENT_ACTION_MOVE &&
               state == RUT_BUTTON_STATE_2 &&
               ((modifiers & RUT_MODIFIER_SHIFT_ON) == 0))
        {
          CoglQuaternion new_rotation;

          //if (!engine->button_down)
          //  break;

          //rut_arcball_mouse_motion (&engine->arcball, engine->width - x, y);
          rut_arcball_mouse_motion (&engine->arcball, view->width - x, view->height - y);
#if 0
          g_print ("Arcball motion, center=%f,%f mouse = (%f, %f)\n",
                   engine->arcball.center[0],
                   engine->arcball.center[1],
                   x, y);
#endif

          cogl_quaternion_multiply (&new_rotation,
                                    &engine->saved_rotation,
                                    &engine->arcball.q_drag);

          //rut_entity_set_rotation (view->view_camera, &new_rotation);
          rut_entity_set_rotation (view->view_camera_rotate, &new_rotation);

          //print_quaternion (&new_rotation, "New Rotation");

          //print_quaternion (&engine->arcball.q_drag, "Arcball Quaternion");

          rut_shell_queue_redraw (engine->ctx->shell);

          return RUT_INPUT_EVENT_STATUS_HANDLED;
        }

    }
#ifdef RIG_EDITOR_ENABLED
  else if (!_rig_in_device_mode &&
           rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_KEY &&
           rut_key_event_get_action (event) == RUT_KEY_EVENT_ACTION_UP)
    {
      switch (rut_key_event_get_keysym (event))
        {
        case RUT_KEY_minus:
          {
            float zoom = rut_camera_get_zoom (view->view_camera_component);

            zoom *= 0.8;

            rut_camera_set_zoom (view->view_camera_component, zoom);

            rut_shell_queue_redraw (engine->ctx->shell);

            break;
          }
        case RUT_KEY_equal:
          {
            float zoom = rut_camera_get_zoom (view->view_camera_component);

            if (zoom)
              zoom *= 1.2;
            else
              zoom = 0.1;

            rut_camera_set_zoom (view->view_camera_component, zoom);

            rut_shell_queue_redraw (engine->ctx->shell);

            break;
          }
        case RUT_KEY_p:
          rig_set_play_mode_enabled (engine, !engine->play_mode);
          break;
        case RUT_KEY_Delete:
          if (engine->selected_entity)
            {
              rig_undo_journal_delete_entity_and_log (engine->undo_journal,
                                                      engine->selected_entity);
              rig_set_selected_entity (engine, NULL);
            }
          break;
        case RUT_KEY_j:
          if ((rut_key_event_get_modifier_state (event) &
               RUT_MODIFIER_CTRL_ON) &&
              engine->selected_entity)
            move_entity_to_camera (view, engine->selected_entity);
          break;
        }
    }
#endif /* RIG_EDITOR_ENABLED */

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static RutInputEventStatus
device_mode_grab_input_cb (RutInputEvent *event, void *user_data)
{
  RigCameraView *view = user_data;
  RigEngine *engine = view->engine;

  if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
      RutMotionEventAction action = rut_motion_event_get_action (event);

      switch (action)
        {
        case RUT_MOTION_EVENT_ACTION_UP:
          rut_shell_ungrab_input (engine->ctx->shell,
                                  device_mode_grab_input_cb,
                                  user_data);
          return RUT_INPUT_EVENT_STATUS_HANDLED;
        case RUT_MOTION_EVENT_ACTION_MOVE:
          {
            float x = rut_motion_event_get_x (event);
            float dx = x - engine->grab_x;
            CoglFramebuffer *fb = COGL_FRAMEBUFFER (engine->onscreen);
            float progression = dx / cogl_framebuffer_get_width (fb);

            rut_timeline_set_progress (engine->timeline,
                                       engine->grab_progress + progression);

            rut_shell_queue_redraw (engine->ctx->shell);
            return RUT_INPUT_EVENT_STATUS_HANDLED;
          }
        default:
          return RUT_INPUT_EVENT_STATUS_UNHANDLED;
        }
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static RutInputEventStatus
device_mode_input_cb (RutInputEvent *event,
                      void *user_data)
{
  RigCameraView *view = user_data;
  RigEngine *engine = view->engine;

  if (rut_input_event_get_type (event) == RUT_INPUT_EVENT_TYPE_MOTION)
    {
      RutMotionEventAction action = rut_motion_event_get_action (event);
      RutButtonState state = rut_motion_event_get_button_state (event);

      if (action == RUT_MOTION_EVENT_ACTION_DOWN &&
          state == RUT_BUTTON_STATE_1)
        {
            engine->grab_x = rut_motion_event_get_x (event);
            engine->grab_y = rut_motion_event_get_y (event);
            engine->grab_progress = rut_timeline_get_progress (engine->timeline);

            /* TODO: Add rut_shell_implicit_grab_input() that handles releasing
             * the grab for you */
            rut_shell_grab_input (engine->ctx->shell,
                                  rut_input_event_get_camera (event),
                                  device_mode_grab_input_cb, view);
            return RUT_INPUT_EVENT_STATUS_HANDLED;

        }
    }

  return RUT_INPUT_EVENT_STATUS_UNHANDLED;
}

static RutInputEventStatus
input_region_cb (RutInputRegion *region,
                 RutInputEvent *event,
                 void *user_data)
{
#ifdef RIG_EDITOR_ENABLED
  if (!_rig_in_device_mode)
    return input_cb (event, user_data);
  else
#endif
    return device_mode_input_cb (event, user_data);
}

static void
init_device_transforms (RutContext *ctx,
                        RigCameraViewDeviceTransforms *transforms)
{
  /* It simplifies things if all the viewport setup for the
   * camera is handled using entity transformations as opposed to
   * mixing entity transforms with manual camera view transforms.
   *
   * The same chain of transforms is used for the play camera and the
   * view camera so it is encapsulated in a separate struct.
   */

  transforms->origin_offset = rut_entity_new (ctx);
  rut_entity_set_label (transforms->origin_offset, "rig:camera_origin_offset");

  transforms->dev_scale = rut_entity_new (ctx);
  rut_graphable_add_child (transforms->origin_offset, transforms->dev_scale);
  rut_entity_set_label (transforms->dev_scale, "rig:camera_dev_scale");

  transforms->screen_pos = rut_entity_new (ctx);
  rut_graphable_add_child (transforms->dev_scale, transforms->screen_pos);
  rut_entity_set_label (transforms->screen_pos, "rig:camera_screen_pos");
}

RigCameraView *
rig_camera_view_new (RigEngine *engine)
{
  RutContext *ctx = engine->ctx;
  RigCameraView *view = g_slice_new0 (RigCameraView);
  static CoglBool initialized = FALSE;

  if (initialized == FALSE)
    {
      _rig_camera_view_init_type ();

      initialized = TRUE;
    }

  view->ref_count = 1;
  view->context = rut_refable_ref (ctx);
  view->engine = engine;

  rut_object_init (&view->_parent, &rig_camera_view_type);

  rut_graphable_init (view);
  rut_paintable_init (view);

  view->bg_pipeline = cogl_pipeline_new (ctx->cogl_context);

  view->input_region =
    rut_input_region_new_rectangle (0, 0, 0, 0, input_region_cb, view);

  rut_graphable_add_child (view, view->input_region);

  /* Conceptually we rig the camera to an armature with a pivot fixed
   * at the current origin. This setup makes it straight forward to
   * model user navigation by letting us change the length of the
   * armature to handle zoom, rotating the armature to handle
   * middle-click rotating the scene with the mouse and moving the
   * position of the armature for shift-middle-click translations with
   * the mouse.
   */
  view->view_camera_to_origin = rut_entity_new (engine->ctx);
  rut_entity_set_label (view->view_camera_to_origin, "rig:camera_to_origin");

  view->view_camera_rotate = rut_entity_new (engine->ctx);
  rut_graphable_add_child (view->view_camera_to_origin, view->view_camera_rotate);
  rut_entity_set_label (view->view_camera_rotate, "rig:camera_rotate");

  view->view_camera_armature = rut_entity_new (engine->ctx);
  rut_graphable_add_child (view->view_camera_rotate, view->view_camera_armature);
  rut_entity_set_label (view->view_camera_armature, "rig:camera_armature");

  init_device_transforms (ctx, &view->view_device_transforms);
  rut_graphable_add_child (view->view_camera_armature,
                           view->view_device_transforms.origin_offset);

  view->view_camera = rut_entity_new (engine->ctx);
  //rut_graphable_add_child (view->view_camera_2d_view, view->view_camera); FIXME
  rut_graphable_add_child (view->view_device_transforms.screen_pos,
                           view->view_camera);
  rut_entity_set_label (view->view_camera, "rig:camera");

  view->view_camera_2d_view = rut_entity_new (engine->ctx);
  //rut_graphable_add_child (view->view_camera_screen_pos, view->view_camera_2d_view); FIXME
  rut_entity_set_label (view->view_camera_2d_view, "rig:camera_2d_view");

  view->view_camera_component = rut_camera_new (engine->ctx, NULL);
  rut_camera_set_clear (view->view_camera_component, FALSE);
  rut_entity_add_component (view->view_camera, view->view_camera_component);

  init_device_transforms (ctx, &view->play_device_transforms);
  view->play_dummy_entity = rut_entity_new (ctx);
  rut_entity_set_label (view->play_dummy_entity, "rig:play_dummy_entity");
  rut_graphable_add_child (view->play_device_transforms.screen_pos,
                           view->play_dummy_entity);

  return view;
}

static void
initialize_navigation_camera (RigCameraView *view)
{
  RigEngine *engine = view->engine;

  view->origin[0] = engine->device_width / 2;
  view->origin[1] = engine->device_height / 2;
  view->origin[2] = 0;

  rut_entity_set_translate (view->view_camera_to_origin,
                            view->origin[0],
                            view->origin[1],
                            view->origin[2]);

  rut_entity_set_translate (view->view_device_transforms.origin_offset,
                            -engine->device_width / 2,
                            -(engine->device_height / 2), 0);

  view->view_camera_z = 10.f;

  update_camera_position (view);

  update_device_transforms (view);
}

void
rig_camera_view_set_scene (RigCameraView *view,
                           RutGraph *scene)
{
  if (view->scene == scene)
    return;

  if (view->scene)
    {
      rut_graphable_remove_child (view->view_camera_to_origin);
      rut_shell_remove_input_camera (view->context->shell,
                                     view->view_camera_component,
                                     view->scene);
    }

  if (scene)
    {
      rut_graphable_add_child (scene, view->view_camera_to_origin);
      rut_shell_add_input_camera (view->context->shell,
                                  view->view_camera_component,
                                  scene);
      initialize_navigation_camera (view);
    }

  /* XXX: to avoid having a circular reference we don't take a
   * reference on the scene... */
  view->scene = scene;
}

void
rig_camera_view_set_play_camera (RigCameraView *view,
                                 RutEntity *play_camera)
{
  if (view->play_camera == play_camera)
    return;

  if (view->play_camera)
    {
      rut_graphable_remove_child (view->play_device_transforms.origin_offset);
      rut_refable_unref (view->play_camera);
      rut_refable_unref (view->play_camera_component);
    }

  if (play_camera)
    {
      view->play_camera = rut_refable_ref (play_camera);

      rut_graphable_add_child (play_camera,
                               view->play_device_transforms.origin_offset);

      view->play_camera_component =
        rut_entity_get_component (play_camera,
                                  RUT_COMPONENT_TYPE_CAMERA);
      rut_refable_ref (view->play_camera_component);
    }
  else
    view->play_camera_component = NULL;
}
