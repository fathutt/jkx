// this include must remain at the top of every CPP file
// Terrain. The clipmap owns the height field and the renderer draws it, and
// single-player has no clipmap side at all - no cm_landscape, nothing behind
// TAG_CM_TERRAIN but the tag itself. So this is not a single-player feature
// that is missing here; it is a multiplayer one with nothing to talk to, and
// the whole file is left out rather than stubbed.

#include "tr_local.h"

#if	_DEBUG
#endif
#ifdef _DEBUG
#endif
#if	_DEBUG
#endif
