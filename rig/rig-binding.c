/*
 * Rig
 *
 * UI Engine & Editor
 *
 * Copyright (C) 2013 Intel Corporation.
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
 *
 */

#include <config.h>

#include <rut.h>

#include "rig-code.h"

typedef struct _RigBinding RigBinding;

typedef struct _Dependency
{
  RigBinding *binding;

  RutObject *object;
  RutProperty *property;

  char *variable_name;

} Dependency;

struct _RigBinding
{
  RutObjectBase _base;

  RigEngine *engine;

  RutProperty *property;

  int binding_id;

  char *expression;

  char *function_name;

  RigCodeNode *function_node;
  RigCodeNode *expression_node;

  CList *dependencies;

  unsigned int active: 1;
};

static void
_rig_bindinc_free (void *object)
{
  RigBinding *binding = object;

  if (binding->expression)
    c_free (binding->expression);

  c_free (binding->function_name);

  rut_graphable_remove_child (binding->function_node);

  rut_object_free (RigBinding, binding);
}

RutType rig_binding_type;

static void
_rig_binding_init_type (void)
{
  rut_type_init (&rig_binding_type, "RigBinding", _rig_bindinc_free);
}

static Dependency *
find_dependency (RigBinding *binding,
                 RutProperty *property)
{
  CList *l;

  for (l = binding->dependencies; l; l = l->next)
    {
      Dependency *dependency = l->data;
      if (dependency->property == property)
        return dependency;
    }

  return NULL;
}

#ifdef RIG_EDITOR_ENABLED
static void
get_property_codegen_info (RutProperty *property,
                           const char **type_name,
                           const char **var_decl_pre,
                           const char **var_decl_post,
                           const char **get_val_pre)
{
  switch (property->spec->type)
    {
    case RUT_PROPERTY_TYPE_ENUM:
      *type_name = "enum";
      *var_decl_pre = "int ";
      *var_decl_post = "";
      *get_val_pre = "int ";
      break;
    case RUT_PROPERTY_TYPE_BOOLEAN:
      *type_name = "boolean";
      *var_decl_pre = "bool ";
      *var_decl_post = "";
      *get_val_pre = "bool ";
      break;
    case RUT_PROPERTY_TYPE_FLOAT:
      *type_name = "float";
      *var_decl_pre = "float ";
      *var_decl_post = "";
      *get_val_pre = "float ";
      break;

    /* FIXME: we want to avoid the use of pointers or "Rut" types in
     * UI logic code... */
    case RUT_PROPERTY_TYPE_OBJECT:
      *type_name = "object";
      *var_decl_pre = "RutObject *";
      *var_decl_post = "";
      *get_val_pre = "const RutObject *";
      break;
    case RUT_PROPERTY_TYPE_ASSET:
      *type_name = "asset";
      *var_decl_pre = "RigAsset *";
      *var_decl_post = "";
      *get_val_pre = "const RigAsset *";
      break;
    case RUT_PROPERTY_TYPE_POINTER:
      *type_name = "pointer";
      *var_decl_pre = "void *";
      *var_decl_post = ";\n";
      *get_val_pre = "const void *";
      break;
    case RUT_PROPERTY_TYPE_TEXT:
      *type_name = "text";
      *var_decl_pre = "char *";
      *var_decl_post = "";
      *get_val_pre = "const char *";
      break;
    case RUT_PROPERTY_TYPE_DOUBLE:
      *type_name = "double";
      *var_decl_pre = "double ";
      *var_decl_post = "";
      *get_val_pre = "double ";
      break;
    case RUT_PROPERTY_TYPE_INTEGER:
      *type_name = "integer";
      *var_decl_pre = "int ";
      *var_decl_post = "";
      *get_val_pre = "int ";
      break;
    case RUT_PROPERTY_TYPE_UINT32:
      *type_name = "uint32";
      *var_decl_pre = "uint32_t ";
      *var_decl_post = "";
      *get_val_pre = "uint32_t ";
      break;

    /* FIXME: we don't want to expose the Cogl api... */
    case RUT_PROPERTY_TYPE_QUATERNION:
      *type_name = "quaternion";
      *var_decl_pre = "CoglQuaternion ";
      *var_decl_post = "";
      *get_val_pre = "const CoglQuaternion *";
      break;
    case RUT_PROPERTY_TYPE_VEC3:
      *type_name = "vec3";
      *var_decl_pre = "float ";
      *var_decl_post = "[3]";
      *get_val_pre = "const float *";
      break;
    case RUT_PROPERTY_TYPE_VEC4:
      *type_name = "vec4";
      *var_decl_pre = "float ";
      *var_decl_post = "[4]";
      *get_val_pre = "const float *";
      break;
    case RUT_PROPERTY_TYPE_COLOR:
      *type_name = "color";
      *var_decl_pre = "CoglColor ";
      *var_decl_post = "";
      *get_val_pre = "const CoglColor *";
      break;
    }
}

