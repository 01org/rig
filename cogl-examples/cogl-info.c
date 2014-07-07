#include <cogl/cogl.h>
#include <glib.h>
#include <stdio.h>

struct {
    cg_feature_id_t feature;
    const char *short_description;
    const char *long_description;
} features[] = {
    { CG_FEATURE_ID_TEXTURE_NPOT_BASIC, "Non power of two textures (basic)",
      "The hardware supports non power of two textures, but you also "
      "need to check the CG_FEATURE_ID_TEXTURE_NPOT_MIPMAP and "
      "CG_FEATURE_ID_TEXTURE_NPOT_REPEAT features to know if the "
      "hardware supports npot texture mipmaps or repeat modes other "
      "than CG_RENDERER_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE respectively." },
    { CG_FEATURE_ID_TEXTURE_NPOT_MIPMAP,
      "Non power of two textures (+ mipmap)",
      "Mipmapping is supported in conjuntion with non power of two "
      "textures." },
    { CG_FEATURE_ID_TEXTURE_NPOT_REPEAT,
      "Non power of two textures (+ repeat modes)",
      "Repeat modes other than "
      "CG_RENDERER_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE are supported by "
      "the hardware in conjunction with non power of two textures." },
    { CG_FEATURE_ID_TEXTURE_NPOT,
      "Non power of two textures (fully featured)",
      "Non power of two textures are supported by the hardware. This "
      "is a equivalent to the CG_FEATURE_ID_TEXTURE_NPOT_BASIC, "
      "CG_FEATURE_ID_TEXTURE_NPOT_MIPMAP and "
      "CG_FEATURE_ID_TEXTURE_NPOT_REPEAT features combined." },
    { CG_FEATURE_ID_TEXTURE_3D, "3D texture support", "3D texture support" },
    { CG_FEATURE_ID_OFFSCREEN, "Offscreen rendering support",
      "Offscreen rendering support" },
    { CG_FEATURE_ID_OFFSCREEN_MULTISAMPLE,
      "Offscreen rendering with multisampling support",
      "Offscreen rendering with multisampling support" },
    { CG_FEATURE_ID_ONSCREEN_MULTIPLE,
      "Multiple onscreen framebuffers supported",
      "Multiple onscreen framebuffers supported" },
    { CG_FEATURE_ID_GLSL, "GLSL support", "GLSL support" },
    { CG_FEATURE_ID_UNSIGNED_INT_INDICES, "Unsigned integer indices",
      "CG_RENDERER_INDICES_TYPE_UNSIGNED_INT is supported in "
      "cg_indices_new()." },
    { CG_FEATURE_ID_DEPTH_RANGE,
      "cg_pipeline_set_depth_range() support",
      "cg_pipeline_set_depth_range() support", },
    { CG_FEATURE_ID_POINT_SPRITE, "Point sprite coordinates",
      "cg_pipeline_set_layer_point_sprite_coords_enabled() is supported" },
    { CG_FEATURE_ID_MAP_BUFFER_FOR_READ, "Mapping buffers for reading",
      "Mapping buffers for reading" },
    { CG_FEATURE_ID_MAP_BUFFER_FOR_WRITE, "Mapping buffers for writing",
      "Mapping buffers for writing" },
    { CG_FEATURE_ID_MIRRORED_REPEAT, "Mirrored repeat wrap modes",
      "Mirrored repeat wrap modes" },
    { CG_FEATURE_ID_GLES2_CONTEXT, "GLES2 API integration supported",
      "Support for creating a GLES2 context for using the GLES2 API in a "
      "way that's integrated with Cogl." },
    { CG_FEATURE_ID_DEPTH_TEXTURE, "Depth Textures",
      "cg_framebuffer_ts can be configured to render their depth buffer into "
      "a texture" },
    { CG_FEATURE_ID_PER_VERTEX_POINT_SIZE, "Per-vertex point size",
      "cg_point_size_in can be used as an attribute to specify a per-vertex "
      "point size" }
};

