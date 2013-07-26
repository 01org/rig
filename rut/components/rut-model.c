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

#include <config.h>

#include <math.h>

#include "rut-global.h"
#include "rut-types.h"
#include "rut-geometry.h"
#include "rut-mesh.h"
#include "rut-mesh-ply.h"

#include "components/rut-model.h"

#define PI 3.14159265359

/* The vertex structure */

typedef struct _Vertex
{
  float x, y, z;
  float nx, ny, nz;
  float tx, ty, tz;
  float s0, t0;
  float s1, t1;
  float s4, t4;
  float s7, t7;
}Vertex;

/* The polygon structure */

typedef struct _Polygon
{
	int id;
  Vertex *vertices[3];
  Vertex flat_vertices[3];
  Vertex tangent;
  Vertex normal;
  CoglBool uncovered;
}Polygon;

/* Texture patch structure */

typedef struct _TexturePatch
{
	GList *polygons;
	Polygon *root;
	float tangent_angle;
	float width;
	float height;
}TexturePatch;

typedef struct _RutModelPrivate
{
	GList *texture_patches;
	Polygon *polygons;
	Vertex *vertices;
	int **adj_matrix;
	int n_polygons;
	int n_vertices;
}RutModelPrivate;

/* Some convinient constants */
static Vertex flat_normal = { 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
															0 };

CoglPrimitive *
rut_model_get_primitive (RutObject *object)
{
  RutModel *model = object;

  if (!model->primitive)
    {
      if (model->mesh)
        {
          model->primitive =
            rut_mesh_create_primitive (model->ctx, model->mesh);
        }
    }

  return model->primitive;
}

RutType rut_model_type;

static void
_rut_model_free (void *object)
{
  RutModel *model = object;

  if (model->primitive)
    cogl_object_unref (model->primitive);

  if (model->mesh)
    rut_refable_unref (model->mesh);

  g_free (model->priv);
  g_slice_free (RutModel, model);
}

void
_rut_model_init_type (void)
{
  static RutRefableVTable refable_vtable = {
    rut_refable_simple_ref,
    rut_refable_simple_unref,
    _rut_model_free
  };

  static RutComponentableVTable componentable_vtable = {
    .draw = NULL
  };

  static RutPrimableVTable primable_vtable = {
    .get_primitive = rut_model_get_primitive
  };

  static RutPickableVTable pickable_vtable = {
    .get_mesh = rut_model_get_mesh
  };

  RutType *type = &rut_model_type;

#define TYPE RutModel

  rut_type_init (type, G_STRINGIFY (TYPE));
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_REF_COUNTABLE,
                          offsetof (TYPE, ref_count),
                          &refable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_COMPONENTABLE,
                          offsetof (TYPE, component),
                          &componentable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_PRIMABLE,
                          0, /* no associated properties */
                          &primable_vtable);
  rut_type_add_interface (type,
                          RUT_INTERFACE_ID_PICKABLE,
                          0, /* no associated properties */
                          &pickable_vtable);

#undef TYPE
}

static RutModel *
_rut_model_new (RutContext *ctx)
{
  RutModel *model;

  model = g_slice_new0 (RutModel);
  rut_object_init (&model->_parent, &rut_model_type);
  model->ref_count = 1;
  model->component.type = RUT_COMPONENT_TYPE_GEOMETRY;
  model->ctx = ctx;

  return model;
}

/* Some vertex utility functions */

static Vertex *
substract_vertices (Vertex *v1,
										Vertex *v2)
{
	Vertex *result = g_new (Vertex, 1);

	result->x = v1->x - v2->x;
	result->y = v1->y - v2->y;
	result->z = v1->z - v2->z;

	return result;
}

static float
calculate_magnitude (Vertex *vertex)
{
  return sqrt (pow (vertex->x, 2) + pow (vertex->y, 2) + pow (vertex->z, 2));
}

static float
calculate_dot_product (Vertex *v1,
											 Vertex *v2)
{
  return (v1->x * v2->x) + (v1->y * v2->y) + (v1->z * v2->z);
}

