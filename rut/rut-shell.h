/*
 * Rut
 *
 * Rig Utilities
 *
 * Copyright (C) 2012, 2013 Intel Corporation.
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

#ifndef _RUT_SHELL_H_
#define _RUT_SHELL_H_

#include <stdbool.h>

#if defined(USE_SDL)
#include "rut-sdl-keysyms.h"
#include "SDL_syswm.h"
#endif

#ifdef USE_UV
#include <uv.h>
#endif

#include <cogl/cogl.h>

#include "rut-keysyms.h"
#include "rut-object.h"
#include "rut-types.h"
#include "rut-closure.h"
#include "rut-poll.h"

typedef void (*rut_shell_init_callback_t)(rut_shell_t *shell, void *user_data);
typedef void (*rut_shell_fini_callback_t)(rut_shell_t *shell, void *user_data);
typedef void (*rut_shell_paint_callback_t)(rut_shell_t *shell, void *user_data);

typedef enum _rut_input_event_type_t {
    RUT_INPUT_EVENT_TYPE_MOTION = 1,
    RUT_INPUT_EVENT_TYPE_KEY,
    RUT_INPUT_EVENT_TYPE_TEXT,
    RUT_INPUT_EVENT_TYPE_DROP_OFFER,
    RUT_INPUT_EVENT_TYPE_DROP_CANCEL,
    RUT_INPUT_EVENT_TYPE_DROP
} rut_input_event_type_t;

typedef enum _rut_key_event_action_t {
    RUT_KEY_EVENT_ACTION_UP = 1,
    RUT_KEY_EVENT_ACTION_DOWN
} rut_key_event_action_t;

typedef enum _rut_motion_event_action_t {
    RUT_MOTION_EVENT_ACTION_UP = 1,
    RUT_MOTION_EVENT_ACTION_DOWN,
    RUT_MOTION_EVENT_ACTION_MOVE,
} rut_motion_event_action_t;

typedef enum _rut_button_state_t {
    RUT_BUTTON_STATE_1 = 1 << 0,
    RUT_BUTTON_STATE_2 = 1 << 1,
    RUT_BUTTON_STATE_3 = 1 << 2,
} rut_button_state_t;

typedef enum _rut_modifier_state_t {
    RUT_MODIFIER_LEFT_ALT_ON = 1 << 0,
    RUT_MODIFIER_RIGHT_ALT_ON = 1 << 1,
    RUT_MODIFIER_LEFT_SHIFT_ON = 1 << 2,
    RUT_MODIFIER_RIGHT_SHIFT_ON = 1 << 3,
    RUT_MODIFIER_LEFT_CTRL_ON = 1 << 4,
    RUT_MODIFIER_RIGHT_CTRL_ON = 1 << 5,
    RUT_MODIFIER_LEFT_META_ON = 1 << 6,
    RUT_MODIFIER_RIGHT_META_ON = 1 << 7,
    RUT_MODIFIER_NUM_LOCK_ON = 1 << 8,
    RUT_MODIFIER_CAPS_LOCK_ON = 1 << 9
} rut_modifier_state_t;

typedef enum {
    RUT_CURSOR_ARROW,
    RUT_CURSOR_IBEAM,
    RUT_CURSOR_WAIT,
    RUT_CURSOR_CROSSHAIR,
    RUT_CURSOR_SIZE_WE,
    RUT_CURSOR_SIZE_NS
} rut_cursor_t;

#define RUT_MODIFIER_ALT_ON                                                    \
    (RUT_MODIFIER_LEFT_ALT_ON | RUT_MODIFIER_RIGHT_ALT_ON)
#define RUT_MODIFIER_SHIFT_ON                                                  \
    (RUT_MODIFIER_LEFT_SHIFT_ON | RUT_MODIFIER_RIGHT_SHIFT_ON)
#define RUT_MODIFIER_CTRL_ON                                                   \
    (RUT_MODIFIER_LEFT_CTRL_ON | RUT_MODIFIER_RIGHT_CTRL_ON)
#define RUT_MODIFIER_META_ON                                                   \
    (RUT_MODIFIER_LEFT_META_ON | RUT_MODIFIER_RIGHT_META_ON)

typedef enum _rut_input_event_status_t {
    RUT_INPUT_EVENT_STATUS_UNHANDLED,
    RUT_INPUT_EVENT_STATUS_HANDLED,
} rut_input_event_status_t;

typedef struct _rut_input_event_t {
    rut_list_t list_node;
    rut_input_event_type_t type;
    rut_shell_t *shell;
    rut_object_t *camera;
    const cg_matrix_t *input_transform;

    void *native;

    uint8_t data[];
} rut_input_event_t;

/* stream_t events are optimized for IPC based on the assumption that
 * the remote process does some state tracking to know the current
 * state of pointer buttons for example.
 */
