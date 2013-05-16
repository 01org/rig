Building Rig
==

Rig can either be built against SDL for portability or for Android
(Although beware that the Android support is incomplete at this point)


Dependencies
--

1. **SDL (Simple DirectMedia Layer):**

   Rig uses SDL as a portable way to create an OpenGL context and
   provide input support. SDL should be available on most Linux
   distributions and most operating systems.

   SDL isn't required if building for Android where Rig uses *EGL* to
   create an OpenGL context and directly supports Android's input API.

   SDL 2.0 is highly recommended over SDL 1 although both should
   currently work.

2. **Cogl:**

    Rig uses the Cogl 3D graphics utility api to manage OpenGL state
    and provide math utilities such as matrix, quaternion and vector
    apis.

    Rig currently depends on the latest Cogl *1.99* development API
    which means fetching git master from here:

    `git clone git://git.gnome.org/cogl`

3. **Pango:**

    The Rig editor relies on Pango for complex text layouting and
    rendering via the *cogl-pango* api.


Rig is a standard autotooled project, so once you have fetched the
above dependencies then building Rig is just a matter of running:

        ./configure
        make
        make install

*Note: Rig can't be run uninstalled currently since the editor won't
find its assets. Once you have installed the assets though then it's
possible to run incremental rebuilds of Rig in place without
re-installing.*

For deploying completed applications Rig's editing capabilities can
optionally be disabled at build time leaving only the minimal runtime.
This is done by passing the `--disable-editor` argument to the
`./configure` script.

Running
==

Rig can be run in two modes:

1. **Editor Mode**

    This mode is for interactively designing a new user interface.
    This mode is used by default if the only argument you give to Rig
    is a path to a UI.rig file like:

        rig /path/to/my/ui.rig

    If the file doesn't exist then it will be created when you
    save by pressing `Ctrl-S` so long as the parent directory already
    exists.

    **Note:** When Rig is run in editor mode it will automatically
    search for image assets under the parent directory of your ui
    file. (So under `/path/to/my` for the above example) Because of
    this, it would not be recommended to create a new ui.rig at the
    top of your home directory.

2. **Device Mode**

    Once you have a UI that you want to run as an application Rig can
    instead be run in a chromeless device mode like this:

        rig --device-mode /path/to/my/ui.rig

    Or simply:

        rig -d /path/to/my/ui.rig