static Vertex *
calculate_cross_product (Vertex *v1,
												 Vertex *v2)
{
	Vertex *result = g_new (Vertex, 1);

	result->x = (v1->y * v2->z) - (v2->y * v1->z);
  result->y = (v1->z * v2->x) - (v2->z * v1->x);
  result->z = (v1->x * v2->y) - (v2->x * v1->y);

	return result;
}

static Vertex *
calculate_triangle_centroid (Vertex *v1,
														 Vertex *v2,
														 Vertex *v3)
{
	Vertex *result = g_new (Vertex, 1);

	result->x = (v1->x + v2->x + v3->x) / 3.f;
	result->y = (v1->y + v2->y + v3->y) / 3.f;
	result->z = (v1->z + v2->z + v3->z) / 3.f;

	return result;
}

static void
rotate_vertex_around_custom_axis (Vertex *vertex,
																	Vertex *axis,
																	float angle)
{
	Vertex w, u, *v;
	float div = calculate_dot_product (vertex, axis) / 
							calculate_dot_product (axis, axis);
	float length = calculate_magnitude (axis);
	float cosine = cos (angle);
	float sine = sin (angle);

	w.x = axis->x * div;
	w.y = axis->y * div;
	w.z = axis->z * div;

	u.x = vertex->x - w.x;
	u.y = vertex->y - w.y;
	u.z = vertex->z - w.z;

	v = calculate_cross_product (axis, &u);

	v->x /= length;
	v->y /= length;
	v->z /= length;

	vertex->x = w.x + u.x * cosine + v->x * sine;
	vertex->y = w.y + u.y * cosine + v->y * sine;
	vertex->z = w.z + u.z * cosine + v->z * sine;

	g_free (v);
}

static void
normalize_vertex (Vertex *vertex)
{
  float magnitude = calculate_magnitude (vertex);

  vertex->x = vertex->x / magnitude;
  vertex->y = vertex->y / magnitude;
  vertex->z = vertex->z / magnitude;
}

static CoglBool
check_vertex_equality (Vertex *v1, Vertex *v2)
{
  return v1->x == v2->x &&
         v1->y == v2->y &&
         v1->z == v2->z;
}

/* Calculate polygon tangent */

static void
calculate_poly_tangent (Polygon *poly)
{
	Vertex *edge1;
  Vertex *edge2;
  float tex_edge1[2];
  float tex_edge2[2];
  float coef;

	edge1 = substract_vertices (poly->vertices[1], poly->vertices[0]);
	edge2 = substract_vertices (poly->vertices[2], poly->vertices[0]);

  tex_edge1[0] = poly->vertices[1]->s0 - poly->vertices[0]->s0;
  tex_edge1[1] = poly->vertices[1]->t0 - poly->vertices[0]->t0;

  tex_edge2[0] = poly->vertices[2]->s0 - poly->vertices[0]->s0;
  tex_edge2[1] = poly->vertices[2]->t0 - poly->vertices[0]->t0;

  coef = 1 / (tex_edge1[0] * tex_edge2[1] - tex_edge2[0] * tex_edge1[1]);

  poly->tangent.x = coef * ((edge1->x * tex_edge2[1]) +
  													(edge2->x * (-1 * tex_edge1[1])));
  poly->tangent.y = coef * ((edge1->y * tex_edge2[1]) +
  													(edge2->y * (-1 * tex_edge1[1])));
  poly->tangent.z = coef * ((edge1->z * tex_edge2[1]) +
  													(edge2->z * (-1 * tex_edge1[1])));

  normalize_vertex (&poly->tangent);

	g_free (edge1);
	g_free (edge2);
}

static void
calculate_vertex_tangent (Polygon *poly,
												 	Vertex *vertex)
{
	Vertex tangent;

	tangent.x = poly->tangent.x + vertex->tx;
	tangent.y = poly->tangent.y + vertex->ty;
	tangent.z = poly->tangent.z + vertex->tz;

	normalize_vertex (&tangent);

	vertex->tx = tangent.x;
	vertex->ty = tangent.y;
	vertex->tz = tangent.z;
}

/* Calculate polygon normal */

