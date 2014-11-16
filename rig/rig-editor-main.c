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
#include <clib.h>

#include <rut.h>
#ifdef USE_GSTREAMER
#include <cogl-gst/cogl-gst.h>
#endif

#include "rig-editor.h"
#include "rig-curses-debug.h"

static void
usage(void)
{
    fprintf(stderr, "Usage: rig [UI.rig]\n");
    fprintf(stderr, "\n");

#ifdef RIG_ENABLE_DEBUG
#ifdef linux
    fprintf(stderr, "  -a,--abstract-socket=NAME            Listen on abstract socket for simulator\n");
#endif
    fprintf(stderr, "  -t,--thread-simulator                Run simulator in a separate thread\n");
    fprintf(stderr, "  -m,--mainloop-simulator              Run simulator in the same mainloop as frontend\n");
    fprintf(stderr, "                                       (Simulator runs in separate process by default)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -d,--disable-curses                  Disable curses debug console\n");
    fprintf(stderr, "\n");
#endif

    fprintf(stderr, "  -h,--help    Display this help message\n");
    exit(1);
}

int
main(int argc, char **argv)
{
    rig_editor_t *editor;
    struct option long_opts[] = {

#ifdef RIG_ENABLE_DEBUG
#ifdef linux
        { "abstract-socket",    required_argument, NULL, 'a' },
#endif
        { "thread-simulator",   no_argument,       NULL, 't' },
        { "mainloop-simulator", no_argument,       NULL, 'm' },
        { "disable-curses",     no_argument,       NULL, 'd' },
#endif

        { "help",               no_argument,       NULL, 'h' },
        { 0,                    0,                 NULL,  0  }
    };

#ifdef RIG_ENABLE_DEBUG
# ifdef linux
    const char *short_opts = "a:tmdh";
# else
    const char *short_opts = "tmdh";
# endif
    bool enable_curses_debug = true;
#else
    const char *short_opts = "h";
#endif

    int c;

    rut_init_tls_state();

#ifdef USE_GSTREAMER
    gst_init(&argc, &argv);
#endif

    rig_simulator_run_mode_option = RIG_SIMULATOR_RUN_MODE_PROCESS;

    while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
        switch(c) {
#ifdef RIG_ENABLE_DEBUG
        case 'a':
            rig_simulator_run_mode_option =
                RIG_SIMULATOR_RUN_MODE_CONNECT_ABSTRACT_SOCKET;
            rig_abstract_socket_name_option = optarg;
            break;
        case 't':
            rig_simulator_run_mode_option = RIG_SIMULATOR_RUN_MODE_THREADED;
            break;
        case 'm':
            rig_simulator_run_mode_option = RIG_SIMULATOR_RUN_MODE_MAINLOOP;
            break;
        case 'd':
            enable_curses_debug = false;
            break;
#endif
        default:
            usage();
        }
    }

    if (optind > argc || !argv[optind]) {
        fprintf(stderr, "Needs a UI.rig filename\n\n");
        usage();
    }

#ifdef RIG_ENABLE_DEBUG
    if (enable_curses_debug)
        rig_curses_init();
#endif

    editor = rig_editor_new(argv[optind]);

    rig_editor_run(editor);

    rut_object_unref(editor);

    return 0;
}
