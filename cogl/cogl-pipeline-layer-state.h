/*
 * Cogl
 *
 * A Low-Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
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
 *
 */

#if !defined(__CG_H_INSIDE__) && !defined(CG_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __CG_PIPELINE_LAYER_STATE_H__
#define __CG_PIPELINE_LAYER_STATE_H__

#include <cogl/cogl-pipeline.h>
#include <cogl/cogl-color.h>
#include <cogl/cogl-matrix.h>
#include <cogl/cogl-texture.h>

CG_BEGIN_DECLS

/**
 * cg_pipeline_filter_t:
 * @CG_PIPELINE_FILTER_NEAREST: Measuring in manhatten distance from the,
 *   current pixel center, use the nearest texture texel
 * @CG_PIPELINE_FILTER_LINEAR: Use the weighted average of the 4 texels
 *   nearest the current pixel center
 * @CG_PIPELINE_FILTER_NEAREST_MIPMAP_NEAREST: Select the mimap level whose
 *   texel size most closely matches the current pixel, and use the
 *   %CG_PIPELINE_FILTER_NEAREST criterion
 * @CG_PIPELINE_FILTER_LINEAR_MIPMAP_NEAREST: Select the mimap level whose
 *   texel size most closely matches the current pixel, and use the
 *   %CG_PIPELINE_FILTER_LINEAR criterion
 * @CG_PIPELINE_FILTER_NEAREST_MIPMAP_LINEAR: Select the two mimap levels
 *   whose texel size most closely matches the current pixel, use
 *   the %CG_PIPELINE_FILTER_NEAREST criterion on each one and take
 *   their weighted average
 * @CG_PIPELINE_FILTER_LINEAR_MIPMAP_LINEAR: Select the two mimap levels
 *   whose texel size most closely matches the current pixel, use
 *   the %CG_PIPELINE_FILTER_LINEAR criterion on each one and take
 *   their weighted average
 *
 * Texture filtering is used whenever the current pixel maps either to more
 * than one texture element (texel) or less than one. These filter enums
 * correspond to different strategies used to come up with a pixel color, by
 * possibly referring to multiple neighbouring texels and taking a weighted
 * average or simply using the nearest texel.
 */
typedef enum {
    CG_PIPELINE_FILTER_NEAREST = 0x2600,
    CG_PIPELINE_FILTER_LINEAR = 0x2601,
    CG_PIPELINE_FILTER_NEAREST_MIPMAP_NEAREST = 0x2700,
    CG_PIPELINE_FILTER_LINEAR_MIPMAP_NEAREST = 0x2701,
    CG_PIPELINE_FILTER_NEAREST_MIPMAP_LINEAR = 0x2702,
    CG_PIPELINE_FILTER_LINEAR_MIPMAP_LINEAR = 0x2703
} cg_pipeline_filter_t;
/* NB: these values come from the equivalents in gl.h */

/**
 * cg_pipeline_wrap_mode_t:
 * @CG_PIPELINE_WRAP_MODE_REPEAT: The texture will be repeated. This
 *   is useful for example to draw a tiled background.
 * @CG_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE: The coordinates outside the
 *   range 0→1 will sample copies of the edge pixels of the
 *   texture. This is useful to avoid artifacts if only one copy of
 *   the texture is being rendered.
 *
 * The wrap mode specifies what happens when texture coordinates
 * outside the range 0→1 are used. Note that if the filter mode is
 * anything but %CG_PIPELINE_FILTER_NEAREST then texels outside the
 * range 0→1 might be used even when the coordinate is exactly 0 or 1
 * because OpenGL will try to sample neighbouring pixels. For example
 * if you are trying to render the full texture then you may get
 * artifacts around the edges when the pixels from the other side are
 * merged in if the wrap mode is set to repeat.
 *
 */
/*
 * XXX: keep the values in sync with the cg_sampler_cache_wrap_mode_t enum
 * so no conversion is actually needed.
 */