static void
calculate_poly_normal (Polygon *poly)
{
	Vertex *edge1;
  Vertex *edge2;
	Vertex *poly_normal;

	edge1 = substract_vertices (poly->vertices[1], poly->vertices[0]);
	edge2 = substract_vertices (poly->vertices[2], poly->vertices[0]);

	poly_normal = calculate_cross_product (edge1, edge2);
	poly->normal.x = poly_normal->x;
	poly->normal.y = poly_normal->y;
	poly->normal.z = poly_normal->z;

  normalize_vertex (&poly->normal);

	g_free (edge1);
	g_free (edge2);
	g_free (poly_normal);
}

static void
calculate_vertex_normal (Polygon *poly,
												 Vertex *vertex)
{
	Vertex normal;

	normal.x = poly->normal.x + vertex->nx;
	normal.y = poly->normal.y + vertex->ny;
	normal.z = poly->normal.z + vertex->nz;

	normalize_vertex (&normal);

	vertex->nx = normal.x;
	vertex->ny = normal.y;
	vertex->nz = normal.z;
}

/* Generate cylindrical uv coordinates for a single vertex */

static void
generate_cylindrical_uv_coordinates (Vertex *vertex,
																		 RutModel *model)
{
	Vertex center, dir1, dir2;
  float angle;

  center.x = (model->min_x + model->max_x) * 0.5;
  center.y = vertex->y;
  center.z = (model->min_z + model->max_z) * 0.5;

  dir2.x = model->min_x - center.x;
  dir2.y = vertex->y - center.y;
  dir2.z = model->min_z - center.z;

	dir1.x = vertex->x - center.x;
	dir1.y = 0;
	dir1.z = vertex->z - center.z;

  angle = atan2 (dir1.x, dir1.z) - atan2 (dir2.x, dir2.z);

  if (angle < 0)
    angle = (2.0 * PI) + angle;

  if (angle > 0)
    vertex->s0 = angle/ (2.0 * PI);
  else
    vertex->s0 = 0;

  vertex->t0 = (vertex->y - model->min_y) / (model->max_y - model->min_y);
}

/* Automatically generate all required properties not included in the 
	 PLY file */

