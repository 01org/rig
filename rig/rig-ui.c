/*
 * Rig
 *
 * UI Engine & Editor
 *
 * Copyright (C) 2014  Intel Corporation.
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
 */

#include <config.h>

#include <math.h>

#include <rut.h>

#include "rig-ui.h"
#include "rig-code.h"
#include "rig-entity.h"

#include "rig-renderer.h"

static void
_rig_ui_free(void *object)
{
    rig_ui_t *ui = object;
    c_list_t *l;

    for (l = ui->suspended_controllers; l; l = l->next)
        rut_object_unref(l->data);
    c_list_free(ui->suspended_controllers);

    for (l = ui->controllers; l; l = l->next)
        rut_object_unref(l->data);
    c_list_free(ui->controllers);

    for (l = ui->assets; l; l = l->next)
        rut_object_unref(l->data);
    c_list_free(ui->assets);

    /* NB: no extra reference is held on ui->light other than the
     * reference for it being in the ->scene. */

    if (ui->scene)
        rut_object_unref(ui->scene);

    if (ui->play_camera)
        rut_object_unref(ui->play_camera);

    if (ui->play_camera_component)
        rut_object_unref(ui->play_camera_component);

    if (ui->dso_data)
        c_free(ui->dso_data);

    rut_object_free(rig_ui_t, object);
}

static rut_traverse_visit_flags_t
reap_entity_cb(rut_object_t *object, int depth, void *user_data)
{
    rig_engine_t *engine = user_data;

    /* The root node is a rut_graph_t that shouldn't be reaped */
    if (rut_object_get_type(object) != &rig_entity_type)
        return RUT_TRAVERSE_VISIT_CONTINUE;

    rig_entity_reap(object, engine);

    rut_graphable_remove_child(object);

    return RUT_TRAVERSE_VISIT_CONTINUE;
}

void
rig_ui_reap(rig_ui_t *ui)
{
    rig_engine_t *engine = ui->engine;
    c_list_t *l;

    rut_graphable_traverse(ui->scene,
                           RUT_TRAVERSE_DEPTH_FIRST,
                           reap_entity_cb,
                           NULL, /* post paint */
                           ui->engine);

    for (l = ui->controllers; l; l = l->next) {
        rig_controller_t *controller = l->data;
        rig_controller_reap(controller, engine);
        rut_object_release(controller, ui);
    }

    /* We could potentially leave these to be freed in _free() but it
     * seems a bit ugly to keep the list containing pointers to
     * controllers no longer owned by the ui. */
    c_list_free(ui->controllers);
    ui->controllers = NULL;

    for (l = ui->assets; l; l = l->next) {
        rig_asset_t *asset = l->data;
        rig_asset_reap(asset, engine);
        rut_object_release(asset, ui);
    }

    /* We could potentially leave these to be freed in _free() but it
     * seems a bit ugly to keep the list containing pointers to
     * assets no longer owned by the ui. */
    c_list_free(ui->assets);
    ui->assets = NULL;

    /* XXX: The ui itself is just a normal ref-counted object that
     * doesn't need to be unregistered so we don't call
     * rig_engine_queue_delete() for it */
}

rut_type_t rig_ui_type;

static void
_rig_ui_init_type(void)
{
    rut_type_init(&rig_ui_type, "rig_ui_t", _rig_ui_free);
}

rig_ui_t *
rig_ui_new(rig_engine_t *engine)
{
    rig_ui_t *ui = rut_object_alloc0(rig_ui_t, &rig_ui_type, _rig_ui_init_type);

    ui->engine = engine;

    return ui;
}

void
rig_ui_set_dso_data(rig_ui_t *ui, uint8_t *data, int len)
{
    if (ui->dso_data)
        c_free(ui->dso_data);

    ui->dso_data = c_malloc(len);
    memcpy(ui->dso_data, data, len);
    ui->dso_len = len;
}

typedef struct {
    const char *label;
    rig_entity_t *entity;
} find_entity_data_t;

static rut_traverse_visit_flags_t
find_entity_cb(rut_object_t *object, int depth, void *user_data)
{
    find_entity_data_t *data = user_data;

    if (rut_object_get_type(object) == &rig_entity_type &&
        !strcmp(data->label, rig_entity_get_label(object))) {
        data->entity = object;
        return RUT_TRAVERSE_VISIT_BREAK;
    }

    return RUT_TRAVERSE_VISIT_CONTINUE;
}

rig_entity_t *
rig_ui_find_entity(rig_ui_t *ui, const char *label)
{
    find_entity_data_t data = { .label = label, .entity = NULL };

    rut_graphable_traverse(ui->scene,
                           RUT_TRAVERSE_DEPTH_FIRST,
                           find_entity_cb,
                           NULL, /* after_children_cb */
                           &data);

    return data.entity;
}