typedef enum _rut_stream_event_type_t {
    RUT_STREAM_EVENT_POINTER_MOVE = 1,
    RUT_STREAM_EVENT_POINTER_DOWN,
    RUT_STREAM_EVENT_POINTER_UP,
    RUT_STREAM_EVENT_KEY_DOWN,
    RUT_STREAM_EVENT_KEY_UP
} rut_stream_event_type_t;

typedef struct _rut_stream_event_t {
    rut_stream_event_type_t type;
    uint64_t timestamp;

    union {
        struct {
            rut_button_state_t state;
            float x;
            float y;
        } pointer_move;
        struct {
            rut_button_state_t state;
            rut_button_state_t button;
            float x;
            float y;
        } pointer_button;
        struct {
            int keysym;
            rut_modifier_state_t mod_state;
        } key;
    };
} rut_stream_event_t;

typedef rut_input_event_status_t (*rut_input_callback_t)(
    rut_input_event_t *event, void *user_data);

typedef struct {
    rut_list_t list_node;
    rut_input_callback_t callback;
    rut_object_t *camera;
    void *user_data;
} rut_shell_grab_t;

typedef void (*RutPrePaintCallback)(rut_object_t *graphable, void *user_data);

typedef struct {
    rut_list_t link;

    cg_onscreen_t *onscreen;

    rut_cursor_t current_cursor;
    /* This is used to record whether anything set a cursor while
     * handling a mouse motion event. If nothing sets one then the shell
     * will put the cursor back to the default pointer. */
    bool cursor_set;

#if defined(USE_SDL)
    SDL_SysWMinfo sdl_info;
    SDL_Window *sdl_window;
    SDL_Cursor *cursor_image;
#endif
} rut_shell_onscreen_t;

typedef struct {
    rut_list_t list_node;

    int depth;
    rut_object_t *graphable;

    RutPrePaintCallback callback;
    void *user_data;
} rut_shell_pre_paint_entry_t;

typedef struct _rut_input_queue_t {
    rut_shell_t *shell;
    rut_list_t events;
    int n_events;
} rut_input_queue_t;

struct _rut_shell_t {
    rut_object_base_t _base;

    /* If true then this process does not handle input events directly
     * or output graphics directly. */
    bool headless;
#ifdef USE_SDL
    SDL_SYSWM_TYPE sdl_subsystem;
    SDL_Keymod sdl_keymod;
    uint32_t sdl_buttons;
    bool x11_grabbed;

    /* Note we can't use SDL_WaitEvent() to block for events given
     * that we also care about non-SDL based events.
     *
     * This lets us to use poll() to block until an SDL event
     * is recieved instead of busy waiting...
     */
    SDL_mutex *event_pipe_mutex;
    int event_pipe_read;
    int event_pipe_write;
    bool wake_queued;
#endif

#ifndef RIG_SIMULATOR_ONLY
    c_array_t *cg_poll_fds;
    int cg_poll_fds_age;
#endif

    int poll_sources_age;
    rut_list_t poll_sources;

    rut_list_t idle_closures;

#if 0
    int signal_read_fd;
    rut_list_t signal_cb_list;
#endif