static CoglBool
generate_missing_properties (void **attribute_data_v0,
                             void **attribute_data_v1,
                             void **attribute_data_v2,
                             int v0_index,
                             int v1_index,
                             int v2_index,
                             void *user_data)
{
	float *positions[3] = { attribute_data_v0[0], attribute_data_v1[0],
													attribute_data_v2[0] };
	float *normals[3] = { attribute_data_v0[1], attribute_data_v1[1],
												attribute_data_v2[1] };
	float *tangents[3] = { attribute_data_v0[2], attribute_data_v1[2],
												 attribute_data_v2[2] };
  float *tex_coords0[3] = { attribute_data_v0[3], attribute_data_v1[3],
  												 	attribute_data_v2[3] };
  float *tex_coords1[3] = { attribute_data_v0[4], attribute_data_v1[4],
  												 	attribute_data_v2[4] };	
  float *tex_coords4[3] = { attribute_data_v0[5], attribute_data_v1[5],
  												 	attribute_data_v2[5] };
  float *tex_coords7[3] = { attribute_data_v0[6], attribute_data_v1[6],
  												 	attribute_data_v2[6] };
  int i;
  RutModel *model = user_data;
  Polygon *polygon = &model->priv->polygons[model->priv->n_polygons];

  polygon->id = model->priv->n_polygons;
  polygon->uncovered = TRUE;

	for (i = 0; i < 3; i++)
		{
			polygon->vertices[i] = 
				&model->priv->vertices[model->priv->n_vertices + i];
			polygon->vertices[i]->x = positions[i][0];
			polygon->vertices[i]->y = positions[i][1];
			polygon->vertices[i]->z = positions[i][2];

			polygon->vertices[i]->nx = normals[i][0];
			polygon->vertices[i]->ny = normals[i][1];
			polygon->vertices[i]->nz = normals[i][2];

			if (model->builtin_tex_coords)
				{
					polygon->vertices[i]->s0 = tex_coords0[i][0];
					polygon->vertices[i]->t0 = tex_coords0[i][1];
					polygon->vertices[i]->s1 = tex_coords1[i][0] = tex_coords0[i][0];
					polygon->vertices[i]->t1 = tex_coords1[i][1] = tex_coords0[i][1];
					polygon->vertices[i]->s4 = tex_coords4[i][0] = tex_coords0[i][0];
					polygon->vertices[i]->t4 = tex_coords4[i][1] = tex_coords0[i][1];
					polygon->vertices[i]->s7 = tex_coords7[i][0] = tex_coords0[i][0];
					polygon->vertices[i]->t7 = tex_coords7[i][1] = tex_coords0[i][1];
				}
		}

  if (!model->builtin_tex_coords)
    {
      for (i = 0; i < 3; i++)
        {
          generate_cylindrical_uv_coordinates (polygon->vertices[i], model);
					tex_coords0[i][0] = polygon->vertices[i]->s0;
					tex_coords0[i][1] = polygon->vertices[i]->t0;
					tex_coords1[i][0] = polygon->vertices[i]->s1 = tex_coords0[i][0];
					tex_coords1[i][1] = polygon->vertices[i]->t1 =  tex_coords0[i][1];
					tex_coords4[i][0] = polygon->vertices[i]->s4 = tex_coords0[i][0];
					tex_coords4[i][1] = polygon->vertices[i]->t4 = tex_coords0[i][1];
					tex_coords7[i][0] = polygon->vertices[i]->s7 = tex_coords0[i][0];
					tex_coords7[i][1] = polygon->vertices[i]->t7 = tex_coords0[i][1];
        }
    }

	if (!model->builtin_normals)
		calculate_poly_normal (polygon);
  else
  	{
  		polygon->normal.x = (polygon->vertices[0]->nx + 
  		polygon->vertices[1]->nx + polygon->vertices[2]->nx) / 3.0;
  		polygon->normal.y = (polygon->vertices[0]->ny + 
  		polygon->vertices[1]->ny + polygon->vertices[2]->ny) / 3.0;
  		polygon->normal.z = (polygon->vertices[0]->nz + 
  		polygon->vertices[1]->nz + polygon->vertices[2]->nz) / 3.0;
  		normalize_vertex (&polygon->normal);
  	}

  calculate_poly_tangent (polygon);

	for (i = 0; i < 3; i++)
		{
			if (!model->builtin_normals)
				{
					calculate_vertex_normal (polygon, polygon->vertices[i]);
					normals[i][0] = polygon->vertices[i]->nx;
					normals[i][1] = polygon->vertices[i]->ny;
					normals[i][2] = polygon->vertices[i]->nz;
				}

			calculate_vertex_tangent (polygon, polygon->vertices[i]);
			tangents[i][0] = polygon->vertices[i]->tx;
			tangents[i][1] = polygon->vertices[i]->ty;
			tangents[i][2] = polygon->vertices[i]->tz;
		}

	model->priv->n_polygons++;
	model->priv->n_vertices += 3;

  return TRUE;
}

static CoglBool
copy_tangent_space_data (void **attribute_data_v0,
                         void **attribute_data_v1,
                         void **attribute_data_v2,
                         int v0_index,
                         int v1_index,
                         int v2_index,
                         void *user_data)
{
  RutModel *model = user_data;
  Polygon *polygon = &model->priv->polygons[model->priv->n_polygons];
  float *normals[3] = { attribute_data_v0[0], attribute_data_v1[0],
												attribute_data_v2[0] };
	float *tangents[3] = { attribute_data_v0[1], attribute_data_v1[1],
												 attribute_data_v2[1] };
	int i;

  for (i = 0; i < 3; i++)
    {
      polygon->vertices[i]->tx = tangents[i][0];
      polygon->vertices[i]->ty = tangents[i][1];
      polygon->vertices[i]->tz = tangents[i][2];
      if (!model->builtin_normals)
        {
          polygon->vertices[i]->nx = normals[i][0];
			    polygon->vertices[i]->ny = normals[i][1];
			    polygon->vertices[i]->nz = normals[i][2];
        }
    }

  model->priv->n_polygons++;

  return TRUE;
}

static CoglBool
measure_mesh_x_cb (void **attribute_data,
                   int vertex_index,
                   void *user_data)
{
  RutModel *model = user_data;
  float *pos = attribute_data[0];

  if (pos[0] < model->min_x)
    model->min_x = pos[0];
  if (pos[0] > model->max_x)
    model->max_x = pos[0];

  return TRUE;
}