typedef enum {
    CG_PIPELINE_WRAP_MODE_REPEAT = 0x2901,
    CG_PIPELINE_WRAP_MODE_MIRRORED_REPEAT = 0x8370,
    CG_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE = 0x812F,
} cg_pipeline_wrap_mode_t;
/* NB: these values come from the equivalents in gl.h */

/**
 * cg_pipeline_set_layer:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the index of the layer
 * @texture: a #cg_texture_t for the layer object
 *
 * In addition to the standard OpenGL lighting model a Cogl pipeline may have
 * one or more layers comprised of textures that can be blended together in
 * order, with a number of different texture combine modes. This function
 * defines a new texture layer.
 *
 * The index values of multiple layers do not have to be consecutive; it is
 * only their relative order that is important.
 *
 * The @texture parameter can also be %NULL in which case the pipeline
 * will use a default white texture. The type of the default texture
 * will be the same as whatever texture was last used for the pipeline
 * or %CG_TEXTURE_TYPE_2D if none has been specified yet. To
 * explicitly specify the type of default texture required, use
 * cg_pipeline_set_layer_null_texture() instead.
 *
 * <note>In the future, we may define other types of pipeline layers, such
 * as purely GLSL based layers.</note>
 *
 * Stability: unstable
 */
void cg_pipeline_set_layer_texture(cg_pipeline_t *pipeline,
                                   int layer_index,
                                   cg_texture_t *texture);

/**
 * cg_pipeline_set_layer_null_texture:
 * @pipeline: A #cg_pipeline_t
 * @layer_index: The layer number to modify
 * @texture_type: The type of the default texture to use
 *
 * Sets the texture for this layer to be the default texture for the
 * given type. This is equivalent to calling
 * cg_pipeline_set_layer_texture() with %NULL for the texture
 * argument except that you can also specify the type of default
 * texture to use. The default texture is a 1x1 pixel white texture.
 *
 * This function is mostly useful if you want to create a base
 * pipeline that you want to create multiple copies from using
 * cg_pipeline_copy(). In that case this function can be used to
 * specify the texture type so that any pipeline copies can share the
 * internal texture type state for efficiency.
 *
 * Stability: unstable
 */
void cg_pipeline_set_layer_null_texture(cg_pipeline_t *pipeline,
                                        int layer_index,
                                        cg_texture_type_t texture_type);

/**
 * cg_pipeline_get_layer_texture:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the index of the layer
 *
 * Return value: the texture that was set for the given layer of the
 *   pipeline or %NULL if no texture was set.
 * Stability: unstable
 */
cg_texture_t *cg_pipeline_get_layer_texture(cg_pipeline_t *pipeline,
                                            int layer_index);

/**
 * cg_pipeline_remove_layer:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: Specifies the layer you want to remove
 *
 * This function removes a layer from your pipeline
 * Stability: unstable
 */
void cg_pipeline_remove_layer(cg_pipeline_t *pipeline, int layer_index);

