
noinst_LTLIBRARIES = librut.la

components_source_h = \
    components/rut-camera.h \
    components/rut-light.h \
    components/rut-model.h \
    components/rut-material.h \
    components/rut-diamond.h \
    components/rut-shape.h \
    components/rut-pointalism-grid.h

source_h = \
    $(components_source_h) \
    rut-color.h \
    rut-image-source.h \
    rut-entity.h \
    rut-components.h \
    rut-type.h \
    rut-object.h \
    rut-property.h \
    rut-types.h \
    rut-interfaces.h \
    rut-planes.h \
    rut-timeline.h \
    rut-display-list.h \
    rut.h \
    rut-arcball.h \
    rut-bitmask.h \
    rut-shell.h \
    rut-keysyms.h \
    rut-memory-stack.h \
    rut-global.h \
    rut-util.h \
    rut-text-buffer.h \
    rut-text.h \
    rut-tool.h \
    rut-geometry.h \
    rut-context.h \
    rut-number-slider.h \
    rut-vec3-slider.h \
    rut-paintable.h \
    rut-drop-down.h \
    rut-inspector.h \
    rut-prop-inspector.h \
    rut-closure.h \
    rut-asset.h \
    rut-stack.h \
    rut-entry.h \
    rut-gaussian-blurrer.h \
    rut-downsampler.h \
    rut-toggle.h \
    rut-dof-effect.h \
    rut-mesh.h \
    rut-mesh-ply.h \
    rut-ui-viewport.h \
    rut-scroll-bar.h \
    rut-image.h \
    rut-property-types.h \
    rut-volume.h \
    rut-color-picker.h \
    rut-color-button.h \
    rut-box-layout.h \
    rut-bin.h \
    rut-icon.h \
    rut-flow-layout.h \
    rut-button.h \
    rut-graph.h \
    rut-transform.h \
    rut-rectangle.h \
    rut-nine-slice.h \
    rut-fixed.h \
    rut-fold.h \
    rut-refcount-debug.h \
    rut-icon-burron.h

librut_la_SOURCES = \
    components/rut-camera.c \
    components/rut-light.c \
    components/rut-model.c \
    components/rut-material.c \
    components/rut-diamond.c \
    components/rut-shape.c \
    components/rut-pointalism-grid.c \
    rply.c \
    rply.h \
    color-table.h \
    rut-color.c \
    rut-image-source.c \
    rut-entity.c \
    rut-type.c \
    rut-object.c \
    rut-property.c \
    rut-interfaces.c \
    rut.c \
    rut-arcball.c \
    rut-transform-private.h \
    rut-camera-private.h \
    rut-volume.c \
    rut-volume-private.h \
    rut-planes.c \
    rut-shell.c \
    rut-bitmask.h \
    rut-bitmask.c \
    rut-flags.h \
    rut-memory-stack.c \
    rut-list.c \
    rut-list.h \
    rut-util.c \
    rut-timeline.c \
    rut-display-list.c \
    rut-text-buffer.c \
    rut-text.c \
    rut-tool.c \
    rut-geometry.c \
    rut-number-slider.c \
    rut-vec3-slider.c \
    rut-paintable.c \
    rut-drop-down.c \
    rut-inspector.c \
    rut-prop-inspector.c \
    rut-closure.c \
    rut-asset.c \
    rut-stack.c \
    rut-entry.c \
    rut-gaussian-blurrer.c \
    rut-downsampler.c \
    rut-toggle.c \
    rut-dof-effect.c \
    rut-mesh.c \
    rut-mesh-ply.c \
    rut-ui-viewport.c \
    rut-scroll-bar.c \
    rut-image.c \
    rut-color-picker.c \
    rut-color-button.c \
    rut-box-layout.c \
    rut-bin.c \
    rut-icon.c \
    rut-flow-layout.c \
    rut-button.c \
    rut-graph.c \
    rut-transform.c \
    rut-rectangle.c \
    rut-nine-slice.c \
    rut-fixed.c \
    rut-fold.c \
    rut-refcount-debug.c \
    rut-icon-button.c \
    $(source_h)

if !HAVE_ANDROID
librut_la_SOURCES += \
    rut-sdl-keysyms.c \
    rut-sdl-keysyms.h
endif

librut_la_LIBADD = \
    @RIG_DEP_LIBS@ \
    @RIG_EXTRA_LDFLAGS@
librut_la_LDFLAGS = \
    -no-undefined \
    -version-info @RIG_LT_CURRENT@:@RIG_LT_REVISION@:@RIG_LT_AGE@ \
    -export-dynamic \
    -export-symbols-regex "(^rut.*)|(^_rut_bitmask.*)"
librut_la_CFLAGS = \
    @RIG_DEP_CFLAGS@ \
    @RIG_EXTRA_CFLAGS@
librut_la_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(top_srcdir)/rut

# rut_headersdir =  $(includedir)/rut
# rut_headers_DATA = $(source_h)

# rut_components_headersdir =  $(includedir)/rut/components
# rut_components_headers_DATA = $(components_source_h)
