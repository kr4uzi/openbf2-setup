#import <AppKit/AppKit.h>

#include "platform_qt.h"

bool setup::frontend::launcherRequested()
{
	// read once, when the application starts: the verification which follows may take a while
	static const bool held = (NSEvent.modifierFlags & NSEventModifierFlagOption) != 0;
	return held;
}

QString setup::frontend::launcherHint()
{
	return QString::fromUtf8("Tip: Open Launcher by pressing ⌥ on startup");
}

QString setup::frontend::microphoneSettingsLabel()
{
	return "Open System Settings";
}

// activation is cooperative since macOS 14: the game gets the front from the application which started
// it, so its first clicks count. the child turns up as a running application a moment after the launch
bool setup::frontend::handOverActivation(long long processId)
{
	NSRunningApplication* game = [NSRunningApplication runningApplicationWithProcessIdentifier:pid_t(processId)];
	if (game == nil)
		return false;

	if (game.active || game.terminated)
		return true;

	// the user went to another application meanwhile: their choice
	NSRunningApplication* front = NSWorkspace.sharedWorkspace.frontmostApplication;
	if (front && front.processIdentifier != NSProcessInfo.processInfo.processIdentifier && front.processIdentifier != pid_t(processId))
		return true;

	if (@available(macOS 14.0, *))
		[NSApp yieldActivationToApplication:game];

	[game activateWithOptions:0];
	return false;
}
