#ifndef _RUT_ASSET_H_
#define _RUT_ASSET_H_

#include <stdbool.h>
#include <gio/gio.h>
#include <cogl-gst/cogl-gst.h>

typedef enum _RutAssetType {
  RUT_ASSET_TYPE_BUILTIN,
  RUT_ASSET_TYPE_TEXTURE,
  RUT_ASSET_TYPE_NORMAL_MAP,
  RUT_ASSET_TYPE_ALPHA_MASK,
  RUT_ASSET_TYPE_PLY_MODEL,
} RutAssetType;

extern RutType rut_asset_type;
typedef struct _RutAsset RutAsset;
#define RUT_ASSET(X) ((RutAsset *)X)

void _rut_asset_type_init (void);

CoglBool
rut_file_info_is_asset (GFileInfo *info, const char *name);

RutAsset *
rut_asset_new_builtin (RutContext *ctx,
                       const char *icon_path);

RutAsset *
rut_asset_new_texture (RutContext *ctx,
                       const char *path,
                       const GList *inferred_tags);

RutAsset *
rut_asset_new_normal_map (RutContext *ctx,
                          const char *path,
                          const GList *inferred_tags);

RutAsset *
rut_asset_new_alpha_mask (RutContext *ctx,
                          const char *path,
                          const GList *inferred_tags);

RutAsset *
rut_asset_new_ply_model (RutContext *ctx,
                         const char *path,
                         const GList *inferred_tags);

RutAsset *
rut_asset_new_from_data (RutContext *ctx,
                         const char *path,
                         RutAssetType type,
                         uint8_t *data,
                         size_t len);

RutAssetType
rut_asset_get_type (RutAsset *asset);

const char *
rut_asset_get_path (RutAsset *asset);

RutContext *
rut_asset_get_context (RutAsset *asset);

CoglTexture *
rut_asset_get_texture (RutAsset *asset);

RutMesh *
rut_asset_get_mesh (RutAsset *asset);

RutObject *
rut_asset_get_model (RutAsset *asset);

CoglBool
rut_asset_get_is_video (RutAsset *asset);

void
rut_asset_set_inferred_tags (RutAsset *asset,
                             const GList *inferred_tags);

const GList *
rut_asset_get_inferred_tags (RutAsset *asset);

CoglBool
rut_asset_has_tag (RutAsset *asset, const char *tag);

GList *
rut_infer_asset_tags (RutContext *ctx, GFileInfo *info, GFile *asset_file);

void
rut_asset_add_inferred_tag (RutAsset *asset, const char *tag);

bool
rut_asset_needs_thumbnail (RutAsset *asset);

typedef void (*RutThumbnailCallback) (RutAsset *asset, void *user_data);

RutClosure *
rut_asset_thumbnail (RutAsset *asset,
                     RutThumbnailCallback ready_callback,
                     void *user_data,
                     RutClosureDestroyCallback destroy_cb);

#endif /* _RUT_ASSET_H_ */
