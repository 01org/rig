#ifndef _RUT_H_
#define _RUT_H_

#include <cogl/cogl.h>

#include <glib.h>

#ifndef __ANDROID__
#define USE_SDL
//#define USE_GLIB
#endif

#include "rut-global.h"
#include "rut-type.h"
#include "rut-object.h"
#include "rut-property.h"
#include "rut-interfaces.h"
#include "rut-context.h"
#include "rut-shell.h"
#include "rut-bitmask.h"
#include "rut-memory-stack.h"
#include "rut-timeline.h"
#include "rut-display-list.h"
#include "rut-arcball.h"
#include "rut-util.h"
#include "rut-text-buffer.h"
#include "rut-text.h"
#include "rut-tool.h"
#include "rut-geometry.h"
#include "rut-paintable.h"
#include "rut-color.h"
#include "rut-asset.h"
#include "rut-bevel.h"
#include "rut-split-view.h"
#include "rut-stack.h"
#include "rut-entry.h"
#include "rut-downsampler.h"
#include "rut-gaussian-blurrer.h"
#include "rut-toggle.h"
#include "rut-dof-effect.h"
#include "rut-inspector.h"
#include "rut-mesh.h"

/* entity/components system */
#include "rut-entity.h"
#include "rut-components.h"

#endif /* _RUT_H_ */
