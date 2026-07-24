//------------------------------------------------------------------------
//  MATHEMATICAL GRID AND SNAPPING DIALOG
//------------------------------------------------------------------------

#ifndef UI_GRID_H_
#define UI_GRID_H_

class Instance;
class SString;

void UI_RunMathematicalGrid(Instance &instance);
bool UI_VerifyMathematicalGridLayout(
		Instance &instance, SString *reason = nullptr);
bool UI_VerifyMathematicalGridWindowPolicy(
		Instance &instance, SString *reason = nullptr);

#endif