static CoglBool
measure_mesh_xy_cb (void **attribute_data,
                    int vertex_index,
                    void *user_data)
{
  RutModel *model = user_data;
  float *pos = attribute_data[0];

  measure_mesh_x_cb (attribute_data, vertex_index, user_data);

  if (pos[1] < model->min_y)
    model->min_y = pos[1];
  if (pos[1] > model->max_y)
    model->max_y = pos[1];

  return TRUE;
}

static CoglBool
measure_mesh_xyz_cb (void **attribute_data,
                     int vertex_index,
                     void *user_data)
{
  RutModel *model = user_data;
  float *pos = attribute_data[0];
  float *normal = attribute_data[1];
  float *tangent = attribute_data[2];

  measure_mesh_xy_cb (attribute_data, vertex_index, user_data);

  if (pos[2] < model->min_z)
    model->min_z = pos[2];
  if (pos[2] > model->max_z)
    model->max_z = pos[2];

	if (!model->builtin_normals)
		normal[0] = normal[1] = normal[2] = 0;

	tangent[0] = tangent[1] = tangent[2] = 0;

  return TRUE;
}

/* Gets the angle between 2 vectors relative to a rotation axis (usually their
	 cross product). This function takes the "direction" of the angle / rotation
	 into consideration and adjusts the angle accordingly, avoiding clockwise
	 rotation. */

static float
get_angle_between_vectors (Vertex *start_vector,
													 Vertex *end_vector,
													 Vertex *axis)
{
	float cosine;
  float angle;
	Vertex rotated_vertex; 

	cosine = calculate_dot_product (start_vector, end_vector);
	angle = acos (cosine);

	if (angle == 0.f)
		return 0.f;

	rotated_vertex.x = start_vector->x;
	rotated_vertex.y = start_vector->y;
	rotated_vertex.z = start_vector->z;

	rotate_vertex_around_custom_axis (&rotated_vertex, axis, angle);

	if (calculate_dot_product (&rotated_vertex, end_vector) > 0.9998)
		return angle;

	/* Otherwise this is a clockwise rotation and the angle needs to adjusted */

	angle = PI * 2.f - angle;

	return angle;
}

/* Determine whether 2 polygons are connected i.e. whether they share an edge */

static int
check_for_shared_vertices (Polygon *poly1,
                       		 Polygon *poly2)
{
	Vertex *edges1[3][2];
	Vertex *edges2[3][2];
	int i, j;

	edges1[0][0] = poly1->vertices[0];
  edges1[0][1] = poly1->vertices[1];

  edges1[1][0] = poly1->vertices[1];
  edges1[1][1] = poly1->vertices[2];

  edges1[2][0] = poly1->vertices[2];
  edges1[2][1] = poly1->vertices[0];

  edges2[0][0] = poly2->vertices[0];
  edges2[0][1] = poly2->vertices[1];

  edges2[1][0] = poly2->vertices[1];
  edges2[1][1] = poly2->vertices[2];

  edges2[2][0] = poly2->vertices[2];
  edges2[2][1] = poly2->vertices[0];


  for (i = 0; i < 3; i++)
    {
      for (j = 0; j < 3; j++)
        {
          if (check_vertex_equality (edges1[i][0], edges2[j][0]) &&
              check_vertex_equality (edges1[i][1], edges2[j][1]))
            return 1;
            else if (check_vertex_equality (edges1[i][0], edges2[j][1]) &&
                     check_vertex_equality (edges1[i][1], edges2[j][0]))
            return 1;
        }
    }

  return 0;
}

/* Generate the adjacency matrix */

static int**
generate_adjacency_matrix (RutModel *model)
{
  int **adj_matrix;
  int i, j;

  adj_matrix = g_new (int*, model->priv->n_polygons);

  for (i = 0; i < model->priv->n_polygons; i++)
    {
      Polygon *origin = &model->priv->polygons[i];
      adj_matrix[i] = g_new (int, model->priv->n_polygons);

      for (j = 0; j < model->priv->n_polygons; j++)
        {
          Polygon *child = &model->priv->polygons[j];

          if (origin->id == child->id)
            adj_matrix[origin->id][child->id] = 0;
          else
            adj_matrix[origin->id][child->id] = 
            	check_for_shared_vertices (origin, child);
        }
    }

  return adj_matrix;
}

