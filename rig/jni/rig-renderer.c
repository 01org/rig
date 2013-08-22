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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rut.h>

#include "rig-engine.h"
#include "rig-renderer.h"

struct _RigRenderer
{
  RutObjectProps _parent;

  int ref_count;

  GArray *journal;
};

typedef enum _CacheSlot
{
  CACHE_SLOT_SHADOW,
  CACHE_SLOT_COLOR_BLENDED,
  CACHE_SLOT_COLOR_UNBLENDED,
  CACHE_SLOT_HAIR_FINS_BLENDED,
  CACHE_SLOT_HAIR_FINS_UNBLENDED,
} CacheSlot;

typedef enum _SourceType
{
  SOURCE_TYPE_COLOR,
  SOURCE_TYPE_ALPHA_MASK,
  SOURCE_TYPE_NORMAL_MAP
} SourceType;

typedef struct _RigJournalEntry
{
  RutEntity *entity;
  CoglMatrix matrix;
} RigJournalEntry;

/* In the shaders, any alpha value greater than or equal to this is
 * considered to be fully opaque. We can't just compare for equality
 * against 1.0 because at least on a Mac Mini there seems to be some
 * fuzziness in the interpolation of the alpha value across the
 * primitive so that it is sometimes slightly less than 1.0 even
 * though all of the vertices in the triangle are 1.0. This means some
 * of the pixels of the geometry would be painted with the blended
 * pipeline. The blended pipeline doesn't write to the depth value so
 * depending on the order of the triangles within the model it might
 * paint the back or the front of the model which causes weird sparkly
 * artifacts.
 *
 * I think it doesn't really make sense to paint models that have any
 * depth using the blended pipeline. In that case you would also need
 * to sort individual triangles of the model according to depth.
 * Perhaps the real solution to this problem is to avoid using the
 * blended pipeline at all for 3D models.
 *
 * However even for flat quad shapes it is probably good to have this
 * threshold because if a pixel is close enough to opaque that the
 * appearance will be the same then it is chaper to render it without
 * blending.
 */
#define OPAQUE_THRESHOLD 0.9999

#define N_PIPELINE_CACHE_SLOTS 5
#define N_IMAGE_SOURCE_CACHE_SLOTS 3
#define N_PRIMITIVE_CACHE_SLOTS 1

typedef struct _RigRendererPriv
{
  RigRenderer *renderer;

  CoglPipeline *pipeline_caches[N_PIPELINE_CACHE_SLOTS];
  RutImageSource *image_source_caches[N_IMAGE_SOURCE_CACHE_SLOTS];
  CoglPrimitive *primitive_caches[N_PRIMITIVE_CACHE_SLOTS];
} RigRendererPriv;

static void
_rig_renderer_free (void *object)
{
  RigRenderer *renderer = object;

  g_array_free (renderer->journal, TRUE);
  renderer->journal = NULL;

  g_slice_free (RigRenderer, object);
}

static void
set_entity_pipeline_cache (RutEntity *entity,
                           int slot,
                           CoglPipeline *pipeline)
{
  RigRendererPriv *priv = entity->renderer_priv;

  if (priv->pipeline_caches[slot])
    cogl_object_unref (priv->pipeline_caches[slot]);

  priv->pipeline_caches[slot] = pipeline;
  if (pipeline)
    cogl_object_ref (pipeline);
}

static CoglPipeline *
get_entity_pipeline_cache (RutEntity *entity,
                           int slot)
{
  RigRendererPriv *priv = entity->renderer_priv;
  return priv->pipeline_caches[slot];
}

static void
set_entity_image_source_cache (RutEntity *entity,
                               int slot,
                               RutImageSource *source)
{
  RigRendererPriv *priv = entity->renderer_priv;

  if (priv->image_source_caches[slot])
    rut_refable_unref (priv->image_source_caches[slot]);

  priv->image_source_caches[slot] = source;
  if (source)
    rut_refable_ref (source);
}

static RutImageSource*
get_entity_image_source_cache (RutEntity *entity,
                               int slot)
{
  RigRendererPriv *priv = entity->renderer_priv;

  return priv->image_source_caches[slot];
}

static void
set_entity_primitive_cache (RutEntity *entity,
                            int slot,
                            CoglPrimitive *primitive)
{
  RigRendererPriv *priv = entity->renderer_priv;

  if (priv->primitive_caches[slot])
    cogl_object_unref (priv->primitive_caches[slot]);

  priv->primitive_caches[slot] = primitive;
  if (primitive)
    cogl_object_ref (primitive);
}

static CoglPrimitive *
get_entity_primitive_cache (RutEntity *entity,
                            int slot)
{
  RigRendererPriv *priv = entity->renderer_priv;

  return priv->primitive_caches[slot];
}

static void
dirty_entity_pipelines (RutEntity *entity)
{
  set_entity_pipeline_cache (entity, CACHE_SLOT_COLOR_UNBLENDED, NULL);
  set_entity_pipeline_cache (entity, CACHE_SLOT_COLOR_BLENDED, NULL);
  set_entity_pipeline_cache (entity, CACHE_SLOT_SHADOW, NULL);
}

static void
dirty_entity_geometry (RutEntity *entity)
{
  set_entity_primitive_cache (entity, 0, NULL);
}

/* TODO: allow more fine grained discarding of cached renderer state */
static void
_rig_renderer_notify_entity_changed (RutEntity *entity)
{
  if (!entity->renderer_priv)
    return;

  dirty_entity_pipelines (entity);
  dirty_entity_geometry (entity);

  set_entity_image_source_cache (entity, SOURCE_TYPE_COLOR, NULL);
  set_entity_image_source_cache (entity, SOURCE_TYPE_ALPHA_MASK, NULL);
  set_entity_image_source_cache (entity, SOURCE_TYPE_NORMAL_MAP, NULL);
}

static void
_rig_renderer_free_priv (RutEntity *entity)
{
  RigRendererPriv *priv = entity->renderer_priv;
  CoglPipeline **pipeline_caches = priv->pipeline_caches;
  RutImageSource **image_source_caches = priv->image_source_caches;
  CoglPrimitive **primitive_caches = priv->primitive_caches;
  int i;

  for (i = 0; i < N_PIPELINE_CACHE_SLOTS; i++)
    {
      if (pipeline_caches[i])
        cogl_object_unref (pipeline_caches[i]);
    }

  for (i = 0; i < N_IMAGE_SOURCE_CACHE_SLOTS; i++)
    {
      if (image_source_caches[i])
        rut_refable_unref (image_source_caches[i]);
    }

  for (i = 0; i < N_PRIMITIVE_CACHE_SLOTS; i++)
    {
      if (primitive_caches[i])
        rut_refable_unref (primitive_caches[i]);
    }

  g_slice_free (RigRendererPriv, priv);
  entity->renderer_priv = NULL;
}

RutType rig_renderer_type;

static void
_rig_renderer_init_type (void)
{
  static RutRefableVTable refable_vtable = {
      rut_refable_simple_ref,
      rut_refable_simple_unref,
      _rig_renderer_free
  };

  static RutRendererVTable renderer_vtable = {
      .notify_entity_changed = _rig_renderer_notify_entity_changed,
      .free_priv = _rig_renderer_free_priv
  };

  RutType *type = &rig_renderer_type;
#define TYPE RigRenderer

  rut_type_init (type, G_STRINGIFY (TYPE));
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (TYPE, ref_count),
                          &refable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_RENDERER,
                          0, /* no implied properties */
                          &renderer_vtable);

#undef TYPE
}

RigRenderer *
rig_renderer_new (RigEngine *engine)
{
  RigRenderer *renderer = g_slice_new0 (RigRenderer);
  static CoglBool initialized = FALSE;

  if (initialized == FALSE)
    {
      _rig_renderer_init_type ();
      initialized = TRUE;
    }

  rut_object_init (&renderer->_parent, &rig_renderer_type);

  renderer->ref_count = 1;

  renderer->journal = g_array_new (FALSE, FALSE, sizeof (RigJournalEntry));

  return renderer;
}

static void
rig_journal_log (GArray *journal,
                 RigPaintContext *paint_ctx,
                 RutEntity *entity,
                 const CoglMatrix *matrix)
{

  RigJournalEntry *entry;

  g_array_set_size (journal, journal->len + 1);
  entry = &g_array_index (journal, RigJournalEntry, journal->len - 1);

  entry->entity = rut_refable_ref (entity);
  entry->matrix = *matrix;
}

static int
sort_entry_cb (const RigJournalEntry *entry0,
               const RigJournalEntry *entry1)
{
  float z0 = entry0->matrix.zw;
  float z1 = entry1->matrix.zw;

  /* TODO: also sort based on the state */

  if (z0 < z1)
    return -1;
  else if (z0 > z1)
    return 1;

  return 0;
}

static void
reshape_cb (RutShape *shape, void *user_data)
{
  RutComponentableProps *componentable =
    rut_object_get_properties (shape, RUT_INTERFACE_ID_COMPONENTABLE);
  RutEntity *entity = componentable->entity;
  dirty_entity_pipelines (entity);
}

static void
nine_slice_changed_cb (RutNineSlice *nine_slice, void *user_data)
{
  RutComponentableProps *componentable =
    rut_object_get_properties (nine_slice, RUT_INTERFACE_ID_COMPONENTABLE);
  RutEntity *entity = componentable->entity;
  _rig_renderer_notify_entity_changed (entity);
  dirty_entity_geometry (entity);
}

/* Called if the geometry of the pointalism grid has changed... */
static void
pointalism_changed_cb (RutPointalismGrid *grid, void *user_data)
{
  RutComponentableProps *componentable =
    rut_object_get_properties (grid, RUT_INTERFACE_ID_COMPONENTABLE);
  RutEntity *entity = componentable->entity;

  dirty_entity_geometry (entity);
}

