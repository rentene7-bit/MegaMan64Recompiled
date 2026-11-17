#ifndef REMASTER_FLAGS_H
#define REMASTER_FLAGS_H

/* Bitmask returned by remaster_patch_flags(). The safe remaster behavior is
 * always set. Draw distance and cutscene skip are included only while their
 * experimental settings are enabled. */

#define REMASTER_FLAG_SEAMS (1u << 0)
#define REMASTER_FLAG_WIDESCREEN (1u << 1)
#define REMASTER_FLAG_CUTSCENE_SYNC (1u << 2)
#define REMASTER_FLAG_PARTICLES (1u << 3)
#define REMASTER_FLAG_LOADING_FADES (1u << 4)
#define REMASTER_FLAG_TEXTURE_FILTER (1u << 5)
#define REMASTER_FLAG_INTERPOLATION (1u << 6)
#define REMASTER_FLAG_DRAW_DISTANCE (1u << 7)
#define REMASTER_FLAG_CUTSCENE_SKIP (1u << 8)

#endif