static const char *
get_winsys_name_for_id(cg_winsys_id_t winsys_id)
{
    switch (winsys_id) {
    case CG_WINSYS_ID_ANY:
        g_return_val_if_reached("ERROR");
    case CG_WINSYS_ID_STUB:
        return "Stub";
    case CG_WINSYS_ID_GLX:
        return "GLX";
    case CG_WINSYS_ID_EGL_XLIB:
        return "EGL + Xlib platform";
    case CG_WINSYS_ID_EGL_NULL:
        return "EGL + NULL window system platform";
    case CG_WINSYS_ID_EGL_GDL:
        return "EGL + GDL platform";
    case CG_WINSYS_ID_EGL_WAYLAND:
        return "EGL + Wayland platform";
    case CG_WINSYS_ID_EGL_KMS:
        return "EGL + KMS platform";
    case CG_WINSYS_ID_EGL_ANDROID:
        return "EGL + Android platform";
    case CG_WINSYS_ID_WGL:
        return "EGL + Windows WGL platform";
    case CG_WINSYS_ID_SDL:
        return "EGL + SDL platform";
    }
    g_return_val_if_reached("Unknown");
}

static void
feature_cb(cg_feature_id_t feature, void *user_data)
{
    int i;
    for (i = 0; i < sizeof(features) / sizeof(features[0]); i++) {
        if (features[i].feature == feature) {
            printf(" » %s\n", features[i].short_description);
            return;
        }
    }
    printf(" » Unknown feature %d\n", feature);
}

typedef struct _OutputState {
    int id;
} OutputState;

static void
output_cb(cg_output_t *output, void *user_data)
{
    OutputState *state = user_data;
    const char *order;
    float refresh;

    printf(" Output%d:\n", state->id++);
    printf("  » position = (%d, %d)\n",
           cg_output_get_x(output),
           cg_output_get_y(output));
    printf("  » resolution = %d x %d\n",
           cg_output_get_width(output),
           cg_output_get_height(output));
    printf("  » physical size = %dmm x %dmm\n",
           cg_output_get_mm_width(output),
           cg_output_get_mm_height(output));
    switch (cg_output_get_subpixel_order(output)) {
    case CG_SUBPIXEL_ORDER_UNKNOWN:
        order = "unknown";
        break;
    case CG_SUBPIXEL_ORDER_NONE:
        order = "non-standard";
        break;
    case CG_SUBPIXEL_ORDER_HORIZONTAL_RGB:
        order = "horizontal,rgb";
        break;
    case CG_SUBPIXEL_ORDER_HORIZONTAL_BGR:
        order = "horizontal,bgr";
        break;
    case CG_SUBPIXEL_ORDER_VERTICAL_RGB:
        order = "vertical,rgb";
        break;
    case CG_SUBPIXEL_ORDER_VERTICAL_BGR:
        order = "vertical,bgr";
        break;
    }
    printf("  » sub pixel order = %s\n", order);

    refresh = cg_output_get_refresh_rate(output);
    if (refresh)
        printf("  » refresh = %f Hz\n", refresh);
    else
        printf("  » refresh = unknown\n");
}

int
main(int argc, char **argv)
{
    cg_renderer_t *renderer;
    cg_display_t *display;
    cg_context_t *ctx;
    cg_error_t *error = NULL;
    cg_winsys_id_t winsys_id;
    const char *winsys_name;
    OutputState output_state;

#ifdef CG_HAS_EMSCRIPTEN_SUPPORT
    ctx = cg_sdl_context_new(SDL_USEREVENT, &error);
#else
    ctx = cg_context_new(NULL, &error);
#endif
    if (!ctx) {
        fprintf(stderr, "Failed to create context: %s\n", error->message);
        return 1;
    }

    display = cg_context_get_display(ctx);
    renderer = cg_display_get_renderer(display);
    winsys_id = cg_renderer_get_winsys_id(renderer);
    winsys_name = get_winsys_name_for_id(winsys_id);
    g_print("Renderer: %s\n\n", winsys_name);

    g_print("Features:\n");
    cg_foreach_feature(ctx, feature_cb, NULL);

    g_print("Outputs:\n");
    output_state.id = 0;
    cg_renderer_foreach_output(renderer, output_cb, &output_state);
    if (output_state.id == 0)
        printf(" Unknown\n");

    return 0;
}