static void
set_focal_parameters (CoglPipeline *pipeline,
                      float focal_distance,
                      float depth_of_field)
{
  int location;
  float distance;

  /* I want to have the focal distance as positive when it's in front of the
   * camera (it seems more natural, but as, in OpenGL, the camera is facing
   * the negative Ys, the actual value to give to the shader has to be
   * negated */
  distance = -focal_distance;

  location = cogl_pipeline_get_uniform_location (pipeline,
                                                 "dof_focal_distance");
  cogl_pipeline_set_uniform_float (pipeline,
                                   location,
                                   1 /* n_components */, 1 /* count */,
                                   &distance);

  location = cogl_pipeline_get_uniform_location (pipeline,
                                                 "dof_depth_of_field");
  cogl_pipeline_set_uniform_float (pipeline,
                                   location,
                                   1 /* n_components */, 1 /* count */,
                                   &depth_of_field);
}

static void
init_dof_pipeline_template (RigEngine *engine)
{
  CoglPipeline *pipeline;
  CoglDepthState depth_state;
  CoglSnippet *snippet;

  pipeline = cogl_pipeline_new (engine->ctx->cogl_context);

  cogl_pipeline_set_color_mask (pipeline, COGL_COLOR_MASK_ALPHA);

  cogl_pipeline_set_blend (pipeline, "RGBA=ADD(SRC_COLOR, 0)", NULL);

  cogl_depth_state_init (&depth_state);
  cogl_depth_state_set_test_enabled (&depth_state, TRUE);
  cogl_pipeline_set_depth_state (pipeline, &depth_state, NULL);

  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,

                              /* definitions */
                              "uniform float dof_focal_distance;\n"
                              "uniform float dof_depth_of_field;\n"

                              "varying float dof_blur;\n",
                              //"varying vec4 world_pos;\n",

                              /* compute the amount of bluriness we want */
                              "vec4 world_pos = cogl_modelview_matrix * pos;\n"
                              //"world_pos = cogl_modelview_matrix * cogl_position_in;\n"
                              "dof_blur = 1.0 - clamp (abs (world_pos.z - dof_focal_distance) /\n"
                              "                  dof_depth_of_field, 0.0, 1.0);\n"
  );

  cogl_pipeline_add_snippet (pipeline, engine->cache_position_snippet);
  cogl_pipeline_add_snippet (pipeline, snippet);
  cogl_object_unref (snippet);

  /* This was used to debug the focal distance and bluriness amount in the DoF
   * effect: */
#if 0
  cogl_pipeline_set_color_mask (pipeline, COGL_COLOR_MASK_ALL);
  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                              "varying vec4 world_pos;\n"
                              "varying float dof_blur;",

                              "cogl_color_out = vec4(dof_blur,0,0,1);\n"
                              //"cogl_color_out = vec4(1.0, 0.0, 0.0, 1.0);\n"
                              //"if (world_pos.z < -30.0) cogl_color_out = vec4(0,1,0,1);\n"
                              //"if (abs (world_pos.z + 30.f) < 0.1) cogl_color_out = vec4(0,1,0,1);\n"
                              "cogl_color_out.a = dof_blur;\n"
                              //"cogl_color_out.a = 1.0;\n"
  );

  cogl_pipeline_add_snippet (pipeline, snippet);
  cogl_object_unref (snippet);
#endif

  engine->dof_pipeline_template = pipeline;
}

static void
init_dof_diamond_pipeline (RigEngine *engine)
{
  CoglPipeline *dof_diamond_pipeline =
    cogl_pipeline_copy (engine->dof_pipeline_template);
  CoglSnippet *snippet;

  cogl_pipeline_set_layer_texture (dof_diamond_pipeline,
                                   0,
                                   engine->ctx->circle_texture);

  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                              /* declarations */
                              "varying float dof_blur;",

                              /* post */
                              "if (cogl_color_out.a <= 0.0)\n"
                              "  discard;\n"
                              "\n"
                              "cogl_color_out.a = dof_blur;\n");

  cogl_pipeline_add_snippet (dof_diamond_pipeline, snippet);
  cogl_object_unref (snippet);

  engine->dof_diamond_pipeline = dof_diamond_pipeline;
}

static void
init_dof_unshaped_pipeline (RigEngine *engine)
{
  CoglPipeline *dof_unshaped_pipeline =
    cogl_pipeline_copy (engine->dof_pipeline_template);
  CoglSnippet *snippet;

  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                              /* declarations */
                              "varying float dof_blur;",

                              /* post */
                              "if (cogl_color_out.a < 0.25)\n"
                              "  discard;\n"
                              "\n"
                              "cogl_color_out.a = dof_blur;\n");

  cogl_pipeline_add_snippet (dof_unshaped_pipeline, snippet);
  cogl_object_unref (snippet);

  engine->dof_unshaped_pipeline = dof_unshaped_pipeline;
}

static void
init_dof_pipeline (RigEngine *engine)
{
  CoglPipeline *dof_pipeline =
    cogl_pipeline_copy (engine->dof_pipeline_template);
  CoglSnippet *snippet;

  /* store the bluriness in the alpha channel */
  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                              "varying float dof_blur;",

                              "cogl_color_out.a = dof_blur;\n"
  );
  cogl_pipeline_add_snippet (dof_pipeline, snippet);
  cogl_object_unref (snippet);

  engine->dof_pipeline = dof_pipeline;
}

