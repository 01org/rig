/*
 * Rut
 *
 * Rig Utilities
 *
 * Copyright (C) 2013  Intel Corporation.
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

/* Note: This API is shared with Rig's runtime code generation */

#ifndef __RUT_PROPERTY_BARE_H__
#define __RUT_PROPERTY_BARE_H__

typedef enum _RutPropertyType
{
  RUT_PROPERTY_TYPE_FLOAT = 1,
  RUT_PROPERTY_TYPE_DOUBLE,
  RUT_PROPERTY_TYPE_INTEGER,
  RUT_PROPERTY_TYPE_ENUM,
  RUT_PROPERTY_TYPE_UINT32,
  RUT_PROPERTY_TYPE_BOOLEAN,
  RUT_PROPERTY_TYPE_TEXT,
  RUT_PROPERTY_TYPE_QUATERNION,
  RUT_PROPERTY_TYPE_VEC3,
  RUT_PROPERTY_TYPE_VEC4,
  RUT_PROPERTY_TYPE_COLOR,
  RUT_PROPERTY_TYPE_OBJECT,

  /* FIXME: instead of supporting RigAsset properties we should
   * support declaring type validation information for RutObject
   * propertys. You should be able to specify a specific RutType or a
   * mask of interfaces.
   */
  RUT_PROPERTY_TYPE_ASSET,
  RUT_PROPERTY_TYPE_POINTER,
} RutPropertyType;

typedef struct _RutBoxed
{
  RutPropertyType type;
  union
    {
      float float_val;
      double double_val;
      int integer_val;
      int enum_val;
      uint32_t uint32_val;
      CoglBool boolean_val;
      char *text_val;
      CoglQuaternion quaternion_val;
      float vec3_val[3];
      float vec4_val[4];
      CoglColor color_val;
      RutObject *object_val;
      RutObject *asset_val;
      void *pointer_val;
    } d;
} RutBoxed;

typedef struct _RutPropertyChange
{
  void *object;
  RutBoxed boxed;
  int prop_id;
} RutPropertyChange;

typedef struct _RutPropertyContext
{
  bool log;
  int magic_marker;
  RutMemoryStack *change_log_stack;
  int log_len;
} RutPropertyContext;

typedef struct _RutProperty RutProperty;

typedef void (*RutPropertyUpdateCallback) (RutProperty *property,
                                           void *user_data);

typedef union _RutPropertyDefault
{
  int integer;
  bool boolean;
  const void *pointer;
} RutPropertyDefault;

typedef struct _RutPropertyValidationInteger
{
  int min;
  int max;
} RutPropertyValidationInteger;

typedef struct _RutPropertyValidationFloat
{
  float min;
  float max;
} RutPropertyValidationFloat;

typedef struct _RutPropertyValidationVec3
{
  float min;
  float max;
} RutPropertyValidationVec3;

typedef struct _RutPropertyValidationVec4
{
  float min;
  float max;
} RutPropertyValidationVec4;

typedef struct _RutPropertyValidationObject
{
  RutType *type;
} RutPropertyValidationObject;

typedef struct _RutPropertyValidationAsset
{
  RigAssetType type;
} RutPropertyValidationAsset;

typedef union _RutPropertyValidation
{
  RutPropertyValidationInteger int_range;
  RutPropertyValidationFloat float_range;
  RutPropertyValidationVec3 vec3_range;
  RutPropertyValidationVec4 vec4_range;
  RutPropertyValidationObject object;
  RutPropertyValidationAsset asset;
  const RutUIEnum *ui_enum;
} RutPropertyValidation;

typedef enum _RutPropertyFlags
{
  RUT_PROPERTY_FLAG_READABLE = 1<<0,
  RUT_PROPERTY_FLAG_WRITABLE = 1<<1,
  RUT_PROPERTY_FLAG_VALIDATE = 1<<2,

  RUT_PROPERTY_FLAG_READWRITE = (RUT_PROPERTY_FLAG_READABLE |
                                 RUT_PROPERTY_FLAG_WRITABLE)
} RutPropertyFlags;