/* Finds a polygon which hasn't been covered by a patch yet */

static Polygon*
find_uncovered_polygon (RutModel *model)
{
	int i;

	for (i = 0; i < model->priv->n_polygons; i++)
		{
			Polygon *polygon = &model->priv->polygons[i];
			if (polygon->uncovered)
				return polygon;
		}

	return NULL;
}

/* Places the polygon at the origin of a metaphorical 2D plane */

static void 
position_polygon_at_2D_origin (Polygon *polygon)
{
	Vertex *centroid, *axis;
	float angle;
	int i;

	/* 1. Calculate polygon centroid */

	centroid = calculate_triangle_centroid (polygon->vertices[0], 
																					polygon->vertices[1],
															 						polygon->vertices[2]);


	/* 2. Move centroid to origin */

	for (i = 0; i < 3; i++)
		{
			polygon->flat_vertices[i].x = polygon->vertices[i]->x - centroid->x;
			polygon->flat_vertices[i].y = polygon->vertices[i]->y - centroid->y;
			polygon->flat_vertices[i].z = polygon->vertices[i]->z - centroid->z;
		}

	/* 3. Find the angle between the polgyon normal and the 2D plane normal */

	axis = calculate_cross_product (&flat_normal, &polygon->normal);
	angle = get_angle_between_vectors (&polygon->normal, &flat_normal, axis);

	if (angle == 0.f)
		{
			g_free (axis);
			return;
		}


	/* 4. Rotate the polygon so that its normal parallelly aligns with the 
				2D plane one */
	
	for (i = 0; i < 3; i++)
		rotate_vertex_around_custom_axis (&polygon->flat_vertices[i], axis, 
																			angle);
}

/* Align the polygon to the up direction of the 2D plane and the one of the
	 patch itself */

static void
extrude_new_vertex (Polygon *parent,
										Polygon *child)
{
	Vertex *distance;
	CoglBool found_first_set = FALSE;
	int shared_vertices[2][2];
	int i, j;

	for (i = 0; i < 3; i++)
		{
			for (j = 0; j < 3; j++)
				{
					if (check_vertex_equality (parent->vertices[i], child->vertices[j]))
						{
							if (!found_first_set)
								{
									shared_vertices[0][0] = i;
									shared_vertices[0][1] = j;
									found_first_set = TRUE;
								}
							else 
								{
									shared_vertices[1][0] = i;
									shared_vertices[1][1] = j;
									goto FOUND_SHARED_VERTICES;
								}
						}
				}
		}

	FOUND_SHARED_VERTICES :

	distance = substract_vertices (&parent->flat_vertices[shared_vertices[0][0]],
																 &child->flat_vertices[shared_vertices[0][1]]);

	for (i = 0; i < 3; i++)
		{
			child->flat_vertices[i].x += distance->x;
			child->flat_vertices[i].y += distance->y;
			child->flat_vertices[i].z += distance->z;
		}

	g_free (distance);
}

static CoglBool
extract_texture_coordinates (TexturePatch* patch,
														 Polygon *polygon)
{
	int i;
	float x_min = -1 * (patch->width / 2.0);
	float y_min = -1 * (patch->height / 2.0);
	float x_max = patch->width / 2.0;
	float y_max = patch->height / 2.0;
	float new_s[3];
	float new_t[3];

	/* 7. Extract the texture coordinates from the flattened polygon using
				linear interpolation */

	for (i = 0; i < 3; i++)
		{
			new_s[i] = (polygon->flat_vertices[i].x - x_min) / (x_max - x_min);
			new_t[i] = (polygon->flat_vertices[i].y - y_min) / (y_max - x_min);
			if (new_s[i] > 1.0 || new_t[i] > 1.0 || new_s[i] < 0.0 || new_t[i] < 0.0)
				return FALSE;
		}

	for (i = 0; i < 3; i++)
		{	
		  polygon->vertices[i]->s0 = polygon->vertices[i]->s1 =
		  polygon->vertices[i]->s4 = polygon->vertices[i]->s7 = new_s[i];
		  polygon->vertices[i]->t0 = polygon->vertices[i]->t1 =
		  polygon->vertices[i]->t4 = polygon->vertices[i]->t7 = new_t[i];
		}

	return TRUE;
}