void
rig_renderer_init (RigEngine *engine)
{
  /* We always want to use exactly the same snippets when creating
   * similar pipelines so that we can take advantage of Cogl's program
   * caching. The program cache only compares the snippet pointers,
   * not the contents of the snippets. Therefore we just create the
   * snippets we're going to use upfront and retain them */

  engine->alpha_mask_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                      /* definitions */
                      "uniform float material_alpha_threshold;\n",
                      /* post */
                      "if (texture2D(cogl_sampler4,\n"
                      "              cogl_tex_coord4_in.st).a <= \n"
                      "    material_alpha_threshold)\n"
                      "  discard;\n");

  engine->alpha_mask_video_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                      /* definitions */
                      "uniform float material_alpha_threshold;\n",
                      /* post */
                      "if (cogl_gst_sample_video4 (\n"
                      "    cogl_tex_coord4_in.st).r < \n"
                      "    material_alpha_threshold)\n"
                      "  discard;\n");

  engine->lighting_vertex_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,
                      /* definitions */
                      "uniform mat3 normal_matrix;\n"
                      "attribute vec3 tangent_in;\n"
                      "varying vec3 normal, eye_direction;\n",
                      /* post */
                      "normal = normalize(normal_matrix * cogl_normal_in);\n"
                      "eye_direction = -vec3(cogl_modelview_matrix *\n"
                      "                      pos);\n"
                      );

  engine->normal_map_vertex_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,
                      /* definitions */
                      "uniform vec3 light0_direction_norm;\n"
                      "varying vec3 light_direction;\n",

                      /* post */
                      "vec3 tangent = normalize(normal_matrix * tangent_in);\n"
                      "vec3 binormal = cross(normal, tangent);\n"

                      /* Transform the light direction into tangent space */
                      "vec3 v;\n"
                      "v.x = dot (light0_direction_norm, tangent);\n"
                      "v.y = dot (light0_direction_norm, binormal);\n"
                      "v.z = dot (light0_direction_norm, normal);\n"
                      "light_direction = normalize (v);\n"

                      /* Transform the eye direction into tangent space */
                      "v.x = dot (eye_direction, tangent);\n"
                      "v.y = dot (eye_direction, binormal);\n"
                      "v.z = dot (eye_direction, normal);\n"
                      "eye_direction = normalize (v);\n");

  engine->cache_position_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX_TRANSFORM,
                      "varying vec4 pos;\n",
                      "pos = cogl_position_in;\n");


  engine->pointalism_vertex_snippet =
  cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX_TRANSFORM,
    "attribute vec2 cell_xy;\n"
    "attribute vec4 cell_st;\n"
    "uniform float scale_factor;\n"
    "uniform float z_trans;\n"
    "uniform int anti_scale;\n"
    "varying vec4 av_color;\n",

    "float grey;\n"

    "av_color = texture2D (cogl_sampler1, vec2 (cell_st.x, cell_st.z));\n"
    "av_color += texture2D (cogl_sampler1, vec2 (cell_st.y, cell_st.z));\n"
    "av_color += texture2D (cogl_sampler1, vec2 (cell_st.y, cell_st.w));\n"
    "av_color += texture2D (cogl_sampler1, vec2 (cell_st.x, cell_st.w));\n"
    "av_color /= 4.0;\n"

    "grey = av_color.r * 0.2126 + av_color.g * 0.7152 + av_color.b * 0.0722;\n"

    "if (anti_scale == 1)\n"
    "{"
    "pos.xy *= scale_factor * grey;\n"
    "pos.z += z_trans * grey;\n"
    "}"
    "else\n"
    "{"
    "pos.xy *= scale_factor - (scale_factor * grey);\n"
    "pos.z += z_trans - (z_trans * grey);\n"
    "}"
    "pos.x += cell_xy.x;\n"
    "pos.y += cell_xy.y;\n"
    "cogl_position_out = cogl_modelview_projection_matrix * pos;\n");

  engine->pointalism_video_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX_TRANSFORM,
    "attribute vec2 cell_xy;\n"
    "attribute vec4 cell_st;\n"
    "uniform float scale_factor;\n"
    "uniform float z_trans;\n"
    "uniform int anti_scale;\n"
    "varying vec4 av_color;\n",

    "float grey;\n"

    "av_color = cogl_gst_sample_video1 (vec2 (cell_st.x, cell_st.z));\n"
    "av_color += cogl_gst_sample_video1 (vec2 (cell_st.y, cell_st.z));\n"
    "av_color += cogl_gst_sample_video1 (vec2 (cell_st.y, cell_st.w));\n"
    "av_color += cogl_gst_sample_video1 (vec2 (cell_st.x, cell_st.w));\n"
    "av_color /= 4.0;\n"

    "grey = av_color.r * 0.2126 + av_color.g * 0.7152 + av_color.b * 0.0722;\n"

    "if (anti_scale == 1)\n"
    "{"
    "pos.xy *= scale_factor * grey;\n"
    "pos.z += z_trans * grey;\n"
    "}"
    "else\n"
    "{"
    "pos.xy *= scale_factor - (scale_factor * grey);\n"
    "pos.z += z_trans - (z_trans * grey);\n"
    "}"
    "pos.x += cell_xy.x;\n"
    "pos.y += cell_xy.y;\n"
    "cogl_position_out = cogl_modelview_projection_matrix * pos;\n");

  engine->shadow_mapping_vertex_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,

                      /* definitions */
                      "uniform mat4 light_shadow_matrix;\n"
                      "varying vec4 shadow_coords;\n",

                      /* post */
                      "shadow_coords = light_shadow_matrix *\n"
                      "                pos;\n");

  engine->blended_discard_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                      /* definitions */
                      NULL,

                      /* post */
                      "if (cogl_color_out.a <= 0.0 ||\n"
                      "    cogl_color_out.a >= "
                      G_STRINGIFY (OPAQUE_THRESHOLD) ")\n"
                      "  discard;\n");

  engine->unblended_discard_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                      /* definitions */
                      NULL,

                      /* post */
                      "if (cogl_color_out.a < "
                      G_STRINGIFY (OPAQUE_THRESHOLD) ")\n"
                      "  discard;\n");

  engine->premultiply_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                      /* definitions */
                      NULL,

                      /* post */

                      /* FIXME: Avoid premultiplying here by fiddling the
                       * blend mode instead which should be more efficient */
                      "cogl_color_out.rgb *= cogl_color_out.a;\n");

  engine->unpremultiply_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                      /* definitions */
                      NULL,

                      /* post */

                      /* FIXME: We need to unpremultiply our colour at this
                       * point to perform lighting, but this is obviously not
                       * ideal and we should instead avoid being premultiplied
                       * at this stage by not premultiplying our textures on
                       * load for example. */
                      "cogl_color_out.rgb /= cogl_color_out.a;\n");

  engine->normal_map_fragment_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
         /* definitions */
         "uniform vec4 light0_ambient, light0_diffuse, light0_specular;\n"
         "uniform vec4 material_ambient, material_diffuse, material_specular;\n"
         "uniform float material_shininess;\n"
         "varying vec3 light_direction, eye_direction;\n",

         /* post */
         "vec4 final_color;\n"

         "vec3 L = normalize(light_direction);\n"

         "vec3 N = texture2D(cogl_sampler7, cogl_tex_coord7_in.st).rgb;\n"
         "N = 2.0 * N - 1.0;\n"
         "N = normalize(N);\n"

         "vec4 ambient = light0_ambient * material_ambient;\n"

         "final_color = ambient * cogl_color_out;\n"
         "float lambert = dot(N, L);\n"

         "if (lambert > 0.0)\n"
         "{\n"
         "  vec4 diffuse = light0_diffuse * material_diffuse;\n"
         "  vec4 specular = light0_specular * material_specular;\n"

         "  final_color += cogl_color_out * diffuse * lambert;\n"

         "  vec3 E = normalize(eye_direction);\n"
         "  vec3 R = reflect (-L, N);\n"
         "  float specular_factor = pow (max(dot(R, E), 0.0),\n"
         "                               material_shininess);\n"
         "  final_color += specular * specular_factor;\n"
         "}\n"

         "cogl_color_out.rgb = final_color.rgb;\n");

  engine->normal_map_video_snippet =
     cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
         /* definitions */
         "uniform vec4 light0_ambient, light0_diffuse, light0_specular;\n"
         "uniform vec4 material_ambient, material_diffuse, material_specular;\n"
         "uniform float material_shininess;\n"
         "varying vec3 light_direction, eye_direction;\n",

         /* post */
         "vec4 final_color;\n"

         "vec3 L = normalize(light_direction);\n"

         "vec3 N = cogl_gst_sample_video7 (cogl_tex_coord7_in.st).rgb;\n"
         "N = 2.0 * N - 1.0;\n"
         "N = normalize(N);\n"

         "vec4 ambient = light0_ambient * material_ambient;\n"

         "final_color = ambient * cogl_color_out;\n"
         "float lambert = dot(N, L);\n"

         "if (lambert > 0.0)\n"
         "{\n"
         "  vec4 diffuse = light0_diffuse * material_diffuse;\n"
         "  vec4 specular = light0_specular * material_specular;\n"

         "  final_color += cogl_color_out * diffuse * lambert;\n"

         "  vec3 E = normalize(eye_direction);\n"
         "  vec3 R = reflect (-L, N);\n"
         "  float specular_factor = pow (max(dot(R, E), 0.0),\n"
         "                               material_shininess);\n"
         "  final_color += specular * specular_factor;\n"
         "}\n"

         "cogl_color_out.rgb = final_color.rgb;\n");


  engine->material_lighting_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
         /* definitions */
         "varying vec3 normal, eye_direction;\n"
         "uniform vec4 light0_ambient, light0_diffuse, light0_specular;\n"
         "uniform vec3 light0_direction_norm;\n"
         "uniform vec4 material_ambient, material_diffuse, material_specular;\n"
         "uniform float material_shininess;\n",

         /* post */
         "vec4 final_color;\n"

         "vec3 L = light0_direction_norm;\n"
         "vec3 N = normalize(normal);\n"

         "vec4 ambient = light0_ambient * material_ambient;\n"

         "final_color = ambient * cogl_color_out;\n"
         "float lambert = dot(N, L);\n"

         "if (lambert > 0.0)\n"
         "{\n"
         "  vec4 diffuse = light0_diffuse * material_diffuse;\n"
         "  vec4 specular = light0_specular * material_specular;\n"

         "  final_color += cogl_color_out * diffuse * lambert;\n"

         "  vec3 E = normalize(eye_direction);\n"
         "  vec3 R = reflect (-L, N);\n"
         "  float specular_factor = pow (max(dot(R, E), 0.0),\n"
         "                               material_shininess);\n"
         "  final_color += specular * specular_factor;\n"
         "}\n"

         "cogl_color_out.rgb = final_color.rgb;\n");

  engine->simple_lighting_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
         /* definitions */
         "varying vec3 normal, eye_direction;\n"
         "uniform vec4 light0_ambient, light0_diffuse, light0_specular;\n"
         "uniform vec3 light0_direction_norm;\n",

         /* post */
         "vec4 final_color;\n"

         "vec3 L = light0_direction_norm;\n"
         "vec3 N = normalize(normal);\n"

         "final_color = light0_ambient * cogl_color_out;\n"
         "float lambert = dot(N, L);\n"

         "if (lambert > 0.0)\n"
         "{\n"
         "  final_color += cogl_color_out * light0_diffuse * lambert;\n"

         "  vec3 E = normalize(eye_direction);\n"
         "  vec3 R = reflect (-L, N);\n"
         "  float specular = pow (max(dot(R, E), 0.0),\n"
         "                        2.);\n"
         "  final_color += light0_specular * vec4(.6, .6, .6, 1.0) *\n"
         "                 specular;\n"
         "}\n"

         "cogl_color_out.rgb = final_color.rgb;\n");


  engine->shadow_mapping_fragment_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                      /* declarations */
                      "varying vec4 shadow_coords;\n",
                      /* post */
                      "vec4 texel10 = texture2D (cogl_sampler10,\n"
                      "                         shadow_coords.xy);\n"
                      "float distance_from_light = texel10.z + 0.0005;\n"
                      "float shadow = 1.0;\n"
                      "if (distance_from_light < shadow_coords.z)\n"
                      "  shadow = 0.5;\n"

                      "cogl_color_out.rgb = shadow * cogl_color_out.rgb;\n");

  engine->pointalism_halo_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
         /* declarations */
         "varying vec4 av_color;\n",

         /* post */
         "cogl_color_out = av_color;\n"
         "cogl_color_out *= texture2D (cogl_sampler0, cogl_tex_coord0_in.st);\n"
         "if (cogl_color_out.a > 0.90 || cogl_color_out.a <= 0.0)\n"
         "  discard;\n");

  engine->pointalism_opaque_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
         /* declarations */
         "varying vec4 av_color;\n",

         /* post */
         "cogl_color_out = av_color;\n"
         "cogl_color_out *= texture2D (cogl_sampler0, cogl_tex_coord0_in.st);\n"
         "if (cogl_color_out.a < 0.90)\n"
         "  discard;\n");

  engine->hair_simple_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                      /* declarations */
                      "varying vec3 normal, eye_direction;\n"
                      "uniform vec4 light0_ambient, light0_diffuse, light0_specular;\n"
                      "uniform vec3 light0_direction_norm;\n",
                      /* post */
                      "vec4 texel = texture2D (cogl_sampler11,\n"
                      "                        cogl_tex_coord11_in.st);\n"
                      "cogl_color_out *= texel;\n"
                      "if (cogl_color_out.a < 0.9) discard;\n"
                      "vec3 E = normalize(eye_direction);\n"
                      "vec3 L = normalize (light0_direction_norm);\n"
                      "vec3 H = normalize (L + E);\n"
                      "vec3 N = normalize (normal);\n"
                      "vec3 Ka = light0_ambient.rgb;\n"
                      "vec3 Kd = vec3 (0.0, 0.0, 0.0);\n"
                      "vec3 Ks = vec3 (0.0, 0.0, 0.0);\n"
                      "float Pd = max (0.0, dot (N, L));\n"
                      "float Ps = max (0.0, dot (N, H));\n"
                      "float u = max (0.0, dot (N, L));\n"
                      "float v = max (0.0, dot (N, H));\n"
                      "if (Pd > 0.0)\n"
                      "  Kd = light0_diffuse.rgb * pow (1.0 - (u * u), Pd / 2.0);\n"
                      "if (Ps > 0.0)\n"
                      "  Ks = light0_specular.rgb * pow (1.0 - (v * v), Ps / 2.0);\n"
                      "vec3 color = Ka + Kd + Ks;\n"
                      "cogl_color_out.rgb *= color;\n"
                      );

  engine->hair_material_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                      /* declarations */
                      "varying vec3 normal, eye_direction;\n"
                      "uniform vec4 light0_ambient, light0_diffuse, light0_specular;\n"
                      "uniform vec3 light0_direction_norm;\n"
                      "uniform vec4 material_ambient, material_diffuse, material_specular;\n"
                      "uniform float material_shininess;\n",
                      /* post */
                      "vec4 texel = texture2D (cogl_sampler11,\n"
                      "                        cogl_tex_coord11_in.st);\n"
                      "cogl_color_out *= texel;\n"
                      "if (cogl_color_out.a < 0.9) discard;\n"
                      "vec3 E = normalize(eye_direction);\n"
                      "vec3 L = normalize (light0_direction_norm);\n"
                      "vec3 H = normalize (L + E);\n"
                      "vec3 N = normalize (normal);\n"
                      "vec3 Ka = light0_ambient.rgb * material_ambient.rgb;\n"
                      "vec3 Kd = vec3 (0.0, 0.0, 0.0);\n"
                      "vec3 Ks = vec3 (0.0, 0.0, 0.0);\n"
                      "float Pd = max (0.0, dot (N, L));\n"
                      "float Ps = max (0.0, dot (N, H));\n"
                      "float u = max (0.0, dot (N, L));\n"
                      "float v = max (0.0, dot (N, H));\n"
                      "if (Pd > 0.0)\n"
                      "  Kd = (light0_diffuse.rgb * material_diffuse.rgb) * pow (1.0 - (u * u), Pd / 2.0);\n"
                      "if (Ps > 0.0)\n"
                      "  Ks = (light0_specular.rgb * material_specular.rgb) * pow (1.0 - (v * v), Ps / 2.0);\n"
                      "vec3 color = Ka + Kd + Ks;\n"
                      "cogl_color_out.rgb *= color;\n"
                      );

  engine->hair_vertex_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,
      /* declarations */
      "uniform float hair_pos;\n",
      /* post */
      "vec4 displace = pos;\n"
      "displace.xyz = cogl_normal_in * hair_pos + displace.xyz;\n"
      "cogl_position_out = cogl_modelview_projection_matrix * displace;\n");

  engine->hair_fin_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,
      "uniform float length;\n",
      "vec4 displace = pos;\n"
      "if (cogl_tex_coord11_in.t < 1.0)\n"
      "displace.xyz += cogl_normal_in * length;\n"
      "cogl_position_out = cogl_modelview_projection_matrix * displace;\n");

  init_dof_pipeline_template (engine);

  init_dof_diamond_pipeline (engine);

  init_dof_unshaped_pipeline (engine);

  init_dof_pipeline (engine);
}

