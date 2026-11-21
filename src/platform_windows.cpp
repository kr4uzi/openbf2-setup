#include "platform_qt.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

bool setup::frontend::launcherRequested()
{
	// read once, when the application starts: the verification which follows may take a while
	static const bool held = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	return held;
}

QString setup::frontend::launcherHint()
{
	return "Tip: Open Launcher by pressing Shift on startup";
}

QString setup::frontend::microphoneSettingsLabel()
{
	return "Open privacy settings";
}

// Windows lets the foreground go to a process the current foreground application allows
bool setup::frontend::handOverActivation(long long processId)
{
	AllowSetForegroundWindow(DWORD(processId));
	return true;
}