static void
codegen_function_node (RigBinding *binding)
{
  RigEngine *engine = binding->engine;
  const char *pre;
  const char *post;
  const char *out_type_name;
  const char *out_var_decl_pre;
  const char *out_var_decl_post;
  const char *out_var_get_pre;
  CList *l;
  int i;

  get_property_codegen_info (binding->property,
                             &out_type_name,
                             &out_var_decl_pre,
                             &out_var_decl_post,
                             &out_var_get_pre);

  c_string_set_size (engine->codegen_string0, 0);
  c_string_set_size (engine->codegen_string1, 0);

  pre =
    "\nvoid\n"
    "%s (RutProperty *_property, void *_user_data)\n"
    "{\n"
    "  RutPropertyContext *_property_ctx = _user_data;\n"
    "  RutProperty **deps = _property->binding->dependencies;\n"
    "  %sout%s;\n";

  c_string_append_printf (engine->codegen_string0, pre,
                          binding->function_name,
                          out_var_decl_pre,
                          out_var_decl_post);

  for (l = binding->dependencies, i = 0; l; l = l->next, i++)
    {
      Dependency *dependency = l->data;
      const char *dep_type_name;
      const char *dep_var_decl_pre;
      const char *dep_var_decl_post;
      const char *dep_get_var_pre;

      get_property_codegen_info (dependency->property,
                                 &dep_type_name,
                                 &dep_var_decl_pre,
                                 &dep_var_decl_post,
                                 &dep_get_var_pre);

      c_string_append_printf (engine->codegen_string0,
                              "  %s %s = rut_property_get_%s (deps[%d]);\n",
                              dep_get_var_pre,
                              dependency->variable_name,
                              dep_type_name,
                              i);
    }

  c_string_append (engine->codegen_string0, "  {\n");

  c_string_set_size (engine->codegen_string1, 0);

  post =
    "\n"
    "  }\n"
    "  rut_property_set_%s (_property_ctx, _property, out);\n"
    "}\n";

  c_string_append_printf (engine->codegen_string1, post,
                          out_type_name);

  rig_code_node_set_pre (binding->function_node,
                         engine->codegen_string0->str);
  rig_code_node_set_post (binding->function_node,
                          engine->codegen_string1->str);
}
#endif /* RIG_EDITOR_ENABLED */

void
rig_binding_activate (RigBinding *binding)
{
  RigEngine *engine = binding->engine;
  RutProperty **dependencies;
  RutBindingCallback callback;
  int n_dependencies;
  CList *l;
  int i;

  c_return_if_fail (!binding->active);

  /* XXX: maybe we should only explicitly remove the binding if we know
   * we've previously set a binding. If we didn't previously set a binding
   * then it would indicate a bug if there were some other binding but we'd
   * hide that by removing it here...
   */
  rut_property_remove_binding (binding->property);

  callback = rig_code_resolve_symbol (engine, binding->function_name);
  if (!callback)
    {
      c_warning ("Failed to lookup binding function symbol \"%s\"",
                 binding->function_name);
      return;
    }

  n_dependencies = c_list_length (binding->dependencies);
  dependencies = g_alloca (sizeof (RutProperty *) * n_dependencies);

  for (l = binding->dependencies, i = 0;
       l;
       l = l->next, i++)
    {
      Dependency *dependency = l->data;
      dependencies[i] = dependency->property;
    }

  _rut_property_set_binding_full_array (binding->property,
                                        callback,
                                        &engine->ctx->property_ctx, /* user data */
                                        NULL, /* destroy */
                                        dependencies,
                                        n_dependencies);
  binding->active = true;
}

