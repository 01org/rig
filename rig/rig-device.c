/*
 * Rig
 *
 * UI Engine & Editor
 *
 * Copyright (C) 2013,2014  Intel Corporation.
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

#include <stdlib.h>
#include <getopt.h>

#ifdef USE_GSTREAMER
#include <cogl-gst/cogl-gst.h>
#endif

#include <clib.h>
#include <rut.h>

#include "rig-frontend.h"
#include "rig-engine.h"
#include "rig-pb.h"
#include "rig-curses-debug.h"
#include "rig-logs.h"

#include "rig.pb-c.h"

typedef struct _rig_device_t {
    rut_object_base_t _base;

    rut_shell_t *shell;
    rig_frontend_t *frontend;
    rig_engine_t *engine;

    char *ui_filename;

} rig_device_t;

static void
rig_device_redraw(rut_shell_t *shell, void *user_data)
{
    rig_device_t *device = user_data;
    rig_engine_t *engine = device->engine;
    rig_frontend_t *frontend = engine->frontend;

    rut_shell_start_redraw(shell);

    /* XXX: we only kick off a new frame in the simulator if it's not
     * still busy... */
    if (!frontend->ui_update_pending) {
        rut_input_queue_t *input_queue = rut_shell_get_input_queue(shell);
        Rig__FrameSetup setup = RIG__FRAME_SETUP__INIT;
        rig_pb_serializer_t *serializer;

        serializer = rig_pb_serializer_new(engine);

        setup.has_play_mode = true;
        setup.play_mode = engine->play_mode;

        setup.n_events = input_queue->n_events;
        setup.events = rig_pb_serialize_input_events(serializer, input_queue);

        if (frontend->has_resized) {
            setup.has_view_width = true;
            setup.view_width = engine->window_width;
            setup.has_view_height = true;
            setup.view_height = engine->window_height;
            frontend->has_resized = false;
        }

        setup.edit = NULL;

        rig_frontend_run_simulator_frame(frontend, serializer, &setup);

        rig_pb_serializer_destroy(serializer);

        rut_input_queue_clear(input_queue);

        rut_memory_stack_rewind(engine->sim_frame_stack);
    }

    rut_shell_update_timelines(shell);

    rut_shell_run_pre_paint_callbacks(shell);

    rut_shell_run_start_paint_callbacks(shell);

    rig_engine_paint(engine);

    rig_engine_garbage_collect(engine,
                               NULL, /* callback */
                               NULL); /* user_data */

    rut_shell_run_post_paint_callbacks(shell);

    rut_memory_stack_rewind(engine->frame_stack);

    rut_shell_end_redraw(shell);

    /* FIXME: we should hook into an asynchronous notification of
     * when rendering has finished for determining when a frame is
     * finished. */
    rut_shell_finish_frame(shell);

    if (rut_shell_check_timelines(shell))
        rut_shell_queue_redraw(shell);
}

static void
simulator_connected_cb(void *user_data)
{
    rig_device_t *device = user_data;
    rig_engine_t *engine = device->engine;

    rig_frontend_reload_simulator_ui(
        device->frontend, engine->play_mode_ui, true); /* play mode ui */
}

static void
_rig_device_free(void *object)
{
    rig_device_t *device = object;

    rut_object_unref(device->engine);

    rut_object_unref(device->shell);
    rut_object_unref(device->shell);

    rut_object_free(rig_device_t, device);
}

static rut_type_t rig_device_type;

static void
_rig_device_init_type(void)
{
    rut_type_init(&rig_device_type, "rig_device_t", _rig_device_free);
}

static void
rig_device_init(rut_shell_t *shell, void *user_data)
{
    rig_device_t *device = user_data;
    rig_engine_t *engine;

    device->frontend = rig_frontend_new(
        device->shell, RIG_FRONTEND_ID_DEVICE, true /* start in play mode */);

    engine = device->frontend->engine;
    device->engine = engine;

    rig_logs_set_frontend(device->frontend);

    /* Finish the slave specific engine setup...
     */
    engine->main_camera_view = rig_camera_view_new(engine);
    rut_stack_add(engine->top_stack, engine->main_camera_view);

    /* Initialize the current mode */
    rig_engine_set_play_mode_enabled(engine, true /* start in play mode */);

    rig_frontend_post_init_engine(engine->frontend, device->ui_filename);

    rig_frontend_set_simulator_connected_callback(
        device->frontend, simulator_connected_cb, device);

    rut_shell_add_input_callback(
        device->shell, rig_engine_input_handler, device->engine, NULL);
}

static rig_device_t *
rig_device_new(const char *filename)
{
    rig_device_t *device = rut_object_alloc0(
        rig_device_t, &rig_device_type, _rig_device_init_type);
    char *assets_location;

    device->ui_filename = c_strdup(filename);

    device->shell = rut_shell_new(rig_device_redraw,
                                  device);

    rig_curses_add_to_shell(device->shell);

    rut_shell_set_on_run_callback(device->shell,
                                  rig_device_init,
                                  device);

    assets_location = c_path_get_dirname(device->ui_filename);
    rut_shell_set_assets_location(device->shell, assets_location);
    c_free(assets_location);

    return device;
}

static void
usage(void)
{
    fprintf(stderr, "Usage: rig-device [UI.rig]\n");
    fprintf(stderr, "  -h,--help    Display this help message\n");
    exit(1);
}

int
main(int argc, char **argv)
{
    rig_device_t *device;
    struct option opts[] = {
        { "help",    no_argument,       NULL, 'h' },
        { 0,         0,                 NULL,  0  }
    };
    int c;

    rig_curses_init();
    rut_init_tls_state();

#ifdef USE_GSTREAMER
    gst_init(&argc, &argv);
#endif

    while ((c = getopt_long(argc, argv, "h", opts, NULL)) != -1) {
        /* only one option a.t.m... */
        usage();
    }

    if (optind > argc || !argv[optind]) {
        fprintf(stderr, "Needs a UI.rig filename\n\n");
        usage();
    }

    device = rig_device_new(argv[optind]);

    rut_shell_main(device->shell);

    rut_object_unref(device);

    return 0;
}
