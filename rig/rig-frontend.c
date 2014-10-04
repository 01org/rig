/*
 * Rig
 *
 * UI Engine & Editor
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
 */

#include <config.h>

#include <stdlib.h>
#include <sys/socket.h>

#ifdef linux
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <rut.h>

#include "rig-engine.h"
#include "rig-frontend.h"
#include "rig-renderer.h"
#include "rig-pb.h"

#include "rig.pb-c.h"

static void spawn_simulator(rut_shell_t *shell, rig_frontend_t *frontend);

static void
frontend__test(Rig__Frontend_Service *service,
               const Rig__Query *query,
               Rig__TestResult_Closure closure,
               void *closure_data)
{
    Rig__TestResult result = RIG__TEST_RESULT__INIT;
    // rig_frontend_t *frontend = rig_pb_rpc_closure_get_connection_data
    // (closure_data);

    c_return_if_fail(query != NULL);

    // c_print ("Frontend Service: Test Query\n");

    closure(&result, closure_data);
}

static void
register_object_cb(void *object, uint64_t id, void *user_data)
{
    rig_frontend_t *frontend = user_data;

    /* If the ID is an odd number that implies it is a temporary ID that
     * we need to be able map... */
    if (id & 1) {
        void *id_ptr = (void *)(uintptr_t)id;
        c_hash_table_insert(frontend->tmp_id_to_object_map, id_ptr, object);
    }
}

static void *
lookup_object(rig_frontend_t *frontend, uint64_t id)
{
    /* If the ID is an odd number that implies it is a temporary ID that
     * needs mapping... */
    if (id & 1) {
        void *id_ptr = (void *)(uintptr_t)id;
        return c_hash_table_lookup(frontend->tmp_id_to_object_map, id_ptr);
    } else /* Otherwise we can assume the ID corresponds to an object pointer */
        return (void *)(intptr_t)id;
}

static void *
lookup_object_cb(uint64_t id, void *user_data)
{
    rig_frontend_t *frontend = user_data;
    return lookup_object(frontend, id);
}

static void
apply_property_change(rig_frontend_t *frontend,
                      rig_pb_un_serializer_t *unserializer,
                      Rig__PropertyChange *pb_change)
{
    void *object;
    rut_property_t *property;
    rut_boxed_t boxed;

    if (!pb_change->has_object_id || pb_change->object_id == 0 ||
        !pb_change->has_property_id || !pb_change->value) {
        c_warning("Frontend: Invalid property change received");
        return;
    }

    object = lookup_object(frontend, pb_change->object_id);

#if 0
    c_print ("Frontend: PropertyChange: %p(%s) prop_id=%d\n",
             object,
             rut_object_get_type_name (object),
             pb_change->property_id);
#endif

    property = rut_introspectable_get_property(object, pb_change->property_id);
    if (!property) {
        c_warning("Frontend: Failed to find object property by id");
        return;
    }

    /* XXX: ideally we shouldn't need to init a rut_boxed_t and set
     * that on a property, and instead we can just directly
     * apply the value to the property we have. */
    rig_pb_init_boxed_value(
        unserializer, &boxed, property->spec->type, pb_change->value);

    //#warning "XXX: frontend updates are disabled"
    rut_property_set_boxed(
        &frontend->engine->ctx->property_ctx, property, &boxed);
}

static void
unregister_id_cb(uint64_t id, void *user_data)
{
    rig_frontend_t *frontend = user_data;

    /* If the ID is an odd number that implies it is a temporary ID that
     * needs mapping... */
    if (id & 1) {
        void *id_ptr = (void *)(uintptr_t)id;

        /* Remove the mapping immediately */
        c_hash_table_remove(frontend->tmp_id_to_object_map, id_ptr);
    }
}