/**
 * cg_pipeline_set_layer_combine:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: Specifies the layer you want define a combine function for
 * @blend_string: A <link linkend="cogl-Blend-Strings">Cogl blend string</link>
 *    describing the desired texture combine function.
 * @error: A #cg_error_t that may report parse errors or lack of GPU/driver
 *   support. May be %NULL, in which case a warning will be printed out if an
 *   error is encountered.
 *
 * If not already familiar; you can refer
 * <link linkend="cogl-Blend-Strings">here</link> for an overview of what blend
 * strings are and there syntax.
 *
 * These are all the functions available for texture combining:
 * <itemizedlist>
 *   <listitem>REPLACE(arg0) = arg0</listitem>
 *   <listitem>MODULATE(arg0, arg1) = arg0 x arg1</listitem>
 *   <listitem>ADD(arg0, arg1) = arg0 + arg1</listitem>
 *   <listitem>ADD_SIGNED(arg0, arg1) = arg0 + arg1 - 0.5</listitem>
 *   <listitem>INTERPOLATE(arg0, arg1, arg2) = arg0 x arg2 + arg1 x (1 -
 * arg2)</listitem>
 *   <listitem>SUBTRACT(arg0, arg1) = arg0 - arg1</listitem>
 *   <listitem>
 *     <programlisting>
 *  DOT3_RGB(arg0, arg1) = 4 x ((arg0[R] - 0.5)) * (arg1[R] - 0.5) +
 *                              (arg0[G] - 0.5)) * (arg1[G] - 0.5) +
 *                              (arg0[B] - 0.5)) * (arg1[B] - 0.5))
 *     </programlisting>
 *   </listitem>
 *   <listitem>
 *     <programlisting>
 *  DOT3_RGBA(arg0, arg1) = 4 x ((arg0[R] - 0.5)) * (arg1[R] - 0.5) +
 *                               (arg0[G] - 0.5)) * (arg1[G] - 0.5) +
 *                               (arg0[B] - 0.5)) * (arg1[B] - 0.5))
 *     </programlisting>
 *   </listitem>
 * </itemizedlist>
 *
 * Refer to the
 * <link linkend="cogl-Blend-String-syntax">color-source syntax</link> for
 * describing the arguments. The valid source names for texture combining
 * are:
 * <variablelist>
 *   <varlistentry>
 *     <term>TEXTURE</term>
 *     <listitem>Use the color from the current texture layer</listitem>
 *   </varlistentry>
 *   <varlistentry>
 *     <term>TEXTURE_0, TEXTURE_1, etc</term>
 *     <listitem>Use the color from the specified texture layer</listitem>
 *   </varlistentry>
 *   <varlistentry>
 *     <term>CONSTANT</term>
 *     <listitem>Use the color from the constant given with
 *     cg_pipeline_set_layer_combine_constant()</listitem>
 *   </varlistentry>
 *   <varlistentry>
 *     <term>PRIMARY</term>
 *     <listitem>Use the color of the pipeline as set with
 *     cg_pipeline_set_color()</listitem>
 *   </varlistentry>
 *   <varlistentry>
 *     <term>PREVIOUS</term>
 *     <listitem>Either use the texture color from the previous layer, or
 *     if this is layer 0, use the color of the pipeline as set with
 *     cg_pipeline_set_color()</listitem>
 *   </varlistentry>
 * </variablelist>
 *
 * <refsect2 id="cogl-Layer-Combine-Examples">
 *   <title>Layer Combine Examples</title>
 *   <para>This is effectively what the default blending is:</para>
 *   <informalexample><programlisting>
 *   RGBA = MODULATE (PREVIOUS, TEXTURE)
 *   </programlisting></informalexample>
 *   <para>This could be used to cross-fade between two images, using
 *   the alpha component of a constant as the interpolator. The constant
 *   color is given by calling
 *   cg_pipeline_set_layer_combine_constant().</para>
 *   <informalexample><programlisting>
 *   RGBA = INTERPOLATE (PREVIOUS, TEXTURE, CONSTANT[A])
 *   </programlisting></informalexample>
 * </refsect2>
 *
 * <note>You can't give a multiplication factor for arguments as you can
 * with blending.</note>
 *
 * Return value: %true if the blend string was successfully parsed, and the
 *   described texture combining is supported by the underlying driver and
 *   or hardware. On failure, %false is returned and @error is set
 *
 * Stability: unstable
 */
bool cg_pipeline_set_layer_combine(cg_pipeline_t *pipeline,
                                   int layer_index,
                                   const char *blend_string,
                                   cg_error_t **error);

/**
 * cg_pipeline_set_layer_combine_constant:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: Specifies the layer you want to specify a constant used
 *               for texture combining
 * @constant: The constant color you want
 *
 * When you are using the 'CONSTANT' color source in a layer combine
 * description then you can use this function to define its value.
 *
 * Stability: unstable
 */
void cg_pipeline_set_layer_combine_constant(cg_pipeline_t *pipeline,
                                            int layer_index,
                                            const cg_color_t *constant);

/**
 * cg_pipeline_get_n_layers:
 * @pipeline: A #cg_pipeline_t object
 *
 * Retrieves the number of layers defined for the given @pipeline
 *
 * Return value: the number of layers
 *
 * Stability: unstable
 */
