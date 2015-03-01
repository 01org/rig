/*
 * Rig
 *
 * UI Engine & Editor
 *
 * Copyright (C) 2015  Intel Corporation.
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

#include <uv.h>

#include <clib.h>

#include <rut.h>

#include "rig-native-module.h"
#include "rig-code-module.h"
#include "rig-engine.h"
#include "rig-entity.h"

enum {
    RIG_NATIVE_MODULE_PROP_NAME,
    RIG_NATIVE_MODULE_N_PROPS
};

struct _rig_native_module_t {
    rut_object_base_t _base;

    rig_engine_t *engine;;

    rut_componentable_props_t component;

    rig_code_module_props_t code_module;

    char *name;

    uv_lib_t *lib;

    void (*load)(void);
    void (*update)(void);

    rut_introspectable_props_t introspectable;
    rut_property_t properties[RIG_NATIVE_MODULE_N_PROPS];
};

const char *
rig_native_module_get_name(rut_object_t *object)
{
    rig_native_module_t *module = object;
    return module->name;
}

void
rig_native_module_set_name(rut_object_t *object,
                           const char *name)
{
    rig_native_module_t *module = object;

    if (module->name) {
        c_free(module->name);
        module->name = NULL;
    }

    module->name = c_strdup(name ? name : "");

    rut_property_dirty(&module->engine->shell->property_ctx,
                       &module->properties[RIG_NATIVE_MODULE_PROP_NAME]);
}

static rut_property_spec_t _rig_native_module_prop_specs[] = {
    { .name = "name",
      .nick = "Name of module to load",
      .type = RUT_PROPERTY_TYPE_TEXT,
      .getter.text_type = rig_native_module_get_name,
      .setter.text_type = rig_native_module_set_name,
      .flags = RUT_PROPERTY_FLAG_READWRITE, },
    { NULL }
};

static void
_rig_native_module_free(void *object)
{
    rig_native_module_t *module = object;

#ifdef RIG_ENABLE_DEBUG
    {
        rut_componentable_props_t *component =
            rut_object_get_properties(object, RUT_TRAIT_ID_COMPONENTABLE);
        c_return_if_fail(component->entity == NULL);
    }
#endif

    rut_introspectable_destroy(module);

    if (module->name)
        c_free(module->name);

    if (module->lib) {
        uv_dlclose(module->lib);
        c_free(module->lib);
        module->lib = NULL;
    }

    rut_object_free(rig_native_module_t, object);
}

static rut_object_t *
_rig_native_module_copy(rut_object_t *object)
{
    rig_native_module_t *src_module = object;
    rig_native_module_t *copy = rig_native_module_new(src_module->engine);

    rig_native_module_set_name(copy, src_module->name);

    return copy;
}

static void
_rig_native_module_load(rut_object_t *object)
{
    rig_native_module_t *module = object;

    if (!module->lib) {
        int status;
        struct sym {
            const char *name;
            void *addr;
        } symbols[] = {
            { "load", &module->load },
            { "update", &module->update },
        };
        int i;

        module->lib = c_malloc(sizeof(uv_lib_t));
        status = uv_dlopen(module->name, module->lib);
        if (status) {
            c_warning("Failed to load native module (%s): %s",
                      module->name, uv_dlerror(module->lib));
            c_free(module->lib);
            module->lib = NULL;
        }

        for (i = 0; i < C_N_ELEMENTS(symbols); i++)
            uv_dlsym(module->lib, symbols[i].name, symbols[i].addr);

        if (module->load)
            module->load();
    }
}

static void
_rig_native_module_update(rut_object_t *object)
{
    rig_native_module_t *module = object;

    if (module->update)
        module->update();
}

rut_type_t rig_native_module_type;

static void
_rig_native_module_init_type(void)
{
    static rut_componentable_vtable_t componentable_vtable = {
        .copy = _rig_native_module_copy
    };

    static rig_code_module_vtable_t module_vtable = {
        .load = _rig_native_module_load,
        .update = _rig_native_module_update,
    };

    rut_type_t *type = &rig_native_module_type;
#define TYPE rig_native_module_t

    rut_type_init(type, C_STRINGIFY(TYPE), _rig_native_module_free);
    rut_type_add_trait(type,
                       RUT_TRAIT_ID_COMPONENTABLE,
                       offsetof(TYPE, component),
                       &componentable_vtable);
    rut_type_add_trait(type,
                       RUT_TRAIT_ID_INTROSPECTABLE,
                       offsetof(TYPE, introspectable),
                       NULL); /* no implied vtable */
    rut_ensure_trait_id(&rig_code_module_trait_id);
    rut_type_add_trait(type,
                       rig_code_module_trait_id,
                       offsetof(TYPE, code_module),
                       &module_vtable);

#undef TYPE
}

rig_native_module_t *
rig_native_module_new(rig_engine_t *engine)
{
    rig_native_module_t *module =
        rut_object_alloc0(rig_native_module_t,
                          &rig_native_module_type,
                          _rig_native_module_init_type);

    module->engine = engine;

    module->component.type = RUT_COMPONENT_TYPE_CODE;

    module->code_module.object = module;

    rut_introspectable_init(
        module, _rig_native_module_prop_specs, module->properties);

    rig_native_module_set_name(module, NULL);

    return module;
}