static void
frontend__update_ui(Rig__Frontend_Service *service,
                    const Rig__UIDiff *pb_ui_diff,
                    Rig__UpdateUIAck_Closure closure,
                    void *closure_data)
{
    Rig__UpdateUIAck ack = RIG__UPDATE_UIACK__INIT;
    rig_frontend_t *frontend =
        rig_pb_rpc_closure_get_connection_data(closure_data);
    rig_engine_t *engine = frontend->engine;
    int i, j;
    int n_property_changes;
    int n_actions;
    rig_pb_un_serializer_t *unserializer;
    rig_engine_op_map_context_t *map_op_ctx;
    rig_engine_op_apply_context_t *apply_op_ctx;
    Rig__UIEdit *pb_ui_edit;

#if 0
    frontend->ui_update_pending = false;

    closure (&ack, closure_data);

    /* XXX: The current use case we have forui update callbacks
     * requires that the frontend be in-sync with the simulator so
     * we invoke them after we have applied all the operations from
     * the simulator */
    rut_closure_list_invoke (&frontend->ui_update_cb_list,
                             rig_frontend_ui_update_callback_t,
                             frontend);
    return;
#endif
    // c_print ("Frontend: Update UI Request\n");

    frontend->ui_update_pending = false;

    c_return_if_fail(pb_ui_diff != NULL);

    n_property_changes = pb_ui_diff->n_property_changes;

    map_op_ctx = &frontend->map_op_ctx;
    apply_op_ctx = &frontend->apply_op_ctx;
    unserializer = frontend->prop_change_unserializer;

    pb_ui_edit = pb_ui_diff->edit;

    /* For compactness, property changes are serialized separately from
     * more general UI edit operations and so we need to take care that
     * we apply property changes and edit operations in the correct
     * order, using the operation sequences to relate to the sequence
     * of property changes.
     */
    j = 0;
    if (pb_ui_edit) {
        for (i = 0; i < pb_ui_edit->n_ops; i++) {
            Rig__Operation *pb_op = pb_ui_edit->ops[i];
            int until = pb_op->sequence;

            for (; j < until; j++) {
                Rig__PropertyChange *pb_change =
                    pb_ui_diff->property_changes[j];
                apply_property_change(frontend, unserializer, pb_change);
            }

            if (!rig_engine_pb_op_map(map_op_ctx, apply_op_ctx, pb_op)) {
                c_warning("Frontend: Failed to ID map simulator operation");
                continue;
            }
        }
    }

    for (; j < n_property_changes; j++) {
        Rig__PropertyChange *pb_change = pb_ui_diff->property_changes[j];
        apply_property_change(frontend, unserializer, pb_change);
    }

    n_actions = pb_ui_diff->n_actions;
    for (i = 0; i < n_actions; i++) {
        Rig__SimulatorAction *pb_action = pb_ui_diff->actions[i];
        switch (pb_action->type) {
#if 0
        case RIG_SIMULATOR_ACTION_TYPE_SET_PLAY_MODE:
            rig_camera_view_set_play_mode_enabled (engine->main_camera_view,
                                                   pb_action->set_play_mode->enabled);
            break;
#endif
        }
    }

    if (pb_ui_diff->has_queue_frame)
        rut_shell_queue_redraw(engine->shell);

    closure(&ack, closure_data);

    /* XXX: The current use case we have forui update callbacks
     * requires that the frontend be in-sync with the simulator so
     * we invoke them after we have applied all the operations from
     * the simulator */
    rut_closure_list_invoke(&frontend->ui_update_cb_list,
                            rig_frontend_ui_update_callback_t,
                            frontend);
}

static Rig__Frontend_Service rig_frontend_service =
    RIG__FRONTEND__INIT(frontend__);

#if 0
static void
handle_simulator_test_response (const Rig__TestResult *result,
                                void *closure_data)
{
    c_print ("Simulator test response received\n");
}
#endif

typedef struct _load_state_t {
    c_list_t *required_assets;
} load_state_t;

bool
asset_filter_cb(rig_asset_t *asset, void *user_data)
{
    bool *play_mode = user_data;

    /* When serializing a play mode ui we assume all assets are shared
     * with an edit mode ui and so we don't need to serialize any
     * assets... */
    if (*play_mode)
        return false;

    switch (rig_asset_get_type(asset)) {
    case RIG_ASSET_TYPE_BUILTIN:
    case RIG_ASSET_TYPE_TEXTURE:
    case RIG_ASSET_TYPE_NORMAL_MAP:
    case RIG_ASSET_TYPE_ALPHA_MASK:
    case RIG_ASSET_TYPE_FONT:
        return false; /* these assets aren't needed in the simulator */
    case RIG_ASSET_TYPE_MESH:
        return true; /* keep mesh assets for picking */
        // c_print ("Serialization requires asset %s\n",
        //         rig_asset_get_path (asset));
        break;
    }

    c_warn_if_reached();
    return false;
}