int cg_pipeline_get_n_layers(cg_pipeline_t *pipeline);

/**
 * cg_pipeline_set_layer_filters:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to change.
 * @min_filter: the filter used when scaling a texture down.
 * @mag_filter: the filter used when magnifying a texture.
 *
 * Changes the decimation and interpolation filters used when a texture is
 * drawn at other scales than 100%.
 *
 * <note>It is an error to pass anything other than
 * %CG_PIPELINE_FILTER_NEAREST or %CG_PIPELINE_FILTER_LINEAR as
 * magnification filters since magnification doesn't ever need to
 * reference values stored in the mipmap chain.</note>
 *
 * Stability: unstable
 */
void cg_pipeline_set_layer_filters(cg_pipeline_t *pipeline,
                                   int layer_index,
                                   cg_pipeline_filter_t min_filter,
                                   cg_pipeline_filter_t mag_filter);

/**
 * cg_pipeline_get_layer_min_filter:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to change.
 *
 * Retrieves the currently set minification #cg_pipeline_filter_t set on
 * the specified layer. The miniifcation filter determines how the
 * layer should be sampled when down-scaled.
 *
 * The default filter is %CG_PIPELINE_FILTER_LINEAR but this can be
 * changed using cg_pipeline_set_layer_filters().
 *
 * Return value: The minification #cg_pipeline_filter_t for the
 *               specified layer.
 * Stability: unstable
 */
cg_pipeline_filter_t cg_pipeline_get_layer_min_filter(cg_pipeline_t *pipeline,
                                                      int layer_index);

/**
 * cg_pipeline_get_layer_mag_filter:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to change.
 *
 * Retrieves the currently set magnification #cg_pipeline_filter_t set on
 * the specified layer. The magnification filter determines how the
 * layer should be sampled when up-scaled.
 *
 * The default filter is %CG_PIPELINE_FILTER_LINEAR but this can be
 * changed using cg_pipeline_set_layer_filters().
 *
 * Return value: The magnification #cg_pipeline_filter_t for the
 *               specified layer.
 * Stability: unstable
 */
cg_pipeline_filter_t cg_pipeline_get_layer_mag_filter(cg_pipeline_t *pipeline,
                                                      int layer_index);

/**
 * cg_pipeline_set_layer_point_sprite_coords_enabled:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to change.
 * @enable: whether to enable point sprite coord generation.
 * @error: A return location for a cg_error_t, or NULL to ignore errors.
 *
 * When rendering points, if @enable is %true then the texture
 * coordinates for this layer will be replaced with coordinates that
 * vary from 0.0 to 1.0 across the primitive. The top left of the
 * point will have the coordinates 0.0,0.0 and the bottom right will
 * have 1.0,1.0. If @enable is %false then the coordinates will be
 * fixed for the entire point.
 *
 * This function will only work if %CG_FEATURE_ID_POINT_SPRITE is
 * available. If the feature is not available then the function will
 * return %false and set @error.
 *
 * Return value: %true if the function succeeds, %false otherwise.
 * Stability: unstable
 */
bool cg_pipeline_set_layer_point_sprite_coords_enabled(cg_pipeline_t *pipeline,
                                                       int layer_index,
                                                       bool enable,
                                                       cg_error_t **error);

/**
 * cg_pipeline_get_layer_point_sprite_coords_enabled:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to check.
 *
 * Gets whether point sprite coordinate generation is enabled for this
 * texture layer.
 *
 * Return value: whether the texture coordinates will be replaced with
 * point sprite coordinates.
 *
 * Stability: unstable
 */
bool cg_pipeline_get_layer_point_sprite_coords_enabled(cg_pipeline_t *pipeline,
                                                       int layer_index);

/**
 * cg_pipeline_get_layer_wrap_mode_s:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to change.
 *
 * Returns the wrap mode for the 's' coordinate of texture lookups on this
 * layer.
 *
 * Return value: the wrap mode for the 's' coordinate of texture lookups on
 * this layer.
 *
 * Stability: unstable
 */