void
rig_binding_deactivate (RigBinding *binding)
{
  c_return_if_fail (binding->active);

  rut_property_remove_binding (binding->property);

  binding->active = false;
}

static void
binding_relink_cb (RigCodeNode *node, void *user_data)
{
  RigBinding *binding = user_data;

  if (binding->active)
    {
      rig_binding_deactivate (binding);
      rig_binding_activate (binding);
    }
}

static void
generate_function_node (RigBinding *binding)
{
  RigEngine *engine = binding->engine;

  binding->function_node =
    rig_code_node_new (engine,
                       NULL, /* pre */
                       NULL); /* post */

  rut_graphable_add_child (engine->code_graph, binding->function_node);
  rut_object_unref (binding->function_node);

  rig_code_node_add_link_callback (binding->function_node,
                                   binding_relink_cb,
                                   binding,
                                   NULL); /* destroy */

#ifdef RIG_EDITOR_ENABLED
  if (!engine->simulator)
    codegen_function_node (binding);
#endif
}

void
rig_binding_remove_dependency (RigBinding *binding,
                               RutProperty *property)
{
  Dependency *dependency = find_dependency (binding, property);

  c_return_if_fail (dependency);

  c_free (dependency->variable_name);
  c_slice_free (Dependency, dependency);

#ifdef RIG_EDITOR_ENABLED
  if (!binding->engine->simulator)
    codegen_function_node (binding);
#endif
}

void
rig_binding_add_dependency (RigBinding *binding,
                            RutProperty *property,
                            const char *name)
{
  Dependency *dependency = c_slice_new0 (Dependency);
  RutObject *object = property->object;

  dependency->object = rut_object_ref (object);
  dependency->binding = binding;

  dependency->property = property;

  dependency->variable_name = c_strdup (name);

  binding->dependencies =
    c_list_prepend (binding->dependencies, dependency);

#ifdef RIG_EDITOR_ENABLED
  if (!binding->engine->simulator)
    codegen_function_node (binding);
#endif
}

char *
rig_binding_get_expression (RigBinding *binding)
{
  return binding->expression;
}

void
rig_binding_set_expression (RigBinding *binding,
                            const char *expression)
{
  c_return_if_fail (expression);

  if ((binding->expression &&
       strcmp (binding->expression, expression) == 0))
    return;

  if (binding->expression_node)
    {
      rig_code_node_remove_child (binding->expression_node);
      binding->expression_node = NULL;
    }

  binding->expression_node = rig_code_node_new (binding->engine,
                                                NULL, /* pre */
                                                expression); /* post */

  rig_code_node_add_child (binding->function_node,
                           binding->expression_node);
  rut_object_unref (binding->expression_node);

#ifdef RIG_EDITOR_ENABLED
  if (!binding->engine->simulator)
    codegen_function_node (binding);
#endif
}

void
rig_binding_set_dependency_name (RigBinding *binding,
                                 RutProperty *property,
                                 const char *name)
{
  Dependency *dependency = find_dependency (binding, property);

  c_return_if_fail (dependency);

  c_free (dependency->variable_name);

  dependency->variable_name = c_strdup (name);

#ifdef RIG_EDITOR_ENABLED
  if (!binding->engine->simulator)
    codegen_function_node (binding);
#endif
}

RigBinding *
rig_binding_new (RigEngine *engine,
                 RutProperty *property,
                 int binding_id)
{
  RigBinding *binding = rut_object_alloc0 (RigBinding,
                                           &rig_binding_type,
                                           _rig_binding_init_type);

  binding->engine = engine;
  binding->property = property;
  binding->function_name = c_strdup_printf ("_binding%d", binding_id);
  binding->binding_id = binding_id;

  generate_function_node (binding);

  return binding;
}

int
rig_binding_get_id (RigBinding *binding)
{
  return binding->binding_id;
}

int
rig_binding_get_n_dependencies (RigBinding *binding)
{
  return c_list_length (binding->dependencies);
}

void
rig_binding_foreach_dependency (RigBinding *binding,
                                void (*callback) (RigBinding *binding,
                                                  RutProperty *dependency,
                                                  void *user_data),
                                void *user_data)
{
  CList *l;

  for (l = binding->dependencies; l; l = l->next)
    callback (binding, l->data, user_data);
}