void
rig_renderer_fini (RigEngine *engine)
{
  cogl_object_unref (engine->alpha_mask_snippet);
  cogl_object_unref (engine->alpha_mask_video_snippet);
  cogl_object_unref (engine->lighting_vertex_snippet);
  cogl_object_unref (engine->normal_map_vertex_snippet);
  cogl_object_unref (engine->shadow_mapping_vertex_snippet);
  cogl_object_unref (engine->blended_discard_snippet);
  cogl_object_unref (engine->unblended_discard_snippet);
  cogl_object_unref (engine->premultiply_snippet);
  cogl_object_unref (engine->unpremultiply_snippet);
  cogl_object_unref (engine->normal_map_fragment_snippet);
  cogl_object_unref (engine->normal_map_video_snippet);
  cogl_object_unref (engine->material_lighting_snippet);
  cogl_object_unref (engine->simple_lighting_snippet);
  cogl_object_unref (engine->shadow_mapping_fragment_snippet);
  cogl_object_unref (engine->pointalism_vertex_snippet);
  cogl_object_unref (engine->pointalism_video_snippet);
  cogl_object_unref (engine->pointalism_halo_snippet);
  cogl_object_unref (engine->pointalism_opaque_snippet);
  cogl_object_unref (engine->cache_position_snippet);
}

static void
add_material_for_mask (CoglPipeline *pipeline,
                       RigEngine *engine,
                       RutMaterial *material,
                       RutImageSource **sources)
{
  RutAsset *color_source_asset;

  if (sources[SOURCE_TYPE_ALPHA_MASK])
    {
      /* XXX: We assume a video source is opaque and so never add to
       * mask pipeline. */
      if (!rut_image_source_get_is_video (sources[SOURCE_TYPE_ALPHA_MASK]))
        {
          cogl_pipeline_set_layer_texture (pipeline, 4,
                                           rut_image_source_get_texture (sources[SOURCE_TYPE_ALPHA_MASK]));
          cogl_pipeline_add_snippet (pipeline,
                                     engine->alpha_mask_snippet);

          cogl_pipeline_set_layer_combine (pipeline, 4,
                                           "RGBA=REPLACE(PREVIOUS)",
                                           NULL);
        }
    }

  color_source_asset = rut_material_get_color_source_asset (material);
  if (color_source_asset)
    cogl_pipeline_set_layer_texture (pipeline, 1,
                                     rut_asset_get_texture (color_source_asset));
}

static CoglPipeline *
get_entity_mask_pipeline (RigEngine *engine,
                          RutEntity *entity,
                          RutComponent *geometry,
                          RutMaterial *material,
                          RutImageSource **sources)
{
  CoglPipeline *pipeline;
  RutObject *hair;

  pipeline = get_entity_pipeline_cache (entity, CACHE_SLOT_SHADOW);

  if (pipeline)
    {
      if (sources[SOURCE_TYPE_COLOR] &&
          rut_object_get_type (geometry) == &rut_pointalism_grid_type)
        {
          int location;
          int scale, z;

          if (rut_image_source_get_is_video (sources[SOURCE_TYPE_COLOR]))
            {
              cogl_gst_video_sink_attach_frame (
                rut_image_source_get_sink (sources[SOURCE_TYPE_COLOR]), pipeline);
            }

          scale = rut_pointalism_grid_get_scale (geometry);
          z = rut_pointalism_grid_get_z (geometry);

          location = cogl_pipeline_get_uniform_location (pipeline,
                                                         "scale_factor");
          cogl_pipeline_set_uniform_1f (pipeline, location, scale);

          location = cogl_pipeline_get_uniform_location (pipeline, "z_trans");
          cogl_pipeline_set_uniform_1f (pipeline, location, z);

          location = cogl_pipeline_get_uniform_location (pipeline, "anti_scale");

          if (rut_pointalism_grid_get_lighter (geometry))
            cogl_pipeline_set_uniform_1i (pipeline, location, 1);
          else
            cogl_pipeline_set_uniform_1i (pipeline, location, 0);
        }

      if (sources[SOURCE_TYPE_ALPHA_MASK])
        {
          int location;
          if (rut_image_source_get_is_video (sources[SOURCE_TYPE_ALPHA_MASK]))
            {
              cogl_gst_video_sink_attach_frame (
                rut_image_source_get_sink (sources[SOURCE_TYPE_ALPHA_MASK]), pipeline);
            }

          location = cogl_pipeline_get_uniform_location (pipeline,
                       "material_alpha_threshold");
          cogl_pipeline_set_uniform_1f (pipeline, location,
                                        material->alpha_mask_threshold);
        }

      return cogl_object_ref (pipeline);
    }

  hair = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_HAIR);

  if (rut_object_get_type (geometry) == &rut_diamond_type)
    {
      pipeline = cogl_object_ref (engine->dof_diamond_pipeline);
      rut_diamond_apply_mask (RUT_DIAMOND (geometry), pipeline);

      if (material)
        add_material_for_mask (pipeline, engine, material, sources);
    }
  else if (rut_object_get_type (geometry) == &rut_shape_type)
    {
      pipeline = cogl_pipeline_copy (engine->dof_unshaped_pipeline);

      if (rut_shape_get_shaped (RUT_SHAPE (geometry)))
        {
          CoglTexture *shape_texture =
            rut_shape_get_shape_texture (RUT_SHAPE (geometry));

          cogl_pipeline_set_layer_texture (pipeline, 0, shape_texture);
        }

      if (material)
        add_material_for_mask (pipeline, engine, material, sources);
    }
  else if (rut_object_get_type (geometry) == &rut_nine_slice_type)
    {
      pipeline = cogl_pipeline_copy (engine->dof_unshaped_pipeline);

      if (material)
        add_material_for_mask (pipeline, engine, material, sources);
    }
  else if (rut_object_get_type (geometry) == &rut_pointalism_grid_type)
    {
      int i;

      pipeline = cogl_pipeline_copy (engine->dof_diamond_pipeline);

      if (material)
        {
          if (sources[0])
            {
              CoglGstVideoSink *sink = rut_image_source_get_sink (sources[0]);

              if (sink && rut_image_source_get_is_video (sources[0]))
                {
                  cogl_gst_video_sink_set_first_layer (sink, 1);
                  cogl_gst_video_sink_set_default_sample (sink, FALSE);
                  cogl_gst_video_sink_setup_pipeline (sink, pipeline);
                  cogl_pipeline_add_snippet (pipeline,
                                             engine->pointalism_video_snippet);
                }
              else if (!sink)
                {
                  cogl_pipeline_set_layer_texture (pipeline, 1,
                    rut_image_source_get_texture (sources[0]));
                  cogl_pipeline_add_snippet (pipeline,
                                             engine->pointalism_vertex_snippet);
                }
             }

          if (sources[1])
            {
              int free_layer = 5;
              CoglGstVideoSink *sink = rut_image_source_get_sink (sources[1]);

              if (sink && rut_image_source_get_is_video (sources[1]))
                {
                  cogl_gst_video_sink_set_first_layer (sink, 4);
                  cogl_gst_video_sink_set_default_sample (sink, FALSE);
                  cogl_gst_video_sink_setup_pipeline (sink, pipeline);
                  free_layer = cogl_gst_video_sink_get_free_layer (sink);
                  cogl_pipeline_add_snippet (pipeline,
                                             engine->alpha_mask_video_snippet);
                }
              else if (!sink)
                {
                  cogl_pipeline_set_layer_texture (pipeline, 4,
                        rut_image_source_get_texture (sources[1]));
                  cogl_pipeline_add_snippet (pipeline,
                                             engine->alpha_mask_snippet);
                }

              for (i = 4; i < free_layer; i++)
                cogl_pipeline_set_layer_combine (pipeline, i,
                                                 "RGBA=REPLACE(PREVIOUS)",
                                                 NULL);
            }
        }
    }
  else
    pipeline = cogl_object_ref (engine->dof_pipeline);

  set_entity_pipeline_cache (entity, CACHE_SLOT_SHADOW, pipeline);

  return pipeline;
}