    /* When running multiple shells in one thread we define one shell
     * as the "main" shell which owns the mainloop.
     */
    rut_shell_t *main_shell;

#ifdef USE_GLIB
    GMainLoop *main_loop;
#endif

#ifdef USE_UV
    uv_loop_t *uv_loop;
    uv_idle_t uv_idle;
    uv_prepare_t cg_prepare;
    uv_timer_t cg_timer;
    uv_check_t cg_check;
#ifdef __ANDROID__
    int uv_ready;
    bool quit;
#endif
#ifdef USE_GLIB
    GMainContext *glib_main_ctx;
    uv_prepare_t glib_uv_prepare;
    uv_check_t glib_uv_check;
    uv_timer_t glib_uv_timer;
    uv_check_t glib_uv_timer_check;
    GArray *pollfds;
    GArray *glib_polls;
    int n_pollfds;
#endif
#endif

    rut_closure_t *paint_idle;

    rut_input_queue_t *input_queue;
    int input_queue_len;

    rut_context_t *rut_ctx;

    rut_shell_init_callback_t init_cb;
    rut_shell_fini_callback_t fini_cb;
    rut_shell_paint_callback_t paint_cb;
    void *user_data;

    rut_list_t input_cb_list;
    c_list_t *input_cameras;

    /* Used to handle input events in window coordinates */
    rut_object_t *window_camera;

    /* Last known position of the mouse */
    float mouse_x;
    float mouse_y;

    rut_object_t *drag_payload;
    rut_object_t *drop_offer_taker;

    /* List of grabs that are currently in place. This are in order from
     * highest to lowest priority. */
    rut_list_t grabs;
    /* A pointer to the next grab to process. This is only used while
     * invoking the grab callbacks so that we can cope with multiple
     * grabs being removed from the list while one is being processed */
    rut_shell_grab_t *next_grab;

    rut_object_t *keyboard_focus_object;
    c_destroy_func_t keyboard_ungrab_cb;

    rut_object_t *clipboard;

    void (*queue_redraw_callback)(rut_shell_t *shell, void *user_data);
    void *queue_redraw_data;

    /* Queue of callbacks to be invoked before painting. If
     * ‘flushing_pre_paints‘ is true then this will be maintained in
     * sorted order. Otherwise it is kept in no particular order and it
     * will be sorted once prepaint flushing starts. That way it doesn't
     * need to keep track of hierarchy changes that occur after the
     * pre-paint was queued. This assumes that the depths won't change
     * will the queue is being flushed */
    rut_list_t pre_paint_callbacks;
    bool flushing_pre_paints;

    rut_list_t start_paint_callbacks;
    rut_list_t post_paint_callbacks;

    int frame;
    rut_list_t frame_infos;

    /* A list of onscreen windows that the shell is manipulating */
    rut_list_t onscreens;

    rut_object_t *selection;

    struct {
        cg_onscreen_t *(*input_event_get_onscreen)(rut_input_event_t *event);

        int32_t (*key_event_get_keysym)(rut_input_event_t *event);
        rut_key_event_action_t (*key_event_get_action)(rut_input_event_t *event);
        rut_modifier_state_t (*key_event_get_modifier_state)(rut_input_event_t *event);

        rut_motion_event_action_t (*motion_event_get_action)(rut_input_event_t *event);
        rut_button_state_t (*motion_event_get_button)(rut_input_event_t *event);
        rut_button_state_t (*motion_event_get_button_state)(rut_input_event_t *event);
        rut_modifier_state_t (*motion_event_get_modifier_state)(rut_input_event_t *event);
        void (*motion_event_get_transformed_xy)(rut_input_event_t *event,
                                                float *x,
                                                float *y);

        const char *(*text_event_get_text)(rut_input_event_t *event);

        void (*free_input_event)(rut_input_event_t *event);

    } platform;
};

