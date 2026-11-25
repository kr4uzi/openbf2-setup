#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

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

// whether the process has a normal window (layer 0) on screen. Owner and layer need no screen recording
// permission, only window titles would
static bool hasWindowOnScreen(pid_t processId)
{
	CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
	if (windows == nullptr)
		return false;

	bool found = false;
	for (NSDictionary* window in (__bridge NSArray*)windows) {
		if ([window[(__bridge NSString*)kCGWindowOwnerPID] intValue] == processId && [window[(__bridge NSString*)kCGWindowLayer] intValue] == 0) {
			found = true;
			break;
		}
	}
	CFRelease(windows);
	return found;
}

// activation is cooperative since macOS 14: the game gets the front from the application which started
// it, so its first clicks count. the child turns up as a running application a moment after the launch.
// It becomes active long before its window exists and can lose that again until the window is ordered
// in; AppKit then grants its own request only while this application is frontmost and still yields to
// it. So the hand-over goes on until the game is active with its window on screen
bool setup::frontend::handOverActivation(long long processId)
{
	NSRunningApplication* game = [NSRunningApplication runningApplicationWithProcessIdentifier:pid_t(processId)];
	if (game == nil)
		return false;

	if (game.terminated || (game.active && hasWindowOnScreen(pid_t(processId))))
		return true;

	// the user went to another application meanwhile: their choice
	NSRunningApplication* front = NSWorkspace.sharedWorkspace.frontmostApplication;
	if (front && front.processIdentifier != NSProcessInfo.processInfo.processIdentifier && front.processIdentifier != pid_t(processId))
		return true;

	if (@available(macOS 14.0, *))
		[NSApp yieldActivationToApplication:game];

	if (!game.active)
		[game activateWithOptions:0];
	return false;
}