static void
grow_texture_patch (RutModel *model,
										TexturePatch *patch)
{
	CoglBool *visited = g_new (CoglBool, model->priv->n_polygons);
	GQueue *stack = g_queue_new ();
	int i;

	for (i = 0; i < model->priv->n_polygons; i++)
		visited[i] = FALSE;

	g_queue_push_tail (stack, patch->root);

	while (!g_queue_is_empty (stack))
		{
			Polygon *parent = g_queue_pop_tail (stack);
			if (!visited[parent->id])
				{
					visited[parent->id] = TRUE;
					for (i = 0; i < model->priv->n_polygons; i++)
						{
							Polygon *child = &model->priv->polygons[i];
						  if (model->priv->adj_matrix[parent->id][child->id] == 1)
								{
									if (!visited[child->id])
										{
											position_polygon_at_2D_origin (child);
          					  extrude_new_vertex (parent, child);
          						if (extract_texture_coordinates (patch, child))
          							{
          								patch->polygons = g_list_prepend (patch->polygons, child);
													g_queue_push_tail (stack, child);
													child->uncovered = FALSE;
												}
										}
								}
						}
				}
		}

	g_free (visited);
	g_queue_free (stack);
}

TexturePatch*
create_texture_patch (RutModel *model,
											int **adj_matrix)
{
	TexturePatch *patch = NULL;
	Polygon *root = NULL;
	Vertex width, height;

	root = find_uncovered_polygon (model);

	if (!root)
		return NULL;

	patch = g_new (TexturePatch, 1);
	patch->polygons = NULL;
	patch->root = root;
	width.x = model->max_x - model->min_x;
	width.y = width.z = 0;
	height.y = model->max_y - model->min_y;
	height.z = height.x = 0;
	patch->width = calculate_magnitude (&width) / 5.0;
	patch->height = calculate_magnitude (&height) / 5.0;

	position_polygon_at_2D_origin (root);

	extract_texture_coordinates (patch, root);
	patch->root->uncovered = FALSE;

	patch->polygons = g_list_prepend (patch->polygons, root);
	grow_texture_patch (model, patch);

	model->priv->texture_patches = g_list_prepend (model->priv->texture_patches, 
																								 patch);

	return patch;
}


RutModel *
rut_model_new_from_mesh (RutContext *ctx,
                         RutMesh *mesh,
                         CoglBool needs_normals,
                         CoglBool needs_tex_coords)
{
  RutModel *model;
  RutAttribute *attribute;
  RutMeshVertexCallback measure_callback;

  model = _rut_model_new (ctx);
  model->type = RUT_MODEL_TYPE_FILE;
  model->mesh = rut_refable_ref (mesh);
  model->patched_mesh = NULL;
  model->priv = g_new (RutModelPrivate, 1);

  attribute = rut_mesh_find_attribute (model->mesh, "cogl_position_in");

  model->min_x = G_MAXFLOAT;
  model->max_x = G_MINFLOAT;
  model->min_y = G_MAXFLOAT;
  model->max_y = G_MINFLOAT;
  model->min_z = G_MAXFLOAT;
  model->max_z = G_MINFLOAT;

	model->priv->texture_patches = NULL;
  model->priv->n_polygons = 0;
  model->priv->n_vertices = 0;
  model->priv->polygons = g_new (Polygon, model->mesh->n_indices / 3);
  model->priv->vertices = g_new (Vertex, model->mesh->n_indices);

	model->builtin_normals = !needs_normals;
	model->builtin_tex_coords = !needs_tex_coords;

  if (attribute->n_components == 1)
    {
      model->min_y = model->max_y = 0;
      model->min_z = model->max_z = 0;
      measure_callback = measure_mesh_x_cb;
    }
  else if (attribute->n_components == 2)
    {
      model->min_z = model->max_z = 0;
      measure_callback = measure_mesh_xy_cb;
    }
  else if (attribute->n_components == 3)
    measure_callback = measure_mesh_xyz_cb;

  rut_mesh_foreach_vertex (model->mesh,
                           measure_callback,
                           model,
                           "cogl_position_in",
                           "cogl_normal_in",
                           "tangent_in",
                           NULL);

	rut_mesh_foreach_triangle (model->mesh,
                             generate_missing_properties,
                             model,
                             "cogl_position_in",
                             "cogl_normal_in",
                             "tangent_in",
                             "cogl_tex_coord0_in",
                             "cogl_tex_coord1_in",
                             "cogl_tex_coord4_in",
                             "cogl_tex_coord7_in",
                             NULL);

  model->priv->n_polygons = 0;                            
  rut_mesh_foreach_triangle (model->mesh,
                             copy_tangent_space_data,
                             model,
                             "cogl_normal_in",
                             "tangent_in",
                             NULL);

  return model;
}

