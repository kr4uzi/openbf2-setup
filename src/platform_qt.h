#pragma once

#include <QString>

// The few things the Qt front end does differently per platform. Everything else in window_qt.cpp,
// client_qt.cpp and microphone_sdl.cpp is shared. Implemented in platform_mac.mm (macOS) and
// platform_linux.cpp (Linux).
namespace setup::frontend {
	// the user asked for the launcher page instead of the direct start: the Option key held while the
	// application opened (macOS), or --launcher on the command line (Linux, also the desktop action).
	// The first call latches the answer; main calls it before anything else
	bool launcherRequested();
	// the hint next to "Open launcher on startup"
	QString launcherHint();
	// the label of the microphone button when access was denied
	QString microphoneSettingsLabel();
	// hands the foreground to the started client. Called repeatedly after the launch until it returns
	// true: the client is active, it is gone, or the user brought another application forward
	bool handOverActivation(long long processId);
}
