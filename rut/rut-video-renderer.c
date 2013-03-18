#include "rut-video-renderer.h"

RutGridMesh*
rut_grid_mesh_new_params (int num_vectors,
                          int num_polygons)
{
  RutGridMesh *mesh = g_new (RutGridMesh, 1);
  mesh->vectors = g_new (RutGridVector, num_vectors);
  mesh->polygons = g_new (RutGridPolygon, num_polygons);
  mesh->num_vectors = num_vectors;
  mesh->num_polygons = num_polygons;

  return mesh;
}

RutGridMesh*
generate_grid (int columns, int rows, float size)
{
  RutGridMesh* mesh = rut_grid_mesh_new_params ((columns * rows) * 4,
                                                (columns * rows) * 2);

  float s_iter = 1.0 / columns;
  float t_iter = 1.0 / rows;
  int i = 0;
  int j = 0;
  int col_counter;
  int row_counter;

  float start_x = -1 * ((size * columns) / 2);
  float start_y = -1 * ((size * rows) / 2);

  for (row_counter = 0; row_counter < rows; row_counter++)
    {
      for (col_counter = 0; col_counter < columns; col_counter++)
        {
          float x, y;

          x = start_x + (size / 2);
          y = start_y + (size / 2);

          mesh->vectors[i].x = (-1 * (size / 2));
          mesh->vectors[i].y = (-1 * (size / 2));
          mesh->vectors[i].z = 1;
          mesh->vectors[i].s = 0;
          mesh->vectors[i].t = 0;
          mesh->vectors[i].xs = x;
          mesh->vectors[i].ys = y;
          mesh->vectors[i].s1 = col_counter * s_iter;
          mesh->vectors[i].t1 = row_counter * t_iter;
          mesh->vectors[i].s2 = (col_counter + 1) * s_iter;
          mesh->vectors[i].t2 = (row_counter + 1) * t_iter;
          mesh->vectors[i].s3 = col_counter * s_iter;
          mesh->vectors[i].t3 = row_counter * t_iter;
          i++;

          mesh->vectors[i].x = (size / 2);
          mesh->vectors[i].y = (-1 * (size / 2));
          mesh->vectors[i].z = 1;
          mesh->vectors[i].s = 1;
          mesh->vectors[i].t = 0;
          mesh->vectors[i].xs = x;
          mesh->vectors[i].ys = y;
          mesh->vectors[i].s1 = col_counter * s_iter;
          mesh->vectors[i].t1 = row_counter * t_iter;
          mesh->vectors[i].s2 = (col_counter + 1) * s_iter;
          mesh->vectors[i].t2 = (row_counter + 1) * t_iter;
          mesh->vectors[i].s3 = (col_counter + 1) * s_iter;
          mesh->vectors[i].t3 = row_counter * t_iter;
          i++;

          mesh->vectors[i].x = (size / 2);
          mesh->vectors[i].y = (size / 2);
          mesh->vectors[i].z = 1;
          mesh->vectors[i].s = 1;
          mesh->vectors[i].t = 1;
          mesh->vectors[i].xs = x;
          mesh->vectors[i].ys = y;
          mesh->vectors[i].s1 = col_counter * s_iter;
          mesh->vectors[i].t1 = row_counter * t_iter;
          mesh->vectors[i].s2 = (col_counter + 1) * s_iter;
          mesh->vectors[i].t2 = (row_counter + 1) * t_iter;
          mesh->vectors[i].s3 = (col_counter + 1) * s_iter;
          mesh->vectors[i].t3 = (row_counter + 1) * t_iter;
          i++;

          mesh->vectors[i].x = (-1 * (size / 2));
          mesh->vectors[i].y = (size / 2);
          mesh->vectors[i].z = 1;
          mesh->vectors[i].s = 0;
          mesh->vectors[i].t = 1;
          mesh->vectors[i].xs = x;
          mesh->vectors[i].ys = y;
          mesh->vectors[i].s1 = col_counter * s_iter;
          mesh->vectors[i].t1 = row_counter * t_iter;
          mesh->vectors[i].s2 = (col_counter + 1) * s_iter;
          mesh->vectors[i].t2 = (row_counter + 1) * t_iter;
          mesh->vectors[i].s3 = col_counter * s_iter;
          mesh->vectors[i].t3 = (row_counter + 1) * t_iter;
          i++;

          mesh->polygons[j].indices[0] = i - 4;
          mesh->polygons[j].indices[1] = i - 3;
          mesh->polygons[j].indices[2] = i - 1;
          j++;

          mesh->polygons[j].indices[0] = i - 1;
          mesh->polygons[j].indices[1] = i - 2;
          mesh->polygons[j].indices[2] = i - 3;
          j++;

          start_x += size;
        }

      start_x = -1 * ((size * columns) / 2);
      start_y += size;
    }

  return mesh;
}

RutVideoRenderer*
rut_video_renderer_new (CoglContext *ctx, int cols, int rows, float size)
{
  RutVideoRenderer *renderer = g_new (RutVideoRenderer, 1);
  CoglAttributeBuffer *vertex_buffer;
  CoglIndexBuffer* index_buffer;
  int i, j;

  renderer->grid = generate_grid (cols, rows, size);
  vertex_buffer = cogl_attribute_buffer_new (ctx, renderer->grid->num_vectors *
                                             sizeof (RutGridVector),
                                             renderer->grid->vectors);

  renderer->attributes[0] = cogl_attribute_new (vertex_buffer,
                                                "cogl_position_in",
                                                sizeof (RutGridVector),
                                                offsetof (RutGridVector, x), 3,
                                                COGL_ATTRIBUTE_TYPE_FLOAT);

  renderer->attributes[1] = cogl_attribute_new (vertex_buffer,
                                                "cogl_tex_coord0_in",
                                                sizeof (RutGridVector),
                                                offsetof (RutGridVector, s), 2,
                                                COGL_ATTRIBUTE_TYPE_FLOAT);

  renderer->attributes[2] = cogl_attribute_new (vertex_buffer,
                                                "cell_st",
                                                sizeof (RutGridVector),
                                                offsetof (RutGridVector, s1), 4,
                                                COGL_ATTRIBUTE_TYPE_FLOAT);

  renderer->attributes[3] = cogl_attribute_new (vertex_buffer,
                                                "cogl_tex_coord1_in",
                                                sizeof (RutGridVector),
                                                offsetof (RutGridVector, s3), 2,
                                                COGL_ATTRIBUTE_TYPE_FLOAT);

  renderer->attributes[4] = cogl_attribute_new (vertex_buffer,
                                                "cell_xy",
                                                sizeof (RutGridVector),
                                                offsetof (RutGridVector, xs), 2,
                                                COGL_ATTRIBUTE_TYPE_FLOAT);

  index_buffer = cogl_index_buffer_new (ctx, (renderer->grid->num_polygons * 3)
                                        * sizeof (unsigned int));

  for (j = 0; j < renderer->grid->num_polygons; j++)
    {
      for (i = 0; i < 3; i++)
        {
          cogl_buffer_set_data ((CoglBuffer*) index_buffer,
                                ((j * 3) + i)* sizeof (unsigned int),
                                &renderer->grid->polygons[j].indices[i],
                                sizeof (unsigned int), NULL);
        }
    }

  renderer->indices = cogl_indices_new_for_buffer (COGL_INDICES_TYPE_UNSIGNED_INT,
                                                   index_buffer, 0);

  //cogl_object_unref (index_buffer);

  return renderer;
}
