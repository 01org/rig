#include <cogl/cogl.h>
#include <cogl/cogl-gles2.h>
#include <glib.h>
#include <stdio.h>

#define OFFSCREEN_WIDTH 100
#define OFFSCREEN_HEIGHT 100

typedef struct _Data {
    cg_device_t *dev;
    cg_framebuffer_t *fb;
    cg_primitive_t *triangle;
    cg_pipeline_t *pipeline;

    cg_texture_t *offscreen_texture;
    cg_offscreen_t *offscreen;
    cg_gles2_context_t *gles2_ctx;
    const cg_gles2_vtable_t *gles2_vtable;
} Data;

static gboolean
paint_cb(void *user_data)
{
    Data *data = user_data;
    cg_error_t *error = NULL;
    const cg_gles2_vtable_t *gles2 = data->gles2_vtable;

    /* Draw scene with GLES2 */
    if (!cg_push_gles2_context(
            data->dev, data->gles2_ctx, data->fb, data->fb, &error)) {
        g_error("Failed to push gles2 context: %s\n", error->message);
    }

    /* Clear offscreen framebuffer with a random color */
    gles2->glClearColor(
        c_random_double(), c_random_double(), c_random_double(), 1.0f);
    gles2->glClear(GL_COLOR_BUFFER_BIT);

    cg_pop_gles2_context(data->dev);

    /* Draw scene with Cogl */
    cg_primitive_draw(data->triangle, data->fb, data->pipeline);

    cg_onscreen_swap_buffers(CG_ONSCREEN(data->fb));

    return FALSE; /* remove the callback */
}

static void
frame_event_cb(cg_onscreen_t *onscreen,
               cg_frame_event_t event,
               cg_frame_info_t *info,
               void *user_data)
{
    if (event == CG_FRAME_EVENT_SYNC)
        paint_cb(user_data);
}

int
main(int argc, char **argv)
{
    Data data;
    cg_onscreen_t *onscreen;
    cg_error_t *error = NULL;
    cg_vertex_p2c4_t triangle_vertices[] = {
        { 0, 0.7, 0xff, 0x00, 0x00, 0xff },
        { -0.7, -0.7, 0x00, 0xff, 0x00, 0xff },
        { 0.7, -0.7, 0x00, 0x00, 0xff, 0xff }
    };
    GSource *cg_source;
    GMainLoop *loop;
    cg_renderer_t *renderer;
    cg_display_t *display;

    renderer = cg_renderer_new();
    cg_renderer_add_constraint(renderer,
                               CG_RENDERER_CONSTRAINT_SUPPORTS_CG_GLES2);
    display = cg_display_new(renderer, NULL);
    data.dev = cg_device_new();
    cg_device_set_display(data.dev, display);

    onscreen = cg_onscreen_new(data.dev, 640, 480);
    cg_onscreen_show(onscreen);
    data.fb = onscreen;

    /* Prepare onscreen primitive */
    data.triangle = cg_primitive_new_p2c4(
        data.dev, CG_VERTICES_MODE_TRIANGLES, 3, triangle_vertices);
    data.pipeline = cg_pipeline_new(data.dev);

    data.offscreen_texture = cg_texture_2d_new_with_size(
        data.dev, OFFSCREEN_WIDTH, OFFSCREEN_HEIGHT);
    data.offscreen = cg_offscreen_new_with_texture(data.offscreen_texture);

    data.gles2_ctx = cg_gles2_context_new(data.dev, &error);
    if (!data.gles2_ctx) {
        g_error("Failed to create GLES2 context: %s\n", error->message);
    }

    data.gles2_vtable = cg_gles2_context_get_vtable(data.gles2_ctx);

    /* Draw scene with GLES2 */
    if (!cg_push_gles2_context(
            data.dev, data.gles2_ctx, data.fb, data.fb, &error)) {
        g_error("Failed to push gles2 context: %s\n", error->message);
    }

    cg_pop_gles2_context(data.dev);

    cg_source = cg_glib_source_new(data.dev, G_PRIORITY_DEFAULT);

    g_source_attach(cg_source, NULL);

    cg_onscreen_add_frame_callback(
        CG_ONSCREEN(data.fb), frame_event_cb, &data, NULL); /* destroy notify */

    g_idle_add(paint_cb, &data);

    loop = g_main_loop_new(NULL, TRUE);
    g_main_loop_run(loop);

    return 0;
}