static void
handle_load_response(const Rig__LoadResult *result,
                     void *closure_data)
{
    // c_print ("Frontend: UI loaded response received from simulator\n");
}

void
rig_frontend_forward_simulator_ui(rig_frontend_t *frontend,
                                  const Rig__UI *pb_ui,
                                  bool play_mode)
{
    ProtobufCService *simulator_service;

    if (!frontend->connected)
        return;

    simulator_service =
        rig_pb_rpc_client_get_service(frontend->frontend_peer->pb_rpc_client);

    rig__simulator__load(simulator_service, pb_ui, handle_load_response, NULL);
}

void
rig_frontend_reload_simulator_ui(rig_frontend_t *frontend,
                                 rig_ui_t *ui,
                                 bool play_mode)
{
    rig_engine_t *engine;
    rig_pb_serializer_t *serializer;
    Rig__UI *pb_ui;

    if (!frontend->connected)
        return;

    engine = frontend->engine;
    serializer = rig_pb_serializer_new(engine);

    rig_pb_serializer_set_use_pointer_ids_enabled(serializer, true);

    rig_pb_serializer_set_asset_filter(serializer, asset_filter_cb, &play_mode);

    pb_ui = rig_pb_serialize_ui(serializer, play_mode, ui);

    rig_frontend_forward_simulator_ui(frontend, pb_ui, play_mode);

    rig_pb_serialized_ui_destroy(pb_ui);

    rig_pb_serializer_destroy(serializer);

    rig_engine_op_apply_context_set_ui(&frontend->apply_op_ctx, ui);
}

static void
frontend_peer_connected(pb_rpc__client_t *pb_client,
                        void *user_data)
{
    rig_frontend_t *frontend = user_data;

    frontend->connected = true;

    if (frontend->simulator_connected_callback) {
        void *user_data = frontend->simulator_connected_data;
        frontend->simulator_connected_callback(user_data);
    }

#if 0
    Rig__Query query = RIG__QUERY__INIT;


    rig__simulator__test (simulator_service, &query,
                          handle_simulator_test_response, NULL);
#endif

    // c_print ("Frontend peer connected\n");
}

static void
frontend_stop_service(rig_frontend_t *frontend)
{
    rut_object_unref(frontend->frontend_peer);
    frontend->frontend_peer = NULL;
    frontend->connected = false;
    frontend->ui_update_pending = false;
}

static void
frontend_peer_error_handler(PB_RPC_Error_Code code,
                            const char *message,
                            void *user_data)
{
    rig_frontend_t *frontend = user_data;

    c_warning("Frontend peer error: %s", message);

    frontend_stop_service(frontend);
}

static void
frontend_start_service(rut_shell_t *shell, rig_frontend_t *frontend)
{
    frontend->frontend_peer = rig_rpc_peer_new(
        shell,
        frontend->fd,
        &rig_frontend_service.base,
        (ProtobufCServiceDescriptor *)&rig__simulator__descriptor,
        frontend_peer_error_handler,
        frontend_peer_connected,
        frontend);
}

void
rig_frontend_set_simulator_connected_callback(rig_frontend_t *frontend,
                                              void (*callback)(void *user_data),
                                              void *user_data)
{
    frontend->simulator_connected_callback = callback;
    frontend->simulator_connected_data = user_data;
}

/* TODO: should support a destroy_notify callback */
void
rig_frontend_sync(rig_frontend_t *frontend,
                  void (*synchronized)(const Rig__SyncAck *result,
                                       void *user_data),
                  void *user_data)
{
    ProtobufCService *simulator_service;
    Rig__Sync sync = RIG__SYNC__INIT;

    if (!frontend->connected)
        return;

    simulator_service =
        rig_pb_rpc_client_get_service(frontend->frontend_peer->pb_rpc_client);

    rig__simulator__synchronize(
        simulator_service, &sync, synchronized, user_data);
}

static void
frame_running_ack(const Rig__RunFrameAck *ack, void *closure_data)
{
    rig_frontend_t *frontend = closure_data;
    rig_engine_t *engine = frontend->engine;

    /* At this point we know that the simulator has now switched modes
     * and so we can finish the switch in the frontend...
     */
    if (frontend->pending_play_mode_enabled != engine->play_mode) {
        rig_engine_set_play_mode_enabled(engine,
                                         frontend->pending_play_mode_enabled);
    }

    // c_print ("Frontend: Run Frame ACK received from simulator\n");
}