typedef enum _rut_input_transform_type_t {
    RUT_INPUT_TRANSFORM_TYPE_NONE,
    RUT_INPUT_TRANSFORM_TYPE_MATRIX,
    RUT_INPUT_TRANSFORM_TYPE_GRAPHABLE
} rut_input_transform_type_t;

typedef struct _rut_input_transform_any_t {
    rut_input_transform_type_t type;
} rut_input_transform_any_t;

typedef struct _rut_input_transform_matrix_t {
    rut_input_transform_type_t type;
    cg_matrix_t *matrix;
} rut_input_transform_matrix_t;

typedef struct _rut_input_transform_graphable_t {
    rut_input_transform_type_t type;
} rut_input_transform_graphable_t;

typedef union _rut_input_transform_t {
    rut_input_transform_any_t any;
    rut_input_transform_matrix_t matrix;
    rut_input_transform_graphable_t graphable;
} rut_input_transform_t;

rut_shell_t *rut_shell_new(bool headless,
                           rut_shell_init_callback_t init,
                           rut_shell_fini_callback_t fini,
                           rut_shell_paint_callback_t paint,
                           void *user_data);

/* When running multiple shells in one thread we define one
 * shell as the "main" shell which owns the mainloop.
 */
void rut_shell_set_main_shell(rut_shell_t *shell, rut_shell_t *main_shell);

bool rut_shell_get_headless(rut_shell_t *shell);

/* XXX: Basically just a hack for now to effectively relate input events to
 * a cg_framebuffer_t and so we have a way to consistently associate a
 * camera with all input events.
 *
 * The camera should provide an orthographic projection into input device
 * coordinates and it's assume to be automatically updated according to
 * window resizes.
 */
void rut_shell_set_window_camera(rut_shell_t *shell,
                                 rut_object_t *window_camera);

/* PRIVATE */
void _rut_shell_associate_context(rut_shell_t *shell, rut_context_t *context);

void _rut_shell_init(rut_shell_t *shell);

rut_context_t *rut_shell_get_context(rut_shell_t *shell);

void rut_shell_main(rut_shell_t *shell);

/*
 * Whatever paint function is given when creating a rut_shell_t
 * is responsible for handling each redraw cycle but should
 * pass control back to the shell for progressing timlines,
 * running pre-paint callbacks and finally checking whether
 * to queue another redraw if there are any timelines
 * running...
 *
 * The folling apis can be used to implement a
 * rut_shell_paint_callback_t...
 */

/* Should be the first thing called for each redraw... */
void rut_shell_start_redraw(rut_shell_t *shell);

/* Progress timelines */
void rut_shell_update_timelines(rut_shell_t *shell);

void rut_shell_dispatch_input_events(rut_shell_t *shell);

/* Dispatch a single event as rut_shell_dispatch_input_events would */
rut_input_event_status_t
rut_shell_dispatch_input_event(rut_shell_t *shell, rut_input_event_t *event);

rut_input_queue_t *rut_input_queue_new(rut_shell_t *shell);

void rut_input_queue_append(rut_input_queue_t *queue, rut_input_event_t *event);

void rut_input_queue_remove(rut_input_queue_t *queue, rut_input_event_t *event);

void rut_input_queue_clear(rut_input_queue_t *queue);

rut_input_queue_t *rut_shell_get_input_queue(rut_shell_t *shell);

void rut_shell_run_pre_paint_callbacks(rut_shell_t *shell);

/* Determines whether any timelines are running */
bool rut_shell_check_timelines(rut_shell_t *shell);

void rut_shell_handle_stream_event(rut_shell_t *shell, rut_stream_event_t *event);

void rut_shell_run_start_paint_callbacks(rut_shell_t *shell);

void rut_shell_run_post_paint_callbacks(rut_shell_t *shell);

/* Delimit the end of a frame, at this point the frame counter is
 * incremented.
 */
void rut_shell_end_redraw(rut_shell_t *shell);

/* Called when a frame has really finished, e.g. when the Rig
 * simulator has finished responding to a run_frame request, sent its
 * update, the new frame has been rendered and presented to the user.
 */
