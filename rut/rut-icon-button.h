/*
 * Rut.
 *
 * Copyright (C) 2013  Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __RUT_ICON_BUTTON_H__
#define __RUT_ICON_BUTTON_H__

typedef struct _RutIconButton RutIconButton;
extern RutType rut_icon_button_type;

typedef enum
{
  RUT_ICON_BUTTON_POSITION_ABOVE,
  RUT_ICON_BUTTON_POSITION_BELOW,
  RUT_ICON_BUTTON_POSITION_SIDE,
} RutIconButtonPosition;

RutIconButton *
rut_icon_button_new (RutContext *ctx,
                     const char *label,
                     RutIconButtonPosition label_position,
                     const char *normal_icon,
                     const char *hover_icon,
                     const char *active_icon,
                     const char *disable_icon);

typedef void (*RutIconButtonClickCallback) (RutIconButton *button, void *user_data);

RutClosure *
rut_icon_button_add_on_click_callback (RutIconButton *button,
                                  RutIconButtonClickCallback callback,
                                  void *user_data,
                                  RutClosureDestroyCallback destroy_cb);

void
rut_icon_button_set_size (RutObject *self,
                          float width,
                          float height);

void
rut_icon_button_get_size (RutObject *self,
                          float *width,
                          float *height);

void
rut_icon_button_set_normal (RutIconButton *button,
                            const char *icon);

void
rut_icon_button_set_hover (RutIconButton *button,
                           const char *icon);

void
rut_icon_button_set_active (RutIconButton *button,
                            const char *icon);

void
rut_icon_button_set_disabled (RutIconButton *button,
                              const char *icon);

void
rut_icon_button_set_label_color (RutIconButton *button,
                                 const CoglColor *color);

#endif /* __RUT_ICON_BUTTON_H__ */