static void
get_light_modelviewprojection (const CoglMatrix *model_transform,
                               RutEntity  *light,
                               const CoglMatrix *light_projection,
                               CoglMatrix *light_mvp)
{
  const CoglMatrix *light_transform;
  CoglMatrix light_view;

  /* TODO: cache the bias * light_projection * light_view matrix! */

  /* Move the unit engine from [-1,1] to [0,1], column major order */
  float bias[16] = {
    .5f, .0f, .0f, .0f,
    .0f, .5f, .0f, .0f,
    .0f, .0f, .5f, .0f,
    .5f, .5f, .5f, 1.f
  };

  light_transform = rut_entity_get_transform (light);
  cogl_matrix_get_inverse (light_transform, &light_view);

  cogl_matrix_init_from_array (light_mvp, bias);
  cogl_matrix_multiply (light_mvp, light_mvp, light_projection);
  cogl_matrix_multiply (light_mvp, light_mvp, &light_view);

  cogl_matrix_multiply (light_mvp, light_mvp, model_transform);
}

static void
image_source_changed_cb (RutImageSource *source,
                         void *user_data)
{
  RigEngine *engine = user_data;

  rut_shell_queue_redraw (engine->shell);
}

static CoglPipeline *
get_entity_color_pipeline (RigEngine *engine,
                           RutEntity *entity,
                           RutComponent *geometry,
                           RutMaterial *material,
                           RutImageSource **sources,
                           CoglBool blended)
{
  CoglSnippet *snippet;
  CoglDepthState depth_state;
  CoglPipeline *pipeline;
  CoglPipeline *fin_pipeline;
  CoglFramebuffer *shadow_fb;
  CoglSnippet *blend = engine->blended_discard_snippet;
  CoglSnippet *unblend = engine->unblended_discard_snippet;
  RutObject *hair;
  int i;

  /* TODO: come up with a scheme for minimizing how many separate
   * CoglPipelines we create or at least deriving the pipelines
   * from a small set of templates.
   */

  hair = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_HAIR);

  if (blended)
    {
      pipeline = get_entity_pipeline_cache (entity, CACHE_SLOT_COLOR_BLENDED);
      if (hair)
        fin_pipeline = get_entity_pipeline_cache (entity,
                                                  CACHE_SLOT_HAIR_FINS_BLENDED);
    }
  else
    {
      pipeline = get_entity_pipeline_cache (entity,
                                            CACHE_SLOT_COLOR_UNBLENDED);
      if (hair)
        fin_pipeline = get_entity_pipeline_cache (entity,
                                                  CACHE_SLOT_HAIR_FINS_UNBLENDED);
    }

  if (pipeline)
    {
      cogl_object_ref (pipeline);
      goto FOUND;
    }

  pipeline = cogl_pipeline_new (engine->ctx->cogl_context);

  if (sources[SOURCE_TYPE_COLOR])
    {
      if (!rut_image_source_get_is_video (sources[SOURCE_TYPE_COLOR]))
        cogl_pipeline_set_layer_texture (pipeline, 1,
          rut_image_source_get_texture (sources[SOURCE_TYPE_COLOR]));
      else
        {
          CoglGstVideoSink *sink = rut_image_source_get_sink (sources[SOURCE_TYPE_COLOR]);
          cogl_gst_video_sink_set_first_layer (sink, 1);
          cogl_gst_video_sink_set_default_sample (sink, TRUE);
          cogl_gst_video_sink_setup_pipeline (sink, pipeline);
        }
    }

  if (sources[SOURCE_TYPE_ALPHA_MASK])
    {
      if (!rut_image_source_get_is_video (sources[SOURCE_TYPE_ALPHA_MASK]))
        cogl_pipeline_set_layer_texture (pipeline, 4,
          rut_image_source_get_texture (sources[SOURCE_TYPE_ALPHA_MASK]));
      else
        {
          CoglGstVideoSink *sink = rut_image_source_get_sink (sources[SOURCE_TYPE_ALPHA_MASK]);
          cogl_gst_video_sink_set_first_layer (sink, 4);
          cogl_gst_video_sink_set_default_sample (sink, FALSE);
          cogl_gst_video_sink_setup_pipeline (sink, pipeline);
        }
    }

  if (sources[SOURCE_TYPE_NORMAL_MAP])
    {
      if (!rut_image_source_get_is_video (sources[SOURCE_TYPE_NORMAL_MAP]))
        cogl_pipeline_set_layer_texture (pipeline, 7,
          rut_image_source_get_texture (sources[SOURCE_TYPE_NORMAL_MAP]));
      else
        {
          CoglGstVideoSink *sink = rut_image_source_get_sink (sources[SOURCE_TYPE_NORMAL_MAP]);
          cogl_gst_video_sink_set_first_layer (sink, 7);
          cogl_gst_video_sink_set_default_sample (sink, FALSE);
          cogl_gst_video_sink_setup_pipeline (sink, pipeline);
        }
    }

#if 0
  /* NB: Our texture colours aren't premultiplied */
  cogl_pipeline_set_blend (pipeline,
                           "RGB = ADD(SRC_COLOR*(SRC_COLOR[A]), DST_COLOR*(1-SRC_COLOR[A]))"
                           "A   = ADD(SRC_COLOR, DST_COLOR*(1-SRC_COLOR[A]))",
                           NULL);
#endif

#if 0
  if (rut_object_get_type (geometry) == &rut_shape_type)
    rut_geometry_component_update_pipeline (geometry, pipeline);

  pipeline = cogl_pipeline_new (rut_cogl_context);
