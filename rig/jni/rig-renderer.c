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

#include "rig-engine.h"
#include "rig-renderer.h"

typedef enum _CacheSlot
{
  CACHE_SLOT_SHADOW,
  CACHE_SLOT_COLOR_BLENDED,
  CACHE_SLOT_COLOR_UNBLENDED,
} CacheSlot;

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

GArray *
rig_journal_new (void)
{
  return g_array_new (FALSE, FALSE, sizeof (RigJournalEntry));
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
  rig_renderer_dirty_entity_state (entity);
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
                      "attribute vec3 tangent_in;\n"
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
                      "vec4 texel7 = texture2D (cogl_sampler10,\n"
                      "                         shadow_coords.xy);\n"
                      "float distance_from_light = texel7.z + 0.0005;\n"
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

static CoglPipeline *
get_entity_mask_pipeline (RigEngine *engine,
                          RutEntity *entity,
                          RutComponent *geometry)
{
  CoglPipeline *pipeline;
  RutImageSource *sources[2];
  int i;

  pipeline = rut_entity_get_pipeline_cache (entity, CACHE_SLOT_SHADOW);
  sources[0] = rut_entity_get_image_source_cache (entity, 0);
  sources[1] = rut_entity_get_image_source_cache (entity, 1);

  if (pipeline)
    {
      if (sources[0] &&
          rut_object_get_type (geometry) == &rut_pointalism_grid_type)
        {
          int location;
          int scale, z;

          if (rut_image_source_get_is_video (sources[0]))
            {
              cogl_gst_video_sink_attach_frame (
                rut_image_source_get_sink (sources[0]), pipeline);
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

      if (sources[1])
        {
          int location;
          RutMaterial *material =
            rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);
          if (rut_image_source_get_is_video (sources[1]))
            {
              cogl_gst_video_sink_attach_frame (
                rut_image_source_get_sink (sources[1]), pipeline);
            }

          location = cogl_pipeline_get_uniform_location (pipeline,
                       "material_alpha_threshold");
          cogl_pipeline_set_uniform_1f (pipeline, location,
                                        material->alpha_mask_threshold);
        }

      return cogl_object_ref (pipeline);
    }

  /* TODO: move into init() somewhere */
  if (G_UNLIKELY (!engine->dof_pipeline_template))
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

  /* TODO: move into init() somewhere */
  if (G_UNLIKELY (!engine->dof_diamond_pipeline))
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

  /* TODO: move into init() somewhere */
  if (G_UNLIKELY (!engine->dof_unshaped_pipeline))
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

  /* TODO: move into init() somewhere */
  if (G_UNLIKELY (!engine->dof_pipeline))
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

  if (rut_object_get_type (geometry) == &rut_diamond_type)
    {
      RutMaterial *material =
        rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);

      pipeline = cogl_object_ref (engine->dof_diamond_pipeline);
      rut_diamond_apply_mask (RUT_DIAMOND (geometry), pipeline);

      if (material)
        {
          RutAsset *texture_asset = rut_material_get_texture_asset (material);

          if (sources[1])
            {
              CoglGstVideoSink *sink = rut_image_source_get_sink (sources[1]);

              if (sink && rut_image_source_get_is_video (sources[1]))
                {
                  int free_layer;
                  cogl_gst_video_sink_set_first_layer (sink, 4);
                  cogl_gst_video_sink_set_default_sample (sink, FALSE);
                  cogl_gst_video_sink_setup_pipeline (sink, pipeline);
                  free_layer = cogl_gst_video_sink_get_free_layer (sink);
                  cogl_pipeline_add_snippet (pipeline,
                                             engine->alpha_mask_video_snippet);
                  for (i = 4; i < free_layer; i++)
                    cogl_pipeline_set_layer_combine (pipeline, i,
                                                     "RGBA=REPLACE(PREVIOUS)",
                                                     NULL);
                }
              else if (!sink)
                {
                  cogl_pipeline_set_layer_texture (pipeline, 4,
                    rut_image_source_get_texture (sources[1]));
                  cogl_pipeline_add_snippet (pipeline,
                                             engine->alpha_mask_snippet);
                  cogl_pipeline_set_layer_combine (pipeline, 4,
                                                   "RGBA=REPLACE(PREVIOUS)",
                                                   NULL);
                }
            }

          if (texture_asset)
            cogl_pipeline_set_layer_texture (pipeline, 1,
              rut_asset_get_texture (texture_asset));
        }
    }
  else if (rut_object_get_type (geometry) == &rut_shape_type)
    {
      RutMaterial *material =
        rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);

      pipeline = cogl_pipeline_copy (engine->dof_unshaped_pipeline);

      if (rut_shape_get_shaped (RUT_SHAPE (geometry)))
        {
          CoglTexture *shape_texture =
            rut_shape_get_shape_texture (RUT_SHAPE (geometry));

          cogl_pipeline_set_layer_texture (pipeline, 0, shape_texture);
        }

      if (material)
        {
          RutAsset *texture_asset = rut_material_get_texture_asset (material);

          if (sources[1])
            {
              CoglGstVideoSink *sink = rut_image_source_get_sink (sources[1]);

              if (sink && rut_image_source_get_is_video (sources[1]))
                {
                  int free_layer;
                  cogl_gst_video_sink_set_first_layer (sink, 4);
                  cogl_gst_video_sink_set_default_sample (sink, FALSE);
                  cogl_gst_video_sink_setup_pipeline (sink, pipeline);
                  free_layer = cogl_gst_video_sink_get_free_layer (sink);
                  cogl_pipeline_add_snippet (pipeline,
                                             engine->alpha_mask_video_snippet);
                  for (i = 4; i < free_layer; i++)
                    cogl_pipeline_set_layer_combine (pipeline, i,
                                                     "RGBA=REPLACE(PREVIOUS)",
                                                     NULL);
                }
              else if (!sink)
                {
                  cogl_pipeline_set_layer_texture (pipeline, 4,
                    rut_image_source_get_texture (sources[1]));
                  cogl_pipeline_add_snippet (pipeline,
                                             engine->alpha_mask_snippet);

                  cogl_pipeline_set_layer_combine (pipeline, 4,
                                                   "RGBA=REPLACE(PREVIOUS)",
                                                   NULL);
                }
            }

          if (texture_asset)
            cogl_pipeline_set_layer_texture (pipeline, 1,
              rut_asset_get_texture (texture_asset));
        }
    }
  else if (rut_object_get_type (geometry) == &rut_pointalism_grid_type)
    {
      RutMaterial *material =
        rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);

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

  rut_entity_set_pipeline_cache (entity, CACHE_SLOT_SHADOW, pipeline);

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
                           CoglBool blended)
{
  CoglSnippet *snippet;
  CoglDepthState depth_state;
  RutMaterial *material;
  CoglPipeline *pipeline;
  CoglFramebuffer *shadow_fb;
  RutImageSource *sources[3];
  CoglSnippet *blend = engine->blended_discard_snippet;
  CoglSnippet *unblend = engine->unblended_discard_snippet;
  int i;

  sources[0] = rut_entity_get_image_source_cache (entity, 0);
  sources[1] = rut_entity_get_image_source_cache (entity, 1);
  sources[2] = rut_entity_get_image_source_cache (entity, 2);

  material = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);

  if (material)
    {
      RutAsset *asset;
      asset = material->texture_asset;

      if (asset && !sources[0])
        {
          sources[0] = rut_image_source_new (engine->ctx, asset);

          rut_entity_set_image_source_cache (entity, 0, sources[0]);
          rut_image_source_add_ready_callback (sources[0], 
                                               rig_entity_new_image_source,
                                               entity, NULL);
          rut_image_source_add_ready_callback (sources[0],
                                               rig_engine_dirty_properties_menu, 
                                               engine, NULL);
          rut_image_source_add_on_changed_callback (sources[0],
                                                    image_source_changed_cb,
                                                    engine,
                                                    NULL);

        }

      asset = material->alpha_mask_asset;

      if (asset && !sources[1])
        {
          sources[1] = rut_image_source_new (engine->ctx, asset);
          
          rut_entity_set_image_source_cache (entity, 1, sources[1]);
          rut_image_source_add_ready_callback (sources[1],
                                               rig_entity_new_image_source, 
                                               entity, NULL);
          rut_image_source_add_ready_callback (sources[1],
                                               rig_engine_dirty_properties_menu, 
                                               engine, NULL);
          rut_image_source_add_on_changed_callback (sources[1],
                                                    image_source_changed_cb,
                                                    engine,
                                                    NULL);

        }

      asset = material->normal_map_asset;

      if (asset && !sources[2])
        {
          sources[2] = rut_image_source_new (engine->ctx, asset);

          rut_entity_set_image_source_cache (entity, 2, sources[2]);
          rut_image_source_add_ready_callback (sources[2],
                                               rig_entity_new_image_source, 
                                               entity, NULL);
          rut_image_source_add_ready_callback (sources[2],
                                               rig_engine_dirty_properties_menu, 
                                               engine, NULL);
          rut_image_source_add_on_changed_callback (sources[2],
                                                    image_source_changed_cb,
                                                    engine,
                                                    NULL);
        }
    }

  if (blended)
    pipeline = rut_entity_get_pipeline_cache (entity,
                                              CACHE_SLOT_COLOR_BLENDED);
  else
    pipeline = rut_entity_get_pipeline_cache (entity,
                                              CACHE_SLOT_COLOR_UNBLENDED);
  if (pipeline)
    {
      cogl_object_ref (pipeline);
      goto FOUND;
    }

  pipeline = cogl_pipeline_new (engine->ctx->cogl_context);

  material = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);

  if (sources[0])
    {
      if (!rut_image_source_get_is_video (sources[0]))
        cogl_pipeline_set_layer_texture (pipeline, 1,
          rut_image_source_get_texture (sources[0]));
      else
        {
          CoglGstVideoSink *sink = rut_image_source_get_sink (sources[0]);
          cogl_gst_video_sink_set_first_layer (sink, 1);
          cogl_gst_video_sink_set_default_sample (sink, TRUE);
          cogl_gst_video_sink_setup_pipeline (sink, pipeline);
        }
    }

  if (sources[1])
    {
      if (!rut_image_source_get_is_video (sources[1]))
        cogl_pipeline_set_layer_texture (pipeline, 4,
          rut_image_source_get_texture (sources[1]));
      else
        {
          CoglGstVideoSink *sink = rut_image_source_get_sink (sources[1]);
          cogl_gst_video_sink_set_first_layer (sink, 4);
          cogl_gst_video_sink_set_default_sample (sink, FALSE);
          cogl_gst_video_sink_setup_pipeline (sink, pipeline);
        }
    }

  if (sources[2])
    {
      if (!rut_image_source_get_is_video (sources[2]))
        cogl_pipeline_set_layer_texture (pipeline, 7,
          rut_image_source_get_texture (sources[2]));
      else
        {
          CoglGstVideoSink *sink = rut_image_source_get_sink (sources[2]);
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

  if (sources[2])
    cogl_pipeline_add_snippet (pipeline, engine->normal_map_vertex_snippet);

  if (rut_entity_get_receive_shadow (entity))
    cogl_pipeline_add_snippet (pipeline, engine->shadow_mapping_vertex_snippet);

  if (rut_object_get_type (geometry) == &rut_shape_type)
    {
      CoglTexture *shape_texture;

      if (rut_shape_get_shaped (RUT_SHAPE (geometry)))
        {
          shape_texture =
            rut_shape_get_shape_texture (RUT_SHAPE (geometry));
          cogl_pipeline_set_layer_texture (pipeline, 0, shape_texture);
        }

      rut_shape_add_reshaped_callback (RUT_SHAPE (geometry),
                                       reshape_cb,
                                       NULL,
                                       NULL);
    }
  else if (rut_object_get_type (geometry) == &rut_diamond_type)
    rut_diamond_apply_mask (RUT_DIAMOND (geometry), pipeline);
  else if (rut_object_get_type (geometry) == &rut_pointalism_grid_type &&
           sources[0])
    {
      cogl_pipeline_set_layer_texture (pipeline, 0,
                                       engine->ctx->circle_texture);
      cogl_pipeline_set_layer_filters (pipeline, 0,
        COGL_PIPELINE_FILTER_LINEAR_MIPMAP_LINEAR,
        COGL_PIPELINE_FILTER_LINEAR);

      if (rut_image_source_get_is_video (sources[0]))
        cogl_pipeline_add_snippet (pipeline,
                                   engine->pointalism_video_snippet);
      else
        cogl_pipeline_add_snippet (pipeline,
                                   engine->pointalism_vertex_snippet);

      blend = engine->pointalism_halo_snippet;
      unblend = engine->pointalism_opaque_snippet;
    }

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
      if (sources[1])
        {
          /* We don't want this layer to be automatically modulated with the
           * previous layers so we set its combine mode to "REPLACE" so it
           * will be skipped past and we can sample its texture manually */

          CoglGstVideoSink *sink = rut_image_source_get_sink (sources[1]);

          if (sink && rut_image_source_get_is_video (sources[1]))
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

      if (sources[2])
        {
          /* We don't want this layer to be automatically modulated with the
           * previous layers so we set its combine mode to "REPLACE" so it
           * will be skipped past and we can sample its texture manually */

          CoglGstVideoSink *sink = rut_image_source_get_sink (sources[2]);

          if (sink && rut_image_source_get_is_video (sources[2]))
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
        {
          snippet = engine->material_lighting_snippet;
        }
    }
  else
    {
      snippet = engine->simple_lighting_snippet;
    }

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

  cogl_pipeline_add_snippet (pipeline, engine->premultiply_snippet);

  if (!blended)
    {
      cogl_pipeline_set_blend (pipeline, "RGBA = ADD (SRC_COLOR, 0)", NULL);
      rut_entity_set_pipeline_cache (entity,
                                     CACHE_SLOT_COLOR_UNBLENDED, pipeline);
    }
  else
    {
      rut_entity_set_pipeline_cache (entity,
                                     CACHE_SLOT_COLOR_BLENDED, pipeline);
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

    /* XXX: This is pretty bad that we are having to do this. It would
     * be nicer if cogl exposed matrix-stacks publicly so we could
     * maintain the entity model_matrix incrementally as we traverse
     * the scenegraph. */
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

static CoglPipeline *
get_entity_pipeline (RigEngine *engine,
                     RutEntity *entity,
                     RutComponent *geometry,
                     RigPass pass)
{
  if (pass == RIG_PASS_COLOR_UNBLENDED)
    return get_entity_color_pipeline (engine, entity, geometry, FALSE);
  else if (pass == RIG_PASS_COLOR_BLENDED)
    return get_entity_color_pipeline (engine, entity, geometry, TRUE);
  else if (pass == RIG_PASS_DOF_DEPTH || pass == RIG_PASS_SHADOW)
    return get_entity_mask_pipeline (engine, entity, geometry);

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
rig_journal_flush (GArray *journal,
                   RigPaintContext *paint_ctx)
{
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

      material = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);

      pipeline = get_entity_pipeline (paint_ctx->engine,
                                      entity,
                                      geometry,
                                      paint_ctx->pass);

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

          /* FIXME: only update the material uniforms when the material has
           * actually changed! */
          material = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);
          if (material)
            rut_material_flush_uniforms (material, pipeline);

          get_normal_matrix (&entry->matrix, normal_matrix);

          location = cogl_pipeline_get_uniform_location (pipeline, "normal_matrix");
          cogl_pipeline_set_uniform_matrix (pipeline,
                                            location,
                                            3, /* dimensions */
                                            1, /* count */
                                            FALSE, /* don't transpose again */
                                            normal_matrix);
        }

      if (rut_object_is (geometry, RUT_INTERFACE_ID_PRIMABLE))
        {
          primitive = rut_primable_get_primitive (geometry);
          cogl_framebuffer_set_modelview_matrix (fb, &entry->matrix);
          cogl_framebuffer_draw_primitive (fb,
                                           pipeline,
                                           primitive);
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
      rig_journal_log (paint_ctx->engine->journal,
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

  rig_journal_flush (engine->journal, paint_ctx);
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

void
rig_renderer_dirty_entity_state (RutEntity *entity)
{
  rut_entity_set_pipeline_cache (entity, CACHE_SLOT_COLOR_UNBLENDED, NULL);
  rut_entity_set_pipeline_cache (entity, CACHE_SLOT_COLOR_BLENDED, NULL);
  rut_entity_set_pipeline_cache (entity, CACHE_SLOT_SHADOW, NULL);
}

void
rig_entity_new_image_source (RutImageSource *source,
                             void *user_data)
{
  RutEntity *entity = user_data;
  RutImageSource *src;
  RutObject *geometry;
  RutMaterial *material;
  RutContext *ctx;
  int width, height;

  geometry = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_GEOMETRY);
  material = rut_entity_get_component (entity, RUT_COMPONENT_TYPE_MATERIAL);

  if (material->texture_asset)
    {
      ctx = rut_asset_get_context (material->texture_asset);
      src = rut_entity_get_image_source_cache (entity, 0);
    }
  else if (material->alpha_mask_asset)
    {
      ctx = rut_asset_get_context (material->alpha_mask_asset);
      src = rut_entity_get_image_source_cache (entity, 1);
    }
  else
    {
      ctx = rut_asset_get_context (material->normal_map_asset);
      src = rut_entity_get_image_source_cache (entity, 2);
    }

  if (rut_image_source_get_is_video (src))
    {
      width = 640;
      height = cogl_gst_video_sink_get_height_for_width (
                 rut_image_source_get_sink (src), width);
    }
  else
    {
      CoglTexture *texture = rut_image_source_get_texture (src);
      width = cogl_texture_get_width (texture);
      height = cogl_texture_get_height (texture);
    }

  if (rut_object_get_type (geometry) == &rut_shape_type)
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

  rig_renderer_dirty_entity_state (entity);
}