static void
initialise_play_camera_position(rig_engine_t *engine, rig_ui_t *ui)
{
    float fov_y = 10; /* y-axis field of view */
    float aspect = (float)engine->device_width / (float)engine->device_height;
    float z_near = 10; /* distance to near clipping plane */
    float z_2d = 30;
    float position[3];
    float left, right, top;
    float left_2d_plane, right_2d_plane;
    float width_scale;
    float width_2d_start;

    /* Initialise the camera to the center of the device with a z
     * position that will give it pixel aligned coordinates at the
     * origin */
    top = z_near * tanf(fov_y * C_PI / 360.0);
    left = -top * aspect;
    right = top * aspect;

    left_2d_plane = left / z_near * z_2d;
    right_2d_plane = right / z_near * z_2d;

    width_2d_start = right_2d_plane - left_2d_plane;

    width_scale = width_2d_start / engine->device_width;

    position[0] = engine->device_width / 2.0f;
    position[1] = engine->device_height / 2.0f;
    position[2] = z_2d / width_scale;

    rig_entity_set_position(ui->play_camera, position);
}

static void
initialise_play_camera_component(rut_object_t *camera_component)
{
    rut_camera_set_projection_mode(camera_component, RUT_PROJECTION_PERSPECTIVE);
    rut_camera_set_field_of_view(camera_component, 10);
    rut_camera_set_near_plane(camera_component, 1000);
    rut_camera_set_far_plane(camera_component, 10000);
}

void
rig_ui_prepare(rig_ui_t *ui)
{
    rig_engine_t *engine = ui->engine;
    rig_controller_t *controller;
    rut_object_t *light_camera;
    c_list_t *l;

    if (!ui->scene)
        ui->scene = rut_graph_new(engine->shell);

    if (!ui->light) {
        rig_light_t *light;
        float vector3[3];
        cg_color_t color;

        ui->light = rig_entity_new(engine->shell);
        rig_entity_set_label(ui->light, "light");

        vector3[0] = 0;
        vector3[1] = 0;
        vector3[2] = 500;
        rig_entity_set_position(ui->light, vector3);

        rig_entity_rotate_x_axis(ui->light, 20);
        rig_entity_rotate_y_axis(ui->light, -20);

        light = rig_light_new(engine->shell);
        cg_color_init_from_4f(&color, .2f, .2f, .2f, 1.f);
        rig_light_set_ambient(light, &color);
        cg_color_init_from_4f(&color, .6f, .6f, .6f, 1.f);
        rig_light_set_diffuse(light, &color);
        cg_color_init_from_4f(&color, .4f, .4f, .4f, 1.f);
        rig_light_set_specular(light, &color);

        rig_entity_add_component(ui->light, light);

        rut_graphable_add_child(ui->scene, ui->light);
    }

    light_camera =
        rig_entity_get_component(ui->light, RUT_COMPONENT_TYPE_CAMERA);
    if (!light_camera) {
        light_camera = rig_camera_new(engine,
                                      1000, /* ortho/vp width */
                                      1000, /* ortho/vp height */
                                      NULL);

        rut_camera_set_background_color4f(light_camera, 0.f, .3f, 0.f, 1.f);
        rut_camera_set_projection_mode(light_camera,
                                       RUT_PROJECTION_ORTHOGRAPHIC);
        rut_camera_set_orthographic_coordinates(
            light_camera, -1000, -1000, 1000, 1000);
        rut_camera_set_near_plane(light_camera, 1.1f);
        rut_camera_set_far_plane(light_camera, 1500.f);

        rig_entity_add_component(ui->light, light_camera);
    }

    if (engine->renderer) {
        cg_framebuffer_t *fb;
        int width, height;

#warning "FIXME: rig-ui.c shouldn't make any assumptions about the renderer in use"
        c_warn_if_fail(rut_object_get_type(engine->renderer) == &rig_renderer_type);

        fb = rig_renderer_get_shadow_fb(engine->renderer);
        width = cg_framebuffer_get_width(fb);
        height = cg_framebuffer_get_height(fb);

        rut_camera_set_framebuffer(light_camera, fb);
        rut_camera_set_viewport(light_camera, 0, 0, width, height);
    }

    if (!ui->controllers) {
        controller = rig_controller_new(engine, "Controller 0");
        rig_controller_set_active(controller, true);
        ui->controllers = c_list_prepend(ui->controllers, controller);
    }

    /* Explcitly transfer ownership of controllers to the UI for
     * improved ref-count debugging.
     *
     * XXX: don't RIG_ENABLE_DEBUG guard this without also
     * updating rig_ui_reap()
     */
    for (l = ui->controllers; l; l = l->next) {
        rut_object_claim(l->data, ui);
        rut_object_unref(l->data);
    }

    if (!ui->play_camera) {
        /* Check if there is already an entity labelled ‘play-camera’
         * in the scene graph */
        ui->play_camera = rig_ui_find_entity(ui, "play-camera");

        if (ui->play_camera)
            rut_object_ref(ui->play_camera);
        else {
            ui->play_camera = rig_entity_new(engine->shell);
            rig_entity_set_label(ui->play_camera, "play-camera");

            initialise_play_camera_position(engine, ui);

            rut_graphable_add_child(ui->scene, ui->play_camera);
        }
    }

    if (!ui->play_camera_component) {
        ui->play_camera_component = rig_entity_get_component(
            ui->play_camera, RUT_COMPONENT_TYPE_CAMERA);

        if (ui->play_camera_component)
            rut_object_ref(ui->play_camera_component);
        else {
            ui->play_camera_component = rig_camera_new(engine,
                                                       -1, /* ortho/vp width */
                                                       -1, /* ortho/vp height */
                                                       engine->frontend->onscreen);

            initialise_play_camera_component(ui->play_camera_component);

            rig_entity_add_component(ui->play_camera,
                                     ui->play_camera_component);
        }
    }

    rut_camera_set_clear(ui->play_camera_component, false);

    rig_ui_suspend(ui);
}