typedef struct _registration_state_t {
    int index;
    Rig__ObjectRegistration *pb_registrations;
    Rig__ObjectRegistration **object_registrations;
} registration_state_t;

static bool
register_temporary_cb(void *key, void *value, void *user_data)
{
    registration_state_t *state = user_data;
    Rig__ObjectRegistration *pb_registration =
        &state->pb_registrations[state->index];
    uint64_t id = (uint64_t)(uintptr_t)key;
    void *object = value;

    rig__object_registration__init(pb_registration);
    pb_registration->temp_id = id;
    pb_registration->real_id = (uint64_t)(uintptr_t)object;

    state->object_registrations[state->index++] = pb_registration;

    return true;
}

void
rig_frontend_run_simulator_frame(rig_frontend_t *frontend,
                                 rig_pb_serializer_t *serializer,
                                 Rig__FrameSetup *setup)
{
    rig_engine_t *engine = frontend->engine;
    ProtobufCService *simulator_service;
    int n_temps;

    if (!frontend->connected)
        return;

    simulator_service =
        rig_pb_rpc_client_get_service(frontend->frontend_peer->pb_rpc_client);

    /* When UI logic in the simulator creates objects, they are
     * initially given a temporary ID until the corresponding object
     * has been created in the frontend. Before running the
     * next simulator frame we send it back the real IDs that have been
     * registered to replace those temporary IDs...
     */
    n_temps = c_hash_table_size(frontend->tmp_id_to_object_map);
    if (n_temps) {
        registration_state_t state;

        setup->n_object_registrations = n_temps;
        setup->object_registrations =
            rut_memory_stack_memalign(engine->frame_stack,
                                      sizeof(void *) * n_temps,
                                      RUT_UTIL_ALIGNOF(void *));

        state.index = 0;
        state.object_registrations = setup->object_registrations;
        state.pb_registrations = rut_memory_stack_memalign(
            engine->frame_stack,
            sizeof(Rig__ObjectRegistration) * n_temps,
            RUT_UTIL_ALIGNOF(Rig__ObjectRegistration));

        c_hash_table_foreach_remove(
            frontend->tmp_id_to_object_map, register_temporary_cb, &state);
    }

    if (frontend->pending_dso_data) {
        setup->has_dso = true;
        setup->dso.len = frontend->pending_dso_len;
        setup->dso.data = rut_memory_stack_memalign(engine->frame_stack,
                                                    frontend->pending_dso_len,
                                                    RUT_UTIL_ALIGNOF(uint8_t));
        memcpy(setup->dso.data, frontend->pending_dso_data, setup->dso.len);
    }

    rig__simulator__run_frame(
        simulator_service, setup, frame_running_ack, frontend); /* user data */

    frontend->ui_update_pending = true;

    if (frontend->pending_dso_data) {
        c_free(frontend->pending_dso_data);
        frontend->pending_dso_data = NULL;
    }
}

static void
_rig_frontend_free(void *object)
{
    rig_frontend_t *frontend = object;

    if (frontend->pending_dso_data)
        c_free(frontend->pending_dso_data);

    rig_engine_op_apply_context_destroy(&frontend->apply_op_ctx);
    rig_engine_op_map_context_destroy(&frontend->map_op_ctx);
    rig_pb_unserializer_destroy(frontend->prop_change_unserializer);

    rut_closure_list_disconnect_all(&frontend->ui_update_cb_list);

    frontend_stop_service(frontend);

    rut_object_unref(frontend->engine);

    c_hash_table_destroy(frontend->tmp_id_to_object_map);

    rut_object_free(rig_frontend_t, object);
}

rut_type_t rig_frontend_type;

static void
_rig_frontend_init_type(void)
{
    rut_type_init(&rig_frontend_type, "rig_frontend_t", _rig_frontend_free);
}

#ifdef RIG_EDITOR_ENABLED