typedef struct _RutPropertySpec
{
  const char *name;

  /* XXX: this might be too limited since it means we can't have
   * dynamically allocated properties that get associated with an
   * object...
   *
   * I suppose though in such a case it's just required to have
   * associated getter and setter functions which means we won't
   * directly reference the data using the offset anyway.
   */
  size_t data_offset;

  /* Note: these are optional. If the property value doesn't
   * need validation then the setter can be left as NULL
   * and if the value is always up to date the getter can
   * also be left as NULL. */
  union
  {
    float (* float_type) (void *object);
    double (* double_type) (void *object);
    int (* integer_type) (void *object);
    int (* enum_type) (void *object);
    uint32_t (* uint32_type) (void *object);
    bool (* boolean_type) (void *object);
    const char *(* text_type) (void *object);
    const CoglQuaternion *(* quaternion_type) (void *object);
    const CoglColor *(* color_type) (void *object);
    const float *(* vec3_type) (void *object);
    const float *(* vec4_type) (void *object);
    void *(* object_type) (void *object);
    RigAsset *(* asset_type) (RutObject *object);
    void *(* pointer_type) (void *object);

    /* This is just used to check the pointer against NULL */
    void *any_type;
  } getter;
  union
  {
    void (* float_type) (void *object, float value);
    void (* double_type) (void *object, double value);
    void (* integer_type) (void *object, int value);
    void (* enum_type) (void *object, int value);
    void (* uint32_type) (void *object, uint32_t value);
    void (* boolean_type) (void *object, bool value);
    void (* text_type) (void *object, const char *value);
    void (* quaternion_type) (void *object,
                              const CoglQuaternion *quaternion);
    void (* color_type) (void *object,
                         const CoglColor *color);
    void (* vec3_type) (void *object, const float value[3]);
    void (* vec4_type) (void *object, const float value[4]);
    void (* object_type) (void *object, void *value);
    void (* asset_type) (RutObject *object, RigAsset *value);
    void (* pointer_type) (void *object, void *value);

    /* This is just used to check the pointer against NULL */
    void *any_type;
  } setter;

  const char *nick;
  const char *blurb;
  RutPropertyFlags flags;
  RutPropertyDefault default_value;
  RutPropertyValidation validation;

  unsigned int type:16;
  unsigned int is_ui_property:1;
  /* Whether this property is allowed to be animatable or not */
  unsigned int animatable:1;
} RutPropertySpec;

/* Note: we intentionally don't pass a pointer to a "source property"
 * that is the property that has changed because RutProperty is
 * designed so that we can defer binding callbacks until the mainloop
 * so we can avoid redundant callbacks in cases where multiple
 * dependencies of a property may be changed. */
typedef void (*RutBindingCallback) (RutProperty *target_property,
                                    void *user_data);

typedef void (*RutBindingDestroyNotify) (RutProperty *property,
                                         void *user_data);

/* XXX: make sure bindings get freed if any of of the dependency
 * properties are destroyed. */
typedef struct _RutPropertyBinding
{
  RutBindingCallback callback;
  RutBindingDestroyNotify destroy_notify;
  void *user_data;
  /* When the property this binding is for gets destroyed we need to
   * know the dependencies so we can remove this property from the
   * corresponding list of dependants for each dependency.
   */
  RutProperty *dependencies[];
} RutPropertyBinding;

struct _RutProperty
{
  const RutPropertySpec *spec;
  GSList *dependants;
  RutPropertyBinding *binding; /* Maybe make this a list of bindings? */
  void *object;

  uint16_t queued_count;
  uint8_t magic_marker;

  /* Most properties are stored in an array associated with an object
   * with an enum to index the array. This will be an index into the
   * array in that case and serves as a unique identifier for the
   * property for the associated object.
   *
   * XXX: consider moving this into the spec:
   */
  uint8_t id; /* NB: This implies we can have no more
                 than 255 properties per object */
};

void
rut_property_dirty (RutPropertyContext *ctx,
                    RutProperty *property);