void
rig_ui_suspend(rig_ui_t *ui)
{
    c_list_t *l;

    if (ui->suspended)
        return;

    for (l = ui->controllers; l; l = l->next) {
        rig_controller_t *controller = l->data;

        rig_controller_set_suspended(controller, true);

        ui->suspended_controllers =
            c_list_prepend(ui->suspended_controllers, controller);

        /* We take a reference on all suspended controllers so we
         * don't need to worry if any of the controllers are deleted
         * while in edit mode. */
        rut_object_ref(controller);
    }

    ui->suspended = true;
}

void
rig_ui_resume(rig_ui_t *ui)
{
    c_list_t *l;

    if (!ui->suspended)
        return;

    for (l = ui->suspended_controllers; l; l = l->next) {
        rig_controller_t *controller = l->data;

        rig_controller_set_suspended(controller, false);
        rut_object_unref(controller);
    }

    c_list_free(ui->suspended_controllers);
    ui->suspended_controllers = NULL;

    ui->suspended = false;
}

static bool
print_component_cb(rut_object_t *component, void *user_data)
{
    int depth = *(int *)user_data;
    char *name = rig_engine_get_object_debug_name(component);
    c_debug("%*s%s\n", depth + 2, " ", name);
    c_free(name);

    return true; /* continue */
}

static rut_traverse_visit_flags_t
print_entity_cb(rut_object_t *object, int depth, void *user_data)
{
    char *name = rig_engine_get_object_debug_name(object);
    c_debug("%*s%s\n", depth, " ", name);

    if (rut_object_get_type(object) == &rig_entity_type) {
        rig_entity_foreach_component_safe(object, print_component_cb, &depth);
    }

    c_free(name);

    return RUT_TRAVERSE_VISIT_CONTINUE;
}

void
rig_ui_print(rig_ui_t *ui)
{
    c_list_t *l;

    c_debug("Scenegraph:\n");
    rut_graphable_traverse(ui->scene,
                           RUT_TRAVERSE_DEPTH_FIRST,
                           print_entity_cb,
                           NULL, /* post paint */
                           NULL); /* user data */

    c_debug("Controllers:\n");
    for (l = ui->controllers; l; l = l->next) {
        char *name = rig_engine_get_object_debug_name(l->data);
        c_debug("  %s\n", name);
        c_free(name);
    }

    c_debug("Assets:\n");
    for (l = ui->assets; l; l = l->next) {
        char *name = rig_engine_get_object_debug_name(l->data);
        c_debug("  %s\n", name);
        c_free(name);
    }
}

void
rig_ui_add_controller(rig_ui_t *ui, rig_controller_t *controller)
{
    ui->controllers = c_list_prepend(ui->controllers, controller);
    rut_object_ref(controller);

    if (!ui->suspended)
        rig_controller_set_suspended(controller, false);
}

void
rig_ui_remove_controller(rig_ui_t *ui, rig_controller_t *controller)
{
    rig_controller_set_suspended(controller, true);

    ui->controllers = c_list_remove(ui->controllers, controller);
    rut_object_unref(controller);
}