#endif

  cogl_pipeline_set_color4f (pipeline, 0.8f, 0.8f, 0.8f, 1.f);

  /* enable depth testing */
  cogl_depth_state_init (&depth_state);
  cogl_depth_state_set_test_enabled (&depth_state, TRUE);

  if (blended)
    cogl_depth_state_set_write_enabled (&depth_state, FALSE);

  cogl_pipeline_set_depth_state (pipeline, &depth_state, NULL);

  cogl_pipeline_add_snippet (pipeline, engine->cache_position_snippet);

  /* Vertex shader setup for lighting */

  cogl_pipeline_add_snippet (pipeline, engine->lighting_vertex_snippet);

  if (sources[SOURCE_TYPE_NORMAL_MAP])
    cogl_pipeline_add_snippet (pipeline, engine->normal_map_vertex_snippet);

  if (rut_entity_get_receive_shadow (entity))
    cogl_pipeline_add_snippet (pipeline, engine->shadow_mapping_vertex_snippet);

  if (rut_object_get_type (geometry) == &rut_nine_slice_type)
    {
#warning "FIXME: This is going to leak closures, if we've already registered a callback!"
      rut_nine_slice_add_update_callback ((RutNineSlice *)geometry,
                                          nine_slice_changed_cb,
                                          NULL,
                                          NULL);
    }
  else if (rut_object_get_type (geometry) == &rut_shape_type)
    {
      CoglTexture *shape_texture;

      if (rut_shape_get_shaped (RUT_SHAPE (geometry)))
        {
          shape_texture =
            rut_shape_get_shape_texture (RUT_SHAPE (geometry));
          cogl_pipeline_set_layer_texture (pipeline, 0, shape_texture);
        }

#warning "FIXME: This is going to leak closures, if we've already registered a callback!"
      rut_shape_add_reshaped_callback (RUT_SHAPE (geometry),
                                       reshape_cb,
                                       NULL,
                                       NULL);
    }
  else if (rut_object_get_type (geometry) == &rut_diamond_type)
    rut_diamond_apply_mask (RUT_DIAMOND (geometry), pipeline);
  else if (rut_object_get_type (geometry) == &rut_pointalism_grid_type &&
           sources[SOURCE_TYPE_COLOR])
    {
#warning "FIXME: This is going to leak closures, if we've already registered a callback!"
      rut_pointalism_grid_add_update_callback ((RutPointalismGrid *)geometry,
                                               pointalism_changed_cb,
                                               NULL,
                                               NULL);

      cogl_pipeline_set_layer_texture (pipeline, 0,
                                       engine->ctx->circle_texture);
      cogl_pipeline_set_layer_filters (pipeline, 0,
        COGL_PIPELINE_FILTER_LINEAR_MIPMAP_LINEAR,
        COGL_PIPELINE_FILTER_LINEAR);

      if (rut_image_source_get_is_video (sources[SOURCE_TYPE_COLOR]))
        cogl_pipeline_add_snippet (pipeline,
                                   engine->pointalism_video_snippet);
      else
        cogl_pipeline_add_snippet (pipeline,
                                   engine->pointalism_vertex_snippet);

      blend = engine->pointalism_halo_snippet;
      unblend = engine->pointalism_opaque_snippet;
    }

  /*if (hair)
    {
      cogl_pipeline_add_snippet (pipeline, engine->hair_fin_snippet);
      rut_hair_set_uniform_location (hair, pipeline,
                                     blended ? RUT_HAIR_SHELL_POSITION_BLENDED :
                                     RUT_HAIR_SHELL_POSITION_UNBLENDED);
    }*/

  /* and fragment shader */

  /* XXX: ideally we wouldn't have to rely on conditionals + discards
   * in the fragment shader to differentiate blended and unblended
   * regions and instead we should let users mark out opaque regions
   * in geometry.
   */
  cogl_pipeline_add_snippet (pipeline,  blended ? blend : unblend);

  cogl_pipeline_add_snippet (pipeline, engine->unpremultiply_snippet);

  if (material)
    {
      if (sources[SOURCE_TYPE_ALPHA_MASK])
        {
          /* We don't want this layer to be automatically modulated with the
           * previous layers so we set its combine mode to "REPLACE" so it
           * will be skipped past and we can sample its texture manually */

          CoglGstVideoSink *sink =
            rut_image_source_get_sink (sources[SOURCE_TYPE_ALPHA_MASK]);

          if (sink && rut_image_source_get_is_video (sources[SOURCE_TYPE_ALPHA_MASK]))
            {
              int free_layer = cogl_gst_video_sink_get_free_layer (sink);
              cogl_pipeline_add_snippet (pipeline,
                                         engine->alpha_mask_video_snippet);
              for (i = 4; i < free_layer; i++)
                cogl_pipeline_set_layer_combine (pipeline, i,
                                                 "RGBA=REPLACE(PREVIOUS)",
                                                 NULL);
            }
          else if (!sink)
            {
              cogl_pipeline_add_snippet (pipeline, engine->alpha_mask_snippet);
              cogl_pipeline_set_layer_combine (pipeline, 4,
                                               "RGBA=REPLACE(PREVIOUS)", NULL);
            }
        }

      if (sources[SOURCE_TYPE_NORMAL_MAP])
        {
          /* We don't want this layer to be automatically modulated with the
           * previous layers so we set its combine mode to "REPLACE" so it
           * will be skipped past and we can sample its texture manually */

          CoglGstVideoSink *sink =
            rut_image_source_get_sink (sources[SOURCE_TYPE_NORMAL_MAP]);

          if (sink && rut_image_source_get_is_video (sources[SOURCE_TYPE_NORMAL_MAP]))
            {
              int free_layer = cogl_gst_video_sink_get_free_layer (sink);
              snippet = engine->normal_map_video_snippet;
              for (i = 7; i < free_layer; i++)
                cogl_pipeline_set_layer_combine (pipeline, i,
                                                 "RGBA=REPLACE(PREVIOUS)",
                                                 NULL);
            }
          else if (!sink)
            {
              snippet = engine->normal_map_fragment_snippet;
              cogl_pipeline_set_layer_combine (pipeline, 7,
                                             "RGBA=REPLACE(PREVIOUS)", NULL);
            }
          else
            snippet = engine->material_lighting_snippet;
        }
      else
        snippet = engine->material_lighting_snippet;
    }
  else
    snippet = engine->simple_lighting_snippet;

  if (!hair)
    cogl_pipeline_add_snippet (pipeline, snippet);

  if (rut_entity_get_receive_shadow (entity))
    {
      /* Hook the shadow map sampling */

      cogl_pipeline_set_layer_texture (pipeline, 10, engine->shadow_map);
      /* For debugging the shadow mapping... */
      //cogl_pipeline_set_layer_texture (pipeline, 7, engine->shadow_color);
      //cogl_pipeline_set_layer_texture (pipeline, 7, engine->gradient);

      /* We don't want this layer to be automatically modulated with the
       * previous layers so we set its combine mode to "REPLACE" so it
       * will be skipped past and we can sample its texture manually */
      cogl_pipeline_set_layer_combine (pipeline, 10, "RGBA=REPLACE(PREVIOUS)",
                                       NULL);

      /* Handle shadow mapping */
      cogl_pipeline_add_snippet (pipeline,
                                 engine->shadow_mapping_fragment_snippet);
    }

  if (hair)
    {
      if (material)
        cogl_pipeline_add_snippet (pipeline, engine->hair_material_snippet);
      else
        cogl_pipeline_add_snippet (pipeline, engine->hair_simple_snippet);

      cogl_pipeline_set_layer_combine (pipeline, 11, "RGBA=REPLACE(PREVIOUS)",
                                       NULL);

      fin_pipeline = cogl_pipeline_copy (pipeline);
      cogl_pipeline_add_snippet (fin_pipeline, engine->hair_fin_snippet);
      cogl_pipeline_add_snippet (pipeline, engine->hair_vertex_snippet);
      rut_hair_set_uniform_location (hair, pipeline,
                                     blended ? RUT_HAIR_SHELL_POSITION_BLENDED :
                                     RUT_HAIR_SHELL_POSITION_UNBLENDED);
      rut_hair_set_uniform_location (hair, fin_pipeline, RUT_HAIR_LENGTH);
    }

  cogl_pipeline_add_snippet (pipeline, engine->premultiply_snippet);

  if (hair)
    cogl_pipeline_add_snippet (fin_pipeline, engine->premultiply_snippet);

  if (!blended)
    {
      cogl_pipeline_set_blend (pipeline, "RGBA = ADD (SRC_COLOR, 0)", NULL);
      set_entity_pipeline_cache (entity, CACHE_SLOT_COLOR_UNBLENDED, pipeline);
      if (hair)
        {
          cogl_pipeline_set_blend (fin_pipeline, "RGBA = ADD (SRC_COLOR, 0)", NULL);
          set_entity_pipeline_cache (entity,
                                     CACHE_SLOT_HAIR_FINS_UNBLENDED, fin_pipeline);
        }
    }
  else
    {
      set_entity_pipeline_cache (entity, CACHE_SLOT_COLOR_BLENDED, pipeline);
      if (hair)
        set_entity_pipeline_cache (entity,
                                   CACHE_SLOT_HAIR_FINS_BLENDED, fin_pipeline);
    }

FOUND:

  /* FIXME: there's lots to optimize about this! */
  shadow_fb = COGL_FRAMEBUFFER (engine->shadow_fb);

  /* update uniforms in pipelines */
  {
    CoglMatrix light_shadow_matrix, light_projection;
    CoglMatrix model_transform;
    const float *light_matrix;
    int location;

    cogl_framebuffer_get_projection_matrix (shadow_fb, &light_projection);

    /* TODO: use Cogl's MatrixStack api so we can update the entity
     * model matrix incrementally as we traverse the scenegraph */
    rut_graphable_get_transform (entity, &model_transform);

    get_light_modelviewprojection (&model_transform,
                                   engine->light,
                                   &light_projection,
                                   &light_shadow_matrix);

    light_matrix = cogl_matrix_get_array (&light_shadow_matrix);

    location = cogl_pipeline_get_uniform_location (pipeline,
                                                   "light_shadow_matrix");
    cogl_pipeline_set_uniform_matrix (pipeline,
                                      location,
                                      4, 1,
                                      FALSE,
                                      light_matrix);
    if (hair)
      cogl_pipeline_set_uniform_matrix (fin_pipeline,
                                        location,
                                        4, 1,
                                        FALSE,
                                        light_matrix);

    for (i = 0; i < 3; i++)
      {
        if (sources[i])
          {
            if (rut_image_source_get_is_video (sources[i]))
              {
                cogl_gst_video_sink_attach_frame (
                  rut_image_source_get_sink (sources[i]), pipeline);

              }
          }
      }
  }

  return pipeline;
}

static void
image_source_ready_cb (RutImageSource *source,
                       void *user_data)
{
  RutEntity *entity = user_data;
  RutContext *ctx = rut_entity_get_context (entity);
  RutImageSource *color_src;
  RutObject *geometry;
  RutMaterial *material;
  int width, height;

  geometry = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_GEOMETRY);
  material = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);

  dirty_entity_pipelines (entity);

  if (material->color_source_asset)
    color_src = get_entity_image_source_cache (entity, SOURCE_TYPE_COLOR);
  else
    color_src = NULL;

  /* If the color source has changed then we may also need to update
   * the geometry according to the size of the color source */
  if (source != color_src)
    return;

  if (rut_image_source_get_is_video (source))
    {
      width = 640;
      height = cogl_gst_video_sink_get_height_for_width (
                 rut_image_source_get_sink (source), width);
    }
  else
    {
      CoglTexture *texture = rut_image_source_get_texture (source);
      width = cogl_texture_get_width (texture);
      height = cogl_texture_get_height (texture);
    }

  /* TODO: make shape/diamond/pointalism image-size-dependant */
  if (rut_object_is (geometry, RUT_INTERFACE_ID_IMAGE_SIZE_DEPENDENT))
    {
      RutImageSizeDependantVTable *dependant =
        rut_object_get_vtable (geometry, RUT_INTERFACE_ID_IMAGE_SIZE_DEPENDENT);
      dependant->set_image_size (geometry, width, height);
    }
  else if (rut_object_get_type (geometry) == &rut_shape_type)
    rut_shape_set_texture_size (RUT_SHAPE (geometry), width, height);
  else if (rut_object_get_type (geometry) == &rut_diamond_type)
    {
      RutDiamond *diamond = geometry;
      float size = rut_diamond_get_size (diamond);

      rut_entity_remove_component (entity, geometry);
      diamond = rut_diamond_new (ctx, size, width, height);
      rut_entity_add_component (entity, geometry);
    }
  else if (rut_object_get_type (geometry) == &rut_pointalism_grid_type)
    {
      RutPointalismGrid *grid = geometry;
      float cell_size, scale, z;
      CoglBool lighter;

      cell_size = rut_pointalism_grid_get_cell_size (grid);
      scale = rut_pointalism_grid_get_scale (grid);
      z = rut_pointalism_grid_get_z (grid);
      lighter = rut_pointalism_grid_get_lighter (grid);

      rut_entity_remove_component (entity, geometry);
      grid = rut_pointalism_grid_new (ctx, cell_size, width, height);

      rut_entity_add_component (entity, grid);
      grid->pointalism_scale = scale;
      grid->pointalism_z = z;
      grid->pointalism_lighter = lighter;
    }
}

