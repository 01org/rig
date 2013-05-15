#ifndef _RIG_SPLIT_VIEW_H_
#define _RIG_SPLIT_VIEW_H_

#include <cogl/cogl.h>

#include "rut.h"

extern RutType rig_split_view_type;
typedef struct _RigSplitView RigSplitView;

typedef enum _RigSplitViewSplit
{
  RIG_SPLIT_VIEW_SPLIT_VERTICAL,
  RIG_SPLIT_VIEW_SPLIT_HORIZONTAL
} RigSplitViewSplit;

RigSplitView *
rig_split_view_new (RigEngine *engine,
                    RigSplitViewSplit split,
                    float width,
                    float height);

void
rig_split_view_set_size (RutObject *split_view,
                         float width,
                         float height);
void
rig_split_view_set_width (RutObject *split_view,
                          float width);

void
rig_split_view_set_height (RutObject *split_view,
                           float height);

void
rig_split_view_get_size (RutObject *split_view,
                         float *width,
                         float *height);

void
rig_split_view_set_split_fraction (RigSplitView *split_view,
                                   float fraction);

void
rig_split_view_set_child0 (RigSplitView *split_view,
                           RutObject *child0);

void
rig_split_view_set_child1 (RigSplitView *split_view,
                           RutObject *child1);

#endif /* _RIG_SPLIT_VIEW_H_ */