void rut_shell_finish_frame(rut_shell_t *shell);

void rut_shell_add_input_camera(rut_shell_t *shell,
                                rut_object_t *camera,
                                rut_object_t *scenegraph);

void rut_shell_remove_input_camera(rut_shell_t *shell,
                                   rut_object_t *camera,
                                   rut_object_t *scenegraph);

/**
 * rut_shell_grab_input:
 * @shell: The #rut_shell_t
 * @camera: An optional camera to set on grabbed events
 * @callback: A callback to give all events to
 * @user_data: A pointer to pass to the callback
 *
 * Adds a grab which will get a chance to see all input events before
 * anything else handles them. The callback can return
 * %RUT_INPUT_EVENT_STATUS_HANDLED if no further processing should be
 * done for the event or %RUT_INPUT_EVENT_STATUS_UNHANDLED otherwise.
 *
 * Multiple grabs can be in place at the same time. In this case the
 * events will be given to the grabs in the reverse order that they
 * were added.
 *
 * If a camera is given for the grab then this camera will be set on
 * all input events before passing them to the callback.
 */
void rut_shell_grab_input(rut_shell_t *shell,
                          rut_object_t *camera,
                          rut_input_event_status_t (*callback)(
                              rut_input_event_t *event, void *user_data),
                          void *user_data);

/**
 * rut_shell_ungrab_input:
 * @shell: The #rut_shell_t
 * @callback: The callback that the grab was set with
 * @user_data: The user data that the grab was set with
 *
 * Removes a grab that was previously set with rut_shell_grab_input().
 * The @callback and @user_data must match the values passed when the
 * grab was taken.
 */
void rut_shell_ungrab_input(rut_shell_t *shell,
                            rut_input_event_status_t (*callback)(
                                rut_input_event_t *event, void *user_data),
                            void *user_data);

/*
 * Use this API for the common case of grabbing input for a pointer
 * when we receive a button press and want to grab until a
 * corresponding button release.
 */
void rut_shell_grab_pointer(rut_shell_t *shell,
                            rut_object_t *camera,
                            rut_input_event_status_t (*callback)(
                                rut_input_event_t *event, void *user_data),
                            void *user_data);

/**
 * rut_shell_grab_key_focus:
 * @shell: The #rut_shell_t
 * @inputable: A #rut_object_t that implements the inputable interface
 * @ungrab_callback: A callback to invoke when the grab is lost
 *
 * Sets the shell's key grab to the given object. The object must
 * implement the inputable interface. All key events will be given to
 * the input region of this object until either something else takes
 * the key focus or rut_shell_ungrab_key_focus() is called.
 */
void rut_shell_grab_key_focus(rut_shell_t *shell,
                              rut_object_t *inputable,
                              c_destroy_func_t ungrab_callback);

void rut_shell_ungrab_key_focus(rut_shell_t *shell);

void rut_shell_queue_redraw(rut_shell_t *shell);

void rut_shell_set_queue_redraw_callback(rut_shell_t *shell,
                                         void (*callback)(rut_shell_t *shell,
                                                          void *user_data),
                                         void *user_data);

/* You can hook into rut_shell_queue_redraw() via
 * rut_shell_set_queue_redraw_callback() but then it if you really
 * want to queue a redraw with the platform mainloop it is your
 * responsibility to call rut_shell_queue_redraw_real() */
void rut_shell_queue_redraw_real(rut_shell_t *shell);

rut_object_t *rut_input_event_get_camera(rut_input_event_t *event);

rut_input_event_type_t rut_input_event_get_type(rut_input_event_t *event);

/**
 * rut_input_event_get_onscreen:
 * @event: A #rut_input_event_t
 *
 * Return value: the onscreen window that this event was generated for
 *   or %NULL if the event does not correspond to a window.
 */
cg_onscreen_t *rut_input_event_get_onscreen(rut_input_event_t *event);