static CoglPipeline *
get_entity_pipeline (RigEngine *engine,
                     RutEntity *entity,
                     RutComponent *geometry,
                     RigPass pass)
{
  RutMaterial *material =
    rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);
  RutImageSource *sources[3];

  /* FIXME: Instead of having rut_entity apis for caching image
   * sources, we should allow the renderer to track arbitrary
   * private state with entities so it can better manage caches
   * of different kinds of derived, renderer specific state.
   */

  sources[SOURCE_TYPE_COLOR] =
    get_entity_image_source_cache (entity, SOURCE_TYPE_COLOR);
  sources[SOURCE_TYPE_ALPHA_MASK] =
    get_entity_image_source_cache (entity, SOURCE_TYPE_ALPHA_MASK);
  sources[SOURCE_TYPE_NORMAL_MAP] =
    get_entity_image_source_cache (entity, SOURCE_TYPE_NORMAL_MAP);

  /* Materials may be associated with various image sources which need
   * to be setup before we try creating pipelines and querying the
   * geometry of entities because many components are influenced by
   * the size of associated images being mapped.
   */
  if (material)
    {
      RutAsset *asset = material->color_source_asset;

      if (asset && !sources[SOURCE_TYPE_COLOR])
        {
          sources[SOURCE_TYPE_COLOR] = rut_image_source_new (engine->ctx, asset);

          set_entity_image_source_cache (entity,
                                         SOURCE_TYPE_COLOR,
                                         sources[SOURCE_TYPE_COLOR]);
#warning "FIXME: we need to track this as renderer priv since we're leaking closures a.t.m"
          rut_image_source_add_ready_callback (sources[SOURCE_TYPE_COLOR],
                                               image_source_ready_cb,
                                               entity, NULL);
          rut_image_source_add_ready_callback (sources[SOURCE_TYPE_COLOR],
                                               rig_engine_dirty_properties_menu,
                                               engine, NULL);
          rut_image_source_add_on_changed_callback (sources[SOURCE_TYPE_COLOR],
                                                    image_source_changed_cb,
                                                    engine,
                                                    NULL);

        }

      asset = material->alpha_mask_asset;

      if (asset && !sources[SOURCE_TYPE_ALPHA_MASK])
        {
          sources[SOURCE_TYPE_ALPHA_MASK] = rut_image_source_new (engine->ctx, asset);

          set_entity_image_source_cache (entity, 1, sources[SOURCE_TYPE_ALPHA_MASK]);
#warning "FIXME: we need to track this as renderer priv since we're leaking closures a.t.m"
          rut_image_source_add_ready_callback (sources[SOURCE_TYPE_ALPHA_MASK],
                                               image_source_ready_cb,
                                               entity, NULL);
          rut_image_source_add_ready_callback (sources[SOURCE_TYPE_ALPHA_MASK],
                                               rig_engine_dirty_properties_menu,
                                               engine, NULL);
          rut_image_source_add_on_changed_callback (sources[SOURCE_TYPE_ALPHA_MASK],
                                                    image_source_changed_cb,
                                                    engine,
                                                    NULL);

        }

      asset = material->normal_map_asset;

      if (asset && !sources[SOURCE_TYPE_NORMAL_MAP])
        {
          sources[SOURCE_TYPE_NORMAL_MAP] = rut_image_source_new (engine->ctx, asset);

          set_entity_image_source_cache (entity, 2, sources[SOURCE_TYPE_NORMAL_MAP]);
#warning "FIXME: we need to track this as renderer priv since we're leaking closures a.t.m"
          rut_image_source_add_ready_callback (sources[SOURCE_TYPE_NORMAL_MAP],
                                               image_source_ready_cb,
                                               entity, NULL);
          rut_image_source_add_ready_callback (sources[SOURCE_TYPE_NORMAL_MAP],
                                               rig_engine_dirty_properties_menu,
                                               engine, NULL);
          rut_image_source_add_on_changed_callback (sources[SOURCE_TYPE_NORMAL_MAP],
                                                    image_source_changed_cb,
                                                    engine,
                                                    NULL);
        }
    }

  if (pass == RIG_PASS_COLOR_UNBLENDED)
    return get_entity_color_pipeline (engine, entity,
                                      geometry, material, sources, FALSE);
  else if (pass == RIG_PASS_COLOR_BLENDED)
    return get_entity_color_pipeline (engine, entity,
                                      geometry, material, sources, TRUE);
  else if (pass == RIG_PASS_DOF_DEPTH || pass == RIG_PASS_SHADOW)
    return get_entity_mask_pipeline (engine, entity,
                                     geometry, material, sources);

  g_warn_if_reached ();
  return NULL;
}
static void
get_normal_matrix (const CoglMatrix *matrix,
                   float *normal_matrix)
{
  CoglMatrix inverse_matrix;

  /* Invert the matrix */
  cogl_matrix_get_inverse (matrix, &inverse_matrix);

  /* Transpose it while converting it to 3x3 */
  normal_matrix[0] = inverse_matrix.xx;
  normal_matrix[1] = inverse_matrix.xy;
  normal_matrix[2] = inverse_matrix.xz;

  normal_matrix[3] = inverse_matrix.yx;
  normal_matrix[4] = inverse_matrix.yy;
  normal_matrix[5] = inverse_matrix.yz;

  normal_matrix[6] = inverse_matrix.zx;
  normal_matrix[7] = inverse_matrix.zy;
  normal_matrix[8] = inverse_matrix.zz;
}

static void
ensure_renderer_priv (RutEntity *entity, RigRenderer *renderer)
{
  /* If this entity was last renderered with some other renderer then
   * we free any private state associated with the previous renderer
   * before creating our own private state */
  if (entity->renderer_priv)
    {
      RutObject *renderer = *(RutObject **)entity->renderer_priv;
      if (rut_object_get_type (renderer) != &rig_renderer_type)
        rut_renderer_free_priv (renderer, entity);
    }

  if (!entity->renderer_priv)
    {
      RigRendererPriv *priv = g_slice_new0 (RigRendererPriv);

      priv->renderer = renderer;
      entity->renderer_priv = priv;
    }
}

static void
rig_renderer_flush_journal (RigRenderer *renderer,
                            RigPaintContext *paint_ctx)
{
  GArray *journal = renderer->journal;
  RutPaintContext *rut_paint_ctx = &paint_ctx->_parent;
  RutCamera *camera = rut_paint_ctx->camera;
  CoglFramebuffer *fb = rut_camera_get_framebuffer (camera);
  int start, dir, end;
  int i;

  /* TODO: use an inline qsort implementation */
  g_array_sort (journal, (void *)sort_entry_cb);

  /* We draw opaque geometry front-to-back so we are more likely to be
   * able to discard later fragments earlier by depth testing.
   *
   * We draw transparent geometry back-to-front so it blends
   * correctly.
   */
  if ( paint_ctx->pass == RIG_PASS_COLOR_BLENDED)
    {
      start = 0;
      dir = 1;
      end = journal->len;
    }
  else
    {
      start = journal->len - 1;
      dir = -1;
      end = -1;
    }

  cogl_framebuffer_push_matrix (fb);

  for (i = start; i != end; i += dir)
    {
      RigJournalEntry *entry = &g_array_index (journal, RigJournalEntry, i);
      RutEntity *entity = entry->entity;
      RutComponent *geometry =
        rut_entity_get_component (entity, RUT_COMPONENT_TYPE_GEOMETRY);
      CoglPipeline *pipeline;
      CoglPrimitive *primitive;
      float normal_matrix[9];
      RutMaterial *material;
      CoglPipeline *fin_pipeline = NULL;
      RutHair *hair = rut_entity_get_component (entity,
                                                RUT_COMPONENT_TYPE_HAIR);

      ensure_renderer_priv (entity, renderer);

      material = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);

      pipeline = get_entity_pipeline (paint_ctx->engine,
                                      entity,
                                      geometry,
                                      paint_ctx->pass);

      if (hair)
        {
          if (paint_ctx->pass == RIG_PASS_COLOR_BLENDED)
            fin_pipeline = get_entity_pipeline_cache (entity,
                                                      CACHE_SLOT_HAIR_FINS_BLENDED);
          else
            fin_pipeline = get_entity_pipeline_cache (entity,
                                                      CACHE_SLOT_HAIR_FINS_UNBLENDED);
        }

      if ((paint_ctx->pass == RIG_PASS_DOF_DEPTH ||
          paint_ctx->pass == RIG_PASS_SHADOW))
        {
          /* FIXME: avoid updating these uniforms for every primitive if
           * the focal parameters haven't change! */
          set_focal_parameters (pipeline,
                                camera->focal_distance,
                                camera->depth_of_field);

        }
      else if ((paint_ctx->pass == RIG_PASS_COLOR_UNBLENDED ||
                paint_ctx->pass == RIG_PASS_COLOR_BLENDED))
        {
          int location;
          RutLight *light = rut_entity_get_component (paint_ctx->engine->light,
                                                      RUT_COMPONENT_TYPE_LIGHT);
          /* FIXME: only update the lighting uniforms when the light has
           * actually moved! */
          rut_light_set_uniforms (light, pipeline);
          if (hair)
            rut_light_set_uniforms (light, fin_pipeline);

          /* FIXME: only update the material uniforms when the material has
           * actually changed! */
          material = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);
          if (material)
            {
              rut_material_flush_uniforms (material, pipeline);
              if (hair)
                rut_material_flush_uniforms (material, fin_pipeline);
            }

          get_normal_matrix (&entry->matrix, normal_matrix);

          location = cogl_pipeline_get_uniform_location (pipeline, "normal_matrix");
          cogl_pipeline_set_uniform_matrix (pipeline,
                                            location,
                                            3, /* dimensions */
                                            1, /* count */
                                            FALSE, /* don't transpose again */
                                            normal_matrix);
          if (hair)
            {
              cogl_pipeline_set_uniform_matrix (fin_pipeline,
                                            location,
                                            3, /* dimensions */
                                            1, /* count */
                                            FALSE, /* don't transpose again */
                                            normal_matrix);
            }
        }

      if (rut_object_is (geometry, RUT_INTERFACE_ID_PRIMABLE))
        {
          primitive = get_entity_primitive_cache (entity, 0);
          if (!primitive)
            {
              primitive = rut_primable_get_primitive (geometry);
              set_entity_primitive_cache (entity, 0, primitive);
            }

          cogl_framebuffer_set_modelview_matrix (fb, &entry->matrix);

          if (hair)
            {
              CoglTexture *texture;
              int i, uniform;

              if (paint_ctx->pass == RIG_PASS_COLOR_BLENDED)
                uniform = RUT_HAIR_SHELL_POSITION_BLENDED;
              else if (paint_ctx->pass == RIG_PASS_COLOR_UNBLENDED)
                uniform = RUT_HAIR_SHELL_POSITION_UNBLENDED;
              else if (paint_ctx->pass == RIG_PASS_DOF_DEPTH ||
                       paint_ctx->pass == RIG_PASS_SHADOW)
                uniform = RUT_HAIR_SHELL_POSITION_SHADOW;

              /* FIXME: only update the hair uniforms when they change! */
              /* FIXME: avoid needing to query the uniform locations by
               * name for each primitive! */

              if (rut_object_get_type (geometry) == &rut_model_type)
                {
                  RutModel *model = (RutModel *)geometry;
                  cogl_pipeline_set_layer_texture (fin_pipeline, 11,
                                                   hair->fin_texture);
                  rut_hair_set_uniform_float_value (hair, fin_pipeline,
                                                    RUT_HAIR_LENGTH,
                                                    hair->length);
                  cogl_primitive_draw (model->fin_primitive, fb, fin_pipeline);
                }

              rut_hair_update_state (hair);

              texture = g_array_index (hair->shell_textures, CoglTexture *, 0);

              cogl_pipeline_set_layer_texture (pipeline, 11, texture);

              rut_hair_set_uniform_float_value (hair, pipeline, uniform, 0);

              /* TODO: we should be drawing the original base model as
               * the interior, with depth write and test enabled to
               * make sure we reduce the work involved in blending all
               * the shells on top. */
              cogl_primitive_draw (primitive, fb, pipeline);

              cogl_pipeline_set_alpha_test_function (pipeline,
                                                     COGL_PIPELINE_ALPHA_FUNC_GREATER, 0.49);

              /* TODO: we should support having more shells than
               * real textures... */
              for (i = 1; i < hair->n_shells; i++)
                {
                  float hair_pos = hair->shell_positions[i];

                  texture = g_array_index (hair->shell_textures, CoglTexture *, i);
                  cogl_pipeline_set_layer_texture (pipeline, 11, texture);

                  rut_hair_set_uniform_float_value (hair, pipeline, uniform,
                                                    hair_pos);

                  cogl_primitive_draw (primitive, fb, pipeline);
                }
            }
          else if (!hair)
            cogl_primitive_draw (primitive, fb, pipeline);
        }
      else if (rut_object_get_type (geometry) == &rut_text_type &&
               paint_ctx->pass == RIG_PASS_COLOR_BLENDED)
        {
          cogl_framebuffer_set_modelview_matrix (fb, &entry->matrix);
          rut_paintable_paint (geometry, rut_paint_ctx);
        }

      cogl_object_unref (pipeline);

      rut_refable_unref (entry->entity);
    }

  cogl_framebuffer_pop_matrix (fb);

  g_array_set_size (journal, 0);
}