RutModel *
rut_model_new_from_asset (RutContext *ctx, 
													RutAsset *asset,
													CoglBool needs_normals,
													CoglBool needs_tex_coords)
{
  RutMesh *mesh = rut_asset_get_mesh (asset);
  RutModel *model;

  if (!mesh)
    return NULL;

  model = rut_model_new_from_mesh (ctx, mesh, needs_normals, needs_tex_coords);
  model->asset = rut_refable_ref (asset);
  model->patched_mesh = rut_model_get_patched_mesh (model);

  return model;
}

RutMesh *
rut_model_get_mesh (RutObject *self)
{
  RutModel *model = self;
  return model->mesh;
}

RutAsset *
rut_model_get_asset (RutModel *model)
{
  return model->asset;
}


static RutMesh *
create_patched_mesh (RutModel *model)
{
	RutMesh *mesh;
  RutAttribute *attributes[7];
  RutBuffer *vertex_buffer;
  RutBuffer *index_buffer;
  unsigned int *indices = g_new (unsigned int, model->priv->n_vertices);
  int i;

	for (i = 0; i < model->priv->n_vertices; i++)
		indices[i] = i;

	vertex_buffer = rut_buffer_new (sizeof (Vertex) * model->priv->n_vertices);
  index_buffer = rut_buffer_new (sizeof (unsigned int) * 
  															 model->priv->n_vertices);

	memcpy (vertex_buffer->data, model->priv->vertices, sizeof (Vertex) * 
					model->priv->n_vertices);
  memcpy (index_buffer->data, indices, sizeof (unsigned int) * 
  				model->priv->n_vertices);

	attributes[0] = rut_attribute_new (vertex_buffer,
                                     "cogl_position_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, x),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[1] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord0_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, s0),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[2] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord1_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, s1),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[3] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord4_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, s4),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[4] = rut_attribute_new (vertex_buffer,
                                     "cogl_tex_coord7_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, s7),
                                     2,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[5] = rut_attribute_new (vertex_buffer,
                                     "cogl_normal_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, nx),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

  attributes[6] = rut_attribute_new (vertex_buffer,
                                     "tangent_in",
                                     sizeof (Vertex),
                                     offsetof (Vertex, tx),
                                     3,
                                     RUT_ATTRIBUTE_TYPE_FLOAT);

	mesh = rut_mesh_new (COGL_VERTICES_MODE_TRIANGLES, 	
											 model->priv->n_vertices, attributes, 7);
  rut_mesh_set_indices (mesh,
                        COGL_INDICES_TYPE_UNSIGNED_INT,
                        index_buffer,
                        model->priv->n_vertices);

	g_free (indices);
	g_free (model->priv->vertices);
	g_free (model->priv->polygons);

	return mesh;
}

RutMesh *
rut_model_get_patched_mesh (RutObject *object)
{
	RutModel *model = object;
	int i;
	GList *iter;

	if (model->patched_mesh)
		return model->patched_mesh;

	model->priv->adj_matrix = generate_adjacency_matrix (model);

	while (create_texture_patch (model, model->priv->adj_matrix));

	for (i = 0; i < model->priv->n_polygons; i++)
		g_free (model->priv->adj_matrix[i]);

	g_free (model->priv->adj_matrix);

	for (iter = model->priv->texture_patches; iter; iter = iter->next)
		g_free (iter->data);

	g_list_free (model->priv->texture_patches);

	model->patched_mesh = create_patched_mesh (model);

	return model->patched_mesh;
}