rut_key_event_action_t rut_key_event_get_action(rut_input_event_t *event);

int32_t rut_key_event_get_keysym(rut_input_event_t *event);

rut_modifier_state_t rut_key_event_get_modifier_state(rut_input_event_t *event);

rut_motion_event_action_t rut_motion_event_get_action(rut_input_event_t *event);

/* For a button-up/down event which specific button changed? */
rut_button_state_t rut_motion_event_get_button(rut_input_event_t *event);

rut_button_state_t rut_motion_event_get_button_state(rut_input_event_t *event);

rut_modifier_state_t
rut_motion_event_get_modifier_state(rut_input_event_t *event);

float rut_motion_event_get_x(rut_input_event_t *event);

float rut_motion_event_get_y(rut_input_event_t *event);

/**
 * rut_motion_event_unproject:
 * @event: A motion event
 * @graphable: An object that implements #rut_graph_table
 * @x: Output location for the unprojected coordinate
 * @y: Output location for the unprojected coordinate
 *
 * Unprojects the position of the motion event so that it will be
 * relative to the coordinate space of the given graphable object.
 *
 * Return value: %false if the coordinate can't be unprojected or
 *   %true otherwise. The coordinate can't be unprojected if the
 *   transform for the graphable object object does not have an inverse.
 */
bool rut_motion_event_unproject(rut_input_event_t *event,
                                rut_object_t *graphable,
                                float *x,
                                float *y);

rut_object_t *rut_drop_offer_event_get_payload(rut_input_event_t *event);

/* Returns the text generated by the event as a null-terminated UTF-8
 * encoded string. */
const char *rut_text_event_get_text(rut_input_event_t *event);

rut_object_t *rut_drop_event_get_data(rut_input_event_t *drop_event);

rut_closure_t *
rut_shell_add_input_callback(rut_shell_t *shell,
                             rut_input_callback_t callback,
                             void *user_data,
                             rut_closure_destroy_callback_t destroy_cb);

/**
 * rut_shell_add_pre_paint_callback:
 * @shell: The #rut_shell_t
 * @graphable: An object implementing the graphable interface
 * @callback: The callback to invoke
 * @user_data: A user data pointer to pass to the callback
 *
 * Adds a callback that will be invoked just before the next frame of
 * the shell is painted. The callback is associated with a graphable
 * object which is used to ensure the callbacks are invoked in
 * increasing order of depth in the hierarchy that the graphable
 * object belongs to. If this function is called a second time for the
 * same graphable object then no extra callback will be added. For
 * that reason, this function should always be called with the same
 * callback and user_data pointers for a particular graphable object.
 *
 * It is safe to call this function in the middle of a pre paint
 * callback. The shell will keep calling callbacks until all of the
 * pending callbacks are complete and no new callbacks were queued.
 *
 * Typically this callback will be registered when an object needs to
 * layout its children before painting. In that case it is expecting
 * that setting the size on the objects children may cause them to
 * also register a pre-paint callback.
 */
void rut_shell_add_pre_paint_callback(rut_shell_t *shell,
                                      rut_object_t *graphable,
                                      RutPrePaintCallback callback,
                                      void *user_data);

rut_closure_t *
rut_shell_add_start_paint_callback(rut_shell_t *shell,
                                   rut_shell_paint_callback_t callback,
                                   void *user_data,
                                   rut_closure_destroy_callback_t destroy);

rut_closure_t *
rut_shell_add_post_paint_callback(rut_shell_t *shell,
                                  rut_shell_paint_callback_t callback,
                                  void *user_data,
                                  rut_closure_destroy_callback_t destroy);

typedef struct _rut_frame_info_t {
    rut_list_t list_node;

    int frame;
    rut_list_t frame_callbacks;
} rut_frame_info_t;

rut_frame_info_t *rut_shell_get_frame_info(rut_shell_t *shell);

typedef void (*rut_shell_frame_callback_t)(rut_shell_t *shell,
                                           rut_frame_info_t *info,
                                           void *user_data);