#if !defined(__ANDROID__) && (defined(linux) || defined(__APPLE__))
static void
simulator_sigchild_cb(GPid pid, int status, void *user_data)
{
    rig_frontend_t *frontend = user_data;
    rig_engine_t *engine = frontend->engine;

    frontend_stop_service(frontend);

    c_print("SIGCHLD received: Simulator Gone!");

    if (frontend->id == RIG_FRONTEND_ID_EDITOR) {
        if (engine->play_mode) {
            rig_engine_set_play_mode_enabled(engine, false);
            frontend->pending_play_mode_enabled = false;
        }
        spawn_simulator(engine->shell, frontend);
    }
}

static void
fork_simulator(rut_shell_t *shell, rig_frontend_t *frontend)
{
    pid_t pid;
    int sp[2];

    c_return_if_fail(frontend->connected == false);

    /*
     * Spawn a simulator process...
     */

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0)
        c_error("Failed to open simulator ipc");

    pid = fork();
    if (pid == 0) {
        char fd_str[10];
        char *path = RIG_BIN_DIR "rig-simulator";

        /* child - simulator process */
        close(sp[0]);

        if (snprintf(fd_str, sizeof(fd_str), "%d", sp[1]) >= sizeof(fd_str))
            c_error("Failed to setup environment for simulator process");

        setenv("_RIG_IPC_FD", fd_str, true);

        switch (frontend->id) {
        case RIG_FRONTEND_ID_EDITOR:
            setenv("_RIG_FRONTEND", "editor", true);
            break;
        case RIG_FRONTEND_ID_SLAVE:
            setenv("_RIG_FRONTEND", "slave", true);
            break;
        case RIG_FRONTEND_ID_DEVICE:
            setenv("_RIG_FRONTEND", "device", true);
            break;
        }

        if (getenv("RIG_SIMULATOR"))
            path = getenv("RIG_SIMULATOR");

#ifdef RIG_ENABLE_DEBUG
        if (execlp("libtool", "libtool", "e", path, NULL) < 0)
            c_error("Failed to run simulator process via libtool");

#if 0
        if (execlp ("libtool", "libtool", "e", "valgrind", path, NULL))
            g_error ("Failed to run simulator process under valgrind");
#endif

#else
        if (execl(path, path, NULL) < 0)
            c_error("Failed to run simulator process");
#endif

        return;
    }

    frontend->simulator_pid = pid;
    frontend->fd = sp[0];

    if (frontend->id == RIG_FRONTEND_ID_EDITOR)
        g_child_watch_add(pid, simulator_sigchild_cb, frontend);

    frontend_start_service(shell, frontend);
}
#endif /* !defined (__ANDROID__) && (defined (linux) || defined (__APPLE__)) */

#endif /* RIG_EDITOR_ENABLED */

typedef struct _thread_setup_t {
    rig_frontend_t *frontend;
    int fd;
} thread_setup_t;

static int
run_simulator(void *user_data)
{
    thread_setup_t *setup = user_data;
    rig_frontend_t *frontend = setup->frontend;
    int fd = setup->fd;
    rig_simulator_t *simulator;

#ifdef USE_GLIB
    g_main_context_push_thread_default(g_main_context_new());
#endif

    simulator = rig_simulator_new(frontend->id, fd);

    rig_simulator_run(simulator);

    rut_object_unref(simulator);

    return 0;
}

static void
create_simulator_thread(rut_shell_t *shell,
                        rig_frontend_t *frontend)
{
    thread_setup_t setup;
    int sp[2];

    c_return_if_fail(frontend->connected == false);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0)
        c_error("Failed to open simulator ipc socketpair");

    setup.frontend = frontend;
    setup.fd = sp[1];

    SDL_CreateThread(run_simulator, "Simulator", &setup);

    frontend->fd = sp[0];

    frontend_start_service(shell, frontend);
}

#ifdef linux
static void
handle_simulator_connect_cb(void *user_data, int fd, int revents)
{
    rig_frontend_t *frontend = user_data;
    struct sockaddr addr;
    socklen_t addr_len = sizeof(addr);

    c_return_if_fail(revents & RUT_POLL_FD_EVENT_IN);

    c_message("Simulator connect request received!");

    frontend->fd = accept(frontend->listen_fd, &addr, &addr_len);
    if (frontend->fd != -1) {
        c_message("Simulator connected!");
        frontend_start_service(frontend->engine->shell, frontend);
    } else
        c_message("Failed to accept simulator connection: %s!",
                  strerror(errno));
}

