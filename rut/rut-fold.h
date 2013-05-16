/*
 * Rut
 *
 * Copyright (C) 2013  Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _RUT_FOLD_H_
#define _RUT_FOLD_H_

#include "rut.h"

extern RutType rut_fold_type;

typedef struct _RutFold RutFold;

#define RUT_FOLD(x) ((RutFold *) x)

RutFold *
rut_fold_new (RutContext *ctx,
              const char *label);

void
rut_fold_set_child (RutFold *fold, RutObject *child);

void
rut_fold_set_folded (RutFold *fold, CoglBool folded);

void
rut_fold_set_folder_color (RutFold *fold, const CoglColor *color);

void
rut_fold_set_label_color (RutFold *fold, const CoglColor *color);

void
rut_fold_set_font_name (RutFold *fold, const char *font);

#endif /* _RUT_FOLD_H_ */