void
rig_camera_update_view (RigEngine *engine, RutEntity *camera, CoglBool shadow_pass)
{
  RutCamera *camera_component =
    rut_entity_get_component (camera, RUT_COMPONENT_TYPE_CAMERA);
  CoglMatrix transform;
  CoglMatrix inverse_transform;
  CoglMatrix view;

  /* translate to z_2d and scale */
  if (!shadow_pass)
    view = engine->main_view;
  else
    view = engine->identity;

  /* apply the camera viewing transform */
  rut_graphable_get_transform (camera, &transform);
  cogl_matrix_get_inverse (&transform, &inverse_transform);
  cogl_matrix_multiply (&view, &view, &inverse_transform);

  if (shadow_pass)
    {
      CoglMatrix flipped_view;
      cogl_matrix_init_identity (&flipped_view);
      cogl_matrix_scale (&flipped_view, 1, -1, 1);
      cogl_matrix_multiply (&flipped_view, &flipped_view, &view);
      rut_camera_set_view_transform (camera_component, &flipped_view);
    }
  else
    rut_camera_set_view_transform (camera_component, &view);
}

static void
draw_entity_camera_frustum (RigEngine *engine,
                            RutEntity *entity,
                            CoglFramebuffer *fb)
{
  RutCamera *camera =
    rut_entity_get_component (entity, RUT_COMPONENT_TYPE_CAMERA);
  CoglPrimitive *primitive = rut_camera_create_frustum_primitive (camera);
  CoglPipeline *pipeline = cogl_pipeline_new (rut_cogl_context);
  CoglDepthState depth_state;

  /* enable depth testing */
  cogl_depth_state_init (&depth_state);
  cogl_depth_state_set_test_enabled (&depth_state, TRUE);
  cogl_pipeline_set_depth_state (pipeline, &depth_state, NULL);

  rut_util_draw_jittered_primitive3f (fb, primitive, 0.8, 0.6, 0.1);

  cogl_object_unref (primitive);
  cogl_object_unref (pipeline);
}

static RutTraverseVisitFlags
entitygraph_pre_paint_cb (RutObject *object,
                          int depth,
                          void *user_data)
{
  RigPaintContext *paint_ctx = user_data;
  RutPaintContext *rut_paint_ctx = user_data;
  RigRenderer *renderer = paint_ctx->renderer;
  RutCamera *camera = rut_paint_ctx->camera;
  CoglFramebuffer *fb = rut_camera_get_framebuffer (camera);

  if (rut_object_is (object, RUT_INTERFACE_ID_TRANSFORMABLE))
    {
      const CoglMatrix *matrix = rut_transformable_get_matrix (object);
      cogl_framebuffer_push_matrix (fb);
      cogl_framebuffer_transform (fb, matrix);
    }

  if (rut_object_get_type (object) == &rut_entity_type)
    {
      RutEntity *entity = RUT_ENTITY (object);
      RutObject *geometry;
      CoglMatrix matrix;

      if (!rut_entity_get_visible (entity) ||
          (paint_ctx->pass == RIG_PASS_SHADOW && !rut_entity_get_cast_shadow (entity)))
        return RUT_TRAVERSE_VISIT_CONTINUE;

      geometry =
        rut_entity_get_component (object, RUT_COMPONENT_TYPE_GEOMETRY);
      if (!geometry)
        {
          if (!paint_ctx->engine->play_mode &&
              object == paint_ctx->engine->light)
            draw_entity_camera_frustum (paint_ctx->engine, object, fb);
          return RUT_TRAVERSE_VISIT_CONTINUE;
        }

      cogl_framebuffer_get_modelview_matrix (fb, &matrix);
      rig_journal_log (renderer->journal,
                       paint_ctx,
                       entity,
                       &matrix);

      return RUT_TRAVERSE_VISIT_CONTINUE;
    }

  return RUT_TRAVERSE_VISIT_CONTINUE;
}

static RutTraverseVisitFlags
entitygraph_post_paint_cb (RutObject *object,
                           int depth,
                           void *user_data)
{
  if (rut_object_is (object, RUT_INTERFACE_ID_TRANSFORMABLE))
    {
      RutPaintContext *rut_paint_ctx = user_data;
      CoglFramebuffer *fb = rut_camera_get_framebuffer (rut_paint_ctx->camera);
      cogl_framebuffer_pop_matrix (fb);
    }

  return RUT_TRAVERSE_VISIT_CONTINUE;
}

static void
paint_scene (RigPaintContext *paint_ctx)
{
  RigRenderer *renderer = paint_ctx->renderer;
  RutPaintContext *rut_paint_ctx = &paint_ctx->_parent;
  RigEngine *engine = paint_ctx->engine;
  CoglContext *ctx = engine->ctx->cogl_context;
  CoglFramebuffer *fb = rut_camera_get_framebuffer (rut_paint_ctx->camera);

  if (paint_ctx->pass == RIG_PASS_COLOR_UNBLENDED)
    {
      CoglPipeline *pipeline = cogl_pipeline_new (ctx);
      cogl_pipeline_set_color4f (pipeline,
                                 engine->background_color.red,
                                 engine->background_color.green,
                                 engine->background_color.blue,
                                 engine->background_color.alpha);
      cogl_framebuffer_draw_rectangle (fb,
                                       pipeline,
                                       0, 0,
                                       engine->device_width, engine->device_height);
                                       //0, 0, engine->pane_width, engine->pane_height);
      cogl_object_unref (pipeline);
    }

  rut_graphable_traverse (engine->scene,
                          RUT_TRAVERSE_DEPTH_FIRST,
                          entitygraph_pre_paint_cb,
                          entitygraph_post_paint_cb,
                          paint_ctx);

  rig_renderer_flush_journal (renderer, paint_ctx);
}

void
rig_paint_camera_entity (RutEntity *camera, RigPaintContext *paint_ctx)
{
  RutPaintContext *rut_paint_ctx = &paint_ctx->_parent;
  RutCamera *save_camera = rut_paint_ctx->camera;
  RutCamera *camera_component =
    rut_entity_get_component (camera, RUT_COMPONENT_TYPE_CAMERA);

  rut_paint_ctx->camera = camera_component;

  rut_camera_flush (camera_component);
  paint_scene (paint_ctx);
  rut_camera_end_frame (camera_component);

  rut_paint_ctx->camera = save_camera;
}
