#include "platform_qt.h"

#include <QCoreApplication>
#include <QStringList>

bool setup::frontend::launcherRequested()
{
	return QCoreApplication::arguments().contains("--launcher");
}

QString setup::frontend::launcherHint()
{
	return "Tip: Open with --launcher to return here";
}

QString setup::frontend::microphoneSettingsLabel()
{
	return "Open audio settings";
}

// the desktop's window manager decides about the foreground; nothing to hand over
bool setup::frontend::handOverActivation(long long)
{
	return true;
}