static bool
bind_to_abstract_socket(rut_shell_t *shell,
                        rig_frontend_t *frontend)
{
    rut_exception_t *catch = NULL;
    int fd = rut_os_listen_on_abstract_socket("rig-simulator", &catch);

    if (fd < 0) {
        c_critical("Failed to listen on abstract \"rig-simulator\" socket: %s",
                   catch->message);
        return false;
    }

    frontend->listen_fd = fd;

    rut_poll_shell_add_fd(shell,
                          frontend->listen_fd,
                          RUT_POLL_FD_EVENT_IN,
                          NULL, /* prepare */
                          handle_simulator_connect_cb, /* dispatch */
                          frontend);

    c_message("Waiting for simulator to connect...");

    return true;
}
#endif /* linux */

#ifdef RIG_EDITOR_ENABLED

static void
spawn_simulator(rut_shell_t *shell, rig_frontend_t *frontend)
{
#if defined(linux)
    if (getenv("_RIG_USE_ABSTRACT_SOCKET"))
        bind_to_abstract_socket(shell,
                                frontend /* FIXME: give application name */);
    else if (getenv("_RIG_USE_SIMULATOR_THREAD"))
        create_simulator_thread(shell, frontend);
    else
        fork_simulator(shell, frontend);
#elif defined(__APPLE__)
    if (getenv("_RIG_USE_SIMULATOR_THREAD"))
        create_simulator_thread(shell, frontend);
    else
        fork_simulator(shell, frontend);
#else
#error "Platform needs some way of connecting to a simulator"
#endif
}

#else /* RIG_EDITOR_ENABLED */

static void
spawn_simulator(rut_shell_t *shell, rig_frontend_t *frontend)
{
    create_simulator_thread(shell, frontend);
}

#endif /* RIG_EDITOR_ENABLED */

static uint64_t
map_id_cb(uint64_t simulator_id, void *user_data)
{
    return (uint64_t)(uintptr_t)lookup_object(user_data, simulator_id);
}

static void
on_onscreen_resize(cg_onscreen_t *onscreen,
                   int width,
                   int height,
                   void *user_data)
{
    rig_engine_t *engine = user_data;

    c_return_if_fail(engine->simulator == NULL);

    rig_engine_resize(engine, width, height);
}

/* TODO: move this state into rig_frontend_t */
void
rig_frontend_post_init_engine(rig_frontend_t *frontend,
                              const char *ui_filename)
{
    rig_engine_t *engine = frontend->engine;
    cg_framebuffer_t *fb;

    engine->default_pipeline = cg_pipeline_new(engine->ctx->cg_device);

    engine->circle_node_attribute =
        rut_create_circle_fan_p2(engine->ctx, 20, &engine->circle_node_n_verts);

    _rig_init_image_source_wrappers_cache(engine);

    engine->renderer = rig_renderer_new(engine);
    rig_renderer_init(engine);

#ifndef __ANDROID__
    if (ui_filename) {
        struct stat st;

        stat(ui_filename, &st);
        if (S_ISREG(st.st_mode))
            rig_engine_load_file(engine, ui_filename);
        else
            rig_engine_load_empty_ui(engine);
    }
#endif

#ifdef RIG_EDITOR_ENABLED
    if (engine->frontend_id == RIG_FRONTEND_ID_EDITOR) {
        engine->onscreen = cg_onscreen_new(engine->ctx->cg_device, 1000, 700);
        cg_onscreen_set_resizable(engine->onscreen, true);
    } else
#endif
    engine->onscreen = cg_onscreen_new(engine->ctx->cg_device,
                                       engine->device_width / 2,
                                       engine->device_height / 2);

    cg_onscreen_add_resize_callback(
        engine->onscreen, on_onscreen_resize, engine, NULL);

    cg_framebuffer_allocate(engine->onscreen, NULL);

    fb = engine->onscreen;
    engine->window_width = cg_framebuffer_get_width(fb);
    engine->window_height = cg_framebuffer_get_height(fb);

    /* FIXME: avoid poking into engine->frontend here... */
    engine->frontend->has_resized = true;
    engine->frontend->pending_width = engine->window_width;
    engine->frontend->pending_height = engine->window_height;

    rut_shell_add_onscreen(engine->shell, engine->onscreen);

#ifdef USE_GTK
    {
        rig_application_t *application = rig_application_new(engine);

        gtk_init(NULL, NULL);

        /* We need to register the application before showing the onscreen
         * because we need to set the dbus paths before the window is
         * mapped. FIXME: Eventually it might be nice to delay creating
         * the windows until the ‘activate’ or ‘open’ signal is emitted so
         * that we can support the single process properly. In that case
         * we could let g_application_run handle the registration
         * itself */
        if (!g_application_register(G_APPLICATION(application),
                                    NULL, /* cancellable */
                                    NULL /* error */))
            /* Another instance of the application is already running */
            rut_shell_quit(shell);

        rig_application_add_onscreen(application, engine->onscreen);
    }
#endif

#ifdef __APPLE__
    rig_osx_init(engine);
#endif

    rut_shell_set_title(
        engine->shell, engine->onscreen, "Rig " C_STRINGIFY(RIG_VERSION));

    cg_onscreen_show(engine->onscreen);

    rig_engine_allocate(engine);
}