rut_closure_t *
rut_shell_add_frame_callback(rut_shell_t *shell,
                             rut_shell_frame_callback_t callback,
                             void *user_data,
                             rut_closure_destroy_callback_t destroy);

/**
 * rut_shell_remove_pre_paint_callback_by_graphable:
 * @shell: The #rut_shell_t
 * @graphable: A graphable object
 *
 * Removes a pre-paint callback that was previously registered with
 * rut_shell_add_pre_paint_callback(). It is not an error to call this
 * function if no callback has actually been registered.
 */
void rut_shell_remove_pre_paint_callback_by_graphable(rut_shell_t *shell,
                                                      rut_object_t *graphable);

void rut_shell_remove_pre_paint_callback(rut_shell_t *shell,
                                         RutPrePaintCallback callback,
                                         void *user_data);

/**
 * rut_shell_add_onscreen:
 * @shell: The #rut_shell_t
 * @onscreen: A #cg_onscreen_t
 *
 * This should be called for everything onscreen that is going to be
 * used by the shell so that Rut can keep track of them.
 */
void rut_shell_add_onscreen(rut_shell_t *shell, cg_onscreen_t *onscreen);

/**
 * rut_shell_set_cursor:
 * @shell: The #rut_shell_t
 * @onscreen: An onscreen window to set the cursor for
 * @cursor: The new cursor
 *
 * Attempts to set the mouse cursor image to @cursor. The shell will
 * automatically reset the cursor back to the default pointer on every
 * mouse motion event if nothing else sets it. The expected usage is
 * that a widget which wants a particular cursor will listen for
 * motion events and always change the cursor when the pointer is over
 * a certain area.
 */
void rut_shell_set_cursor(rut_shell_t *shell,
                          cg_onscreen_t *onscreen,
                          rut_cursor_t cursor);

void rut_shell_set_title(rut_shell_t *shell,
                         cg_onscreen_t *onscreen,
                         const char *title);

/**
 * rut_shell_quit:
 * @shell: The #rut_shell_t
 *
 * Informs the shell that at the next time it returns to the main loop
 * it should quit the loop.
 */
void rut_shell_quit(rut_shell_t *shell);

/**
 * rut_shell_set_selection:
 * @selection: An object implementing the selectable interface
 *
 * This cancels any existing global selection, calling the ::cancel
 * method of the current selection and make @selection the replacement
 * selection.
 *
 * If Ctrl-C is later pressed then ::copy will be called for the
 * selectable and the returned object will be set on the clipboard.
 * If Ctrl-Z is later pressed then ::cut will be called for the
 * selectable and the returned object will be set on the clipboard.
 */
void rut_shell_set_selection(rut_shell_t *shell, rut_object_t *selection);

rut_object_t *rut_shell_get_selection(rut_shell_t *shell);

rut_object_t *rut_shell_get_clipboard(rut_shell_t *shell);

extern rut_type_t rut_text_blob_type;

typedef struct _rut_text_blob_t rut_text_blob_t;

rut_text_blob_t *rut_text_blob_new(const char *text);

void rut_shell_start_drag(rut_shell_t *shell, rut_object_t *payload);

void rut_shell_cancel_drag(rut_shell_t *shell);

void rut_shell_drop(rut_shell_t *shell);

void rut_shell_take_drop_offer(rut_shell_t *shell, rut_object_t *taker);

#if 0
typedef void (*rut_shell_signal_callback_t) (rut_shell_t *shell,
                                             int signal,
                                             void *user_data);

rut_closure_t *
rut_shell_add_signal_callback (rut_shell_t *shell,
                               rut_shell_signal_callback_t callback,
                               void *user_data,
                               rut_closure_destroy_callback_t destroy_cb);
#endif

#ifdef USE_SDL
void rut_shell_handle_sdl_event(rut_shell_t *shell, SDL_Event *sdl_event);
#endif

#endif /* _RUT_SHELL_H_ */