#define SCALAR_TYPE(SUFFIX, CTYPE, TYPE) \
static inline void \
rut_property_set_ ## SUFFIX (RutPropertyContext *ctx, \
                             RutProperty *property, \
                             CTYPE value) \
{ \
 \
  if (property->spec->setter.any_type) \
    { \
      property->spec->setter.SUFFIX ## _type (property->object, value); \
    } \
  else \
    { \
      CTYPE *data = (CTYPE *)((uint8_t *)property->object + \
                              property->spec->data_offset); \
      \
      c_return_if_fail (property->spec->data_offset != 0); \
      c_return_if_fail (property->spec->type == RUT_PROPERTY_TYPE_ ## TYPE); \
      \
      if (property->spec->getter.any_type == NULL && *data == value) \
        return; \
      \
      *data = value; \
      rut_property_dirty (ctx, property); \
    } \
} \
 \
static inline CTYPE \
rut_property_get_ ## SUFFIX (RutProperty *property) \
{ \
  c_return_val_if_fail (property->spec->type == RUT_PROPERTY_TYPE_ ## TYPE, 0); \
 \
  if (property->spec->getter.any_type) \
    { \
      return property->spec->getter.SUFFIX ## _type (property->object); \
    } \
  else \
    { \
      CTYPE *data = (CTYPE *)((uint8_t *)property->object + \
                              property->spec->data_offset); \
      return *data; \
    } \
}

#define POINTER_TYPE(SUFFIX, CTYPE, TYPE) SCALAR_TYPE(SUFFIX, CTYPE, TYPE)

#define COMPOSITE_TYPE(SUFFIX, CTYPE, TYPE) \
static inline void \
rut_property_set_ ## SUFFIX (RutPropertyContext *ctx, \
                             RutProperty *property, \
                             const CTYPE *value) \
{ \
  c_return_if_fail (property->spec->type == RUT_PROPERTY_TYPE_ ## TYPE); \
 \
  if (property->spec->setter.any_type) \
    { \
      property->spec->setter.SUFFIX ## _type (property->object, value); \
    } \
  else \
    { \
      CTYPE *data = (CTYPE *)((uint8_t *)property->object + \
                              property->spec->data_offset); \
      *data = *value; \
      rut_property_dirty (ctx, property); \
    } \
} \
 \
static inline const CTYPE * \
rut_property_get_ ## SUFFIX (RutProperty *property) \
{ \
  c_return_val_if_fail (property->spec->type == RUT_PROPERTY_TYPE_ ## TYPE, 0); \
 \
  if (property->spec->getter.any_type) \
    { \
      return property->spec->getter.SUFFIX ## _type (property->object); \
    } \
  else \
    { \
      CTYPE *data = (CTYPE *)((uint8_t *)property->object + \
                              property->spec->data_offset); \
      return data; \
    } \
}

#define ARRAY_TYPE(SUFFIX, CTYPE, TYPE, LEN) \
static inline void \
rut_property_set_ ## SUFFIX (RutPropertyContext *ctx, \
                             RutProperty *property, \
                             const CTYPE value[LEN]) \
{ \
 \
  c_return_if_fail (property->spec->type == RUT_PROPERTY_TYPE_ ## TYPE); \
 \
  if (property->spec->setter.any_type) \
    { \
      property->spec->setter.SUFFIX ## _type (property->object, value); \
    } \
  else \
    { \
      CTYPE *data = (CTYPE *) ((uint8_t *) property->object + \
                               property->spec->data_offset); \
      memcpy (data, value, sizeof (CTYPE) * LEN); \
      rut_property_dirty (ctx, property); \
    } \
} \
 \
static inline const CTYPE * \
rut_property_get_ ## SUFFIX (RutProperty *property) \
{ \
  c_return_val_if_fail (property->spec->type == RUT_PROPERTY_TYPE_ ## TYPE, 0); \
 \
  if (property->spec->getter.any_type) \
    { \
      return property->spec->getter.SUFFIX ## _type (property->object); \
    } \
  else \
    { \
      const CTYPE *data = (const CTYPE *) ((uint8_t *) property->object + \
                                           property->spec->data_offset); \
      return data; \
    } \
}

#include "rut-property-types.h"

#undef ARRAY_TYPE
#undef POINTER_TYPE
#undef COMPOSITE_TYPE
#undef SCALAR_TYPE

static inline void
rut_property_set_text (RutPropertyContext *ctx,
                       RutProperty *property,
                       const char *value)
{
  char **data =
    (char **)(uint8_t *)property->object + property->spec->data_offset;

  c_return_if_fail (property->spec->type == RUT_PROPERTY_TYPE_TEXT);

  if (property->spec->setter.any_type)
    {
      property->spec->setter.text_type (property->object, value);
    }
  else
    {
      if (*data)
        c_free (*data);
      *data = c_strdup (value);
      rut_property_dirty (ctx, property);
    }
}

static inline const char *
rut_property_get_text (RutProperty *property)
{
  c_return_val_if_fail (property->spec->type == RUT_PROPERTY_TYPE_TEXT, 0);

  if (property->spec->getter.any_type)
    {
      return property->spec->getter.text_type (property->object);
    }
  else
    {
      const char **data = (const char **)((uint8_t *)property->object +
                                          property->spec->data_offset);
      return *data;
    }
}

#endif /* __RUT_PROPERTY_BARE_H__ */