rig_frontend_t *
rig_frontend_new(rut_shell_t *shell, rig_frontend_id_t id, bool play_mode)
{
    rig_frontend_t *frontend = rut_object_alloc0(
        rig_frontend_t, &rig_frontend_type, _rig_frontend_init_type);
    rig_pb_un_serializer_t *unserializer;

    frontend->id = id;

    frontend->pending_play_mode_enabled = play_mode;

    frontend->tmp_id_to_object_map = c_hash_table_new(NULL, NULL);

    rut_list_init(&frontend->ui_update_cb_list);

    spawn_simulator(shell, frontend);

    frontend->engine = rig_engine_new_for_frontend(shell, frontend);

    rig_engine_op_map_context_init(
        &frontend->map_op_ctx, frontend->engine, map_id_cb, frontend);

    rig_engine_op_apply_context_init(&frontend->apply_op_ctx,
                                     frontend->engine,
                                     register_object_cb,
                                     unregister_id_cb,
                                     frontend);

    unserializer = rig_pb_unserializer_new(frontend->engine);
    /* Just to make sure we don't mistakenly use this unserializer to
     * register any objects... */
    rig_pb_unserializer_set_object_register_callback(unserializer, NULL, NULL);
    rig_pb_unserializer_set_id_to_object_callback(
        unserializer, lookup_object_cb, frontend);
    frontend->prop_change_unserializer = unserializer;

    return frontend;
}

rut_closure_t *
rig_frontend_add_ui_update_callback(rig_frontend_t *frontend,
                                    rig_frontend_ui_update_callback_t callback,
                                    void *user_data,
                                    rut_closure_destroy_callback_t destroy)
{
    return rut_closure_list_add(
        &frontend->ui_update_cb_list, callback, user_data, destroy);
}

static void
queue_simulator_frame_cb(rig_frontend_t *frontend, void *user_data)
{
    rut_shell_queue_redraw(frontend->engine->shell);
}

/* Similar to rut_shell_queue_redraw() but for queuing a new simulator
 * frame. If the simulator is currently busy this waits until we
 * recieve an update from the simulator and then queues a redraw. */
void
rig_frontend_queue_simulator_frame(rig_frontend_t *frontend)
{
    if (!frontend->ui_update_pending)
        rut_shell_queue_redraw(frontend->engine->shell);
    else if (!frontend->simulator_queue_frame_closure) {
        frontend->simulator_queue_frame_closure =
            rig_frontend_add_ui_update_callback(frontend,
                                                queue_simulator_frame_cb,
                                                frontend,
                                                NULL); /* destroy */
    }
}

void
rig_frontend_queue_set_play_mode_enabled(rig_frontend_t *frontend,
                                         bool play_mode_enabled)
{
    if (frontend->pending_play_mode_enabled == play_mode_enabled)
        return;

    frontend->pending_play_mode_enabled = play_mode_enabled;

    rig_frontend_queue_simulator_frame(frontend);
}

void
rig_frontend_update_simulator_dso(rig_frontend_t *frontend,
                                  uint8_t *dso,
                                  int len)
{
    if (frontend->pending_dso_data)
        c_free(frontend->pending_dso_data);

    frontend->pending_dso_data = dso;
    frontend->pending_dso_len = len;
}
