//------------------------------------------------------------------------
//  SURFACE TEXTURE TRANSFORM DIALOG
//------------------------------------------------------------------------

#ifndef UI_SURFACE_TRANSFORM_H_
#define UI_SURFACE_TRANSFORM_H_

class Instance;
class SString;

// Explicit object/part values let property-panel buttons target the surface
// they represent without temporarily rewriting the editor selection.
void UI_RunSurfaceTransform(
		Instance &instance, int objectOverride = -1, int partsOverride = 0);

// Used by UI regression coverage to exercise the actual responsive widget
// geometry without showing or running the modal tool.
bool UI_VerifySurfaceTransformLayout(Instance &instance,
		int width, int height, SString *reason = nullptr);

// Regression helper for the WM contract: this tool may be a transient, but
// never a modal or embedded window.  In particular, GNOME must not attach it
// to and resize the main editor while it is dragged.
bool UI_VerifySurfaceTransformWindowPolicy(
		Instance &instance, SString *reason = nullptr);

#endif