cg_pipeline_wrap_mode_t
cg_pipeline_get_layer_wrap_mode_s(cg_pipeline_t *pipeline, int layer_index);

/**
 * cg_pipeline_set_layer_wrap_mode_s:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to change.
 * @mode: the new wrap mode
 *
 * Sets the wrap mode for the 's' coordinate of texture lookups on this layer.
 *
 * Stability: unstable
 */
void cg_pipeline_set_layer_wrap_mode_s(cg_pipeline_t *pipeline,
                                       int layer_index,
                                       cg_pipeline_wrap_mode_t mode);

/**
 * cg_pipeline_get_layer_wrap_mode_t:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to change.
 *
 * Returns the wrap mode for the 't' coordinate of texture lookups on this
 * layer.
 *
 * Return value: the wrap mode for the 't' coordinate of texture lookups on
 * this layer.
 *
 * Stability: unstable
 */
cg_pipeline_wrap_mode_t
cg_pipeline_get_layer_wrap_mode_t(cg_pipeline_t *pipeline, int layer_index);

/**
 * cg_pipeline_set_layer_wrap_mode_t:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to change.
 * @mode: the new wrap mode
 *
 * Sets the wrap mode for the 't' coordinate of texture lookups on this layer.
 *
 * Stability: unstable
 */
void cg_pipeline_set_layer_wrap_mode_t(cg_pipeline_t *pipeline,
                                       int layer_index,
                                       cg_pipeline_wrap_mode_t mode);

/**
 * cg_pipeline_get_layer_wrap_mode_p:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to change.
 *
 * Returns the wrap mode for the 'p' coordinate of texture lookups on this
 * layer.
 *
 * Return value: the wrap mode for the 'p' coordinate of texture lookups on
 * this layer.
 *
 * Stability: unstable
 */
cg_pipeline_wrap_mode_t
cg_pipeline_get_layer_wrap_mode_p(cg_pipeline_t *pipeline, int layer_index);

/**
 * cg_pipeline_set_layer_wrap_mode_p:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to change.
 * @mode: the new wrap mode
 *
 * Sets the wrap mode for the 'p' coordinate of texture lookups on
 * this layer. 'p' is the third coordinate.
 *
 * Stability: unstable
 */
void cg_pipeline_set_layer_wrap_mode_p(cg_pipeline_t *pipeline,
                                       int layer_index,
                                       cg_pipeline_wrap_mode_t mode);

/**
 * cg_pipeline_set_layer_wrap_mode:
 * @pipeline: A #cg_pipeline_t object
 * @layer_index: the layer number to change.
 * @mode: the new wrap mode
 *
 * Sets the wrap mode for all three coordinates of texture lookups on
 * this layer. This is equivalent to calling
 * cg_pipeline_set_layer_wrap_mode_s(),
 * cg_pipeline_set_layer_wrap_mode_t() and
 * cg_pipeline_set_layer_wrap_mode_p() separately.
 *
 * Stability: unstable
 */
void cg_pipeline_set_layer_wrap_mode(cg_pipeline_t *pipeline,
                                     int layer_index,
                                     cg_pipeline_wrap_mode_t mode);

/**
 * cg_pipeline_add_layer_snippet:
 * @pipeline: A #cg_pipeline_t
 * @layer: The layer to hook the snippet to
 * @snippet: A #cg_snippet_t
 *
 * Adds a shader snippet that will hook on to the given layer of the
 * pipeline. The exact part of the pipeline that the snippet wraps
 * around depends on the hook that is given to
 * cg_snippet_new(). Note that some hooks can't be used with a layer
 * and need to be added with cg_pipeline_add_snippet() instead.
 *
 * Stability: Unstable
 */
void cg_pipeline_add_layer_snippet(cg_pipeline_t *pipeline,
                                   int layer,
                                   cg_snippet_t *snippet);

CG_END_DECLS

#endif /* __CG_PIPELINE_LAYER_STATE_H__ */
