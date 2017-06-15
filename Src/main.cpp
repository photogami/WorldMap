#include "Precompiled.h"
#include "../../../Source/Modules/Terrain/TerrainMisc.h"
//-----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	int w, h;
	E_GetMonitorResolution(w, h);
	w *= 0.95f;
	h *= 0.95f;
	
	if (!Engine_Init(NULL, "World Map", "1.0", w, h, "", "sandbox_worldmap/"))
		return 0;

	gConsole.ExecuteFile("content_simview2/engine.cfg");

	while (Engine_DoFrame()) {}

	Engine_Deinit();

	return 0;
}