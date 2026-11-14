#import <AppKit/AppKit.h>

#include "window.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/use_awaitable.hpp>
using namespace setup;

namespace {
	// SETUP_DEBUG=1 traces what the user did and what the window showed. keys are never part of it
	void debug(NSString* format, ...) NS_FORMAT_FUNCTION(1, 2);
	void debug(NSString* format, ...)
	{
		auto enabled = std::getenv("SETUP_DEBUG");
		if (!enabled || std::strcmp(enabled, "1"))
			return;

		va_list arguments;
		va_start(arguments, format);
		NSString* line = [[NSString alloc] initWithFormat:format arguments:arguments];
		va_end(arguments);
		std::fprintf(stderr, "[setup] %s\n", line.UTF8String);
	}

	NSString* text(const std::string& s)
	{
		return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"Unable to display this message";
	}

	// NSString rejects fileSystemRepresentation on an empty string with an Objective-C exception,
	// which a C++ handler cannot catch
	std::filesystem::path path(NSString* s)
	{
		return s.length ? std::filesystem::path{ s.fileSystemRepresentation } : std::filesystem::path{};
	}

	NSString* const titles[] = { @"Welcome to Battlefield 2", @"Choose your disc", @"Choose the two patches", @"Enter your CD key", @"Installing Battlefield 2", @"Set up voice chat", @"Ready to play" };
	NSString* const next_titles[] = { @"Continue", @"Continue", @"Continue", @"Install", @"Cancel", @"Save", @"Play" };
}

// a box which takes a file: dropped on it or chosen in a panel
@interface FileWell : NSView
@property(nonatomic, copy) NSString* path;
@property(nonatomic) BOOL enabled;
@property(copy) void (^chosen)(NSString* path);
@property(strong) NSTextField* label;
@property(strong) NSButton* choose;
@property(strong) NSProgressIndicator* checking;
- (instancetype)initWithTitle:(NSString*)title directories:(BOOL)directories;
@end

@implementation FileWell {
	BOOL _directories;
}

- (instancetype)initWithTitle:(NSString*)title directories:(BOOL)directories
{
	if ((self = [super initWithFrame:NSMakeRect(0, 0, 560, 92)])) {
		_directories = directories;
		_enabled = YES;
		self.wantsLayer = YES;
		self.layer.cornerRadius = 8;
		self.layer.borderWidth = 1;
		self.layer.borderColor = NSColor.separatorColor.CGColor;
		self.accessibilityLabel = title;

		NSTextField* heading = [NSTextField labelWithString:title];
		heading.frame = NSMakeRect(16, 60, 420, 22);
		[self addSubview:heading];

		_label = [NSTextField labelWithString:@"Drop a file here or choose…"];
		_label.frame = NSMakeRect(16, 16, 425, 36);
		_label.lineBreakMode = NSLineBreakByTruncatingMiddle;
		[self addSubview:_label];

		_choose = [NSButton buttonWithTitle:@"Choose…" target:self action:@selector(choose:)];
		_choose.frame = NSMakeRect(450, 27, 95, 32);
		_choose.accessibilityLabel = [@"Choose " stringByAppendingString:title];
		[self addSubview:_choose];

		_checking = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(16, 6, 528, 5)];
		_checking.indeterminate = NO;
		_checking.maxValue = 1;
		_checking.hidden = YES;
		[self addSubview:_checking];

		[self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
	}

	return self;
}

- (void)setPath:(NSString*)path
{
	_path = [path copy];
	debug(@"chosen well=%@ path=%@", self.accessibilityLabel, path);
	self.label.stringValue = path ?: @"Drop a file here or choose…";
	self.checking.hidden = YES;
	if (path && self.chosen)
		self.chosen(path);
}

- (void)setEnabled:(BOOL)enabled
{
	_enabled = enabled;
	self.choose.enabled = enabled;
}

- (void)choose:(id)sender
{
	NSOpenPanel* panel = NSOpenPanel.openPanel;
	panel.canChooseDirectories = _directories;
	panel.canChooseFiles = YES;
	panel.allowsMultipleSelection = NO;
	[panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
		if (response == NSModalResponseOK && self.enabled)
			self.path = panel.URL.path;
	}];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
	return self.enabled ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
	NSArray<NSURL*>* urls = [sender.draggingPasteboard readObjectsForClasses:@[ NSURL.class ] options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
	if (!self.enabled || urls.count != 1)
		return NO;

	self.path = urls.firstObject.path;
	return YES;
}
@end

// a 0-1 peak meter with the activation threshold drawn as a marker, the same scale retail's BF2VoiceSetup
// shows against its threshold
@interface LevelMeter : NSView
@property(nonatomic) double level;
@property(nonatomic) double threshold;
@end

@implementation LevelMeter
- (void)setLevel:(double)level
{
	_level = fmin(1.0, fmax(0.0, level));
	self.needsDisplay = YES;
}

- (void)setThreshold:(double)threshold
{
	_threshold = fmin(1.0, fmax(0.0, threshold));
	self.needsDisplay = YES;
}

- (BOOL)isAccessibilityElement
{
	return YES;
}

- (NSAccessibilityRole)accessibilityRole
{
	return NSAccessibilityLevelIndicatorRole;
}

- (id)accessibilityValue
{
	return [NSString stringWithFormat:@"%.0f%%", self.level * 100];
}

- (void)drawRect:(NSRect)dirty
{
	NSRect bounds = NSInsetRect(self.bounds, 0.5, 0.5);
	NSBezierPath* frame = [NSBezierPath bezierPathWithRoundedRect:bounds xRadius:4 yRadius:4];
	[NSColor.quaternaryLabelColor setFill];
	[frame fill];

	NSRect fill = bounds;
	fill.size.width = bounds.size.width * self.level;
	[(self.level >= self.threshold && self.level > 0 ? NSColor.systemGreenColor : NSColor.systemGrayColor) setFill];
	[[NSBezierPath bezierPathWithRoundedRect:fill xRadius:4 yRadius:4] fill];

	CGFloat x = bounds.origin.x + bounds.size.width * self.threshold;
	NSBezierPath* marker = [NSBezierPath bezierPath];
	marker.lineWidth = 2;
	[marker moveToPoint:NSMakePoint(x, bounds.origin.y)];
	[marker lineToPoint:NSMakePoint(x, NSMaxY(bounds))];
	[NSColor.labelColor setStroke];
	[marker stroke];

	[NSColor.separatorColor setStroke];
	[frame stroke];
}
@end

@class WindowController;

// one of the five groups of a key row. index 0-4 is Battlefield 2, 5-9 Special Forces
@interface KeyField : NSTextField
@property(weak) WindowController* controller;
@property NSInteger index;
@end

// everything AppKit: the window, its controls and what they look like on each page. it decides nothing,
// whatever the user does is handed to `emit`
@interface WindowController : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTextFieldDelegate>
@property(copy) void (^emit)(Event::Kind kind, NSString* path);
@property BOOL closing;
@property Page page;
@property(strong) NSWindow* window;
@property(strong) NSView* body;
@property(strong) NSButton* next;
@property(strong) NSButton* back;
@property(strong) NSButton* voiceSetup;
@property(strong) NSButton* modsFolder;
@property(strong) NSPopUpButton* mods;
@property(copy) NSString* selectedMod;
@property(strong) FileWell* disc;
@property(strong) FileWell* patch141;
@property(strong) FileWell* patch150;
@property(strong) NSArray<KeyField*>* keys;
@property(strong) NSArray<NSTextField*>* keyStatuses;
// Deluxe media only: install the expansion (default) or the base game alone
@property(strong) NSButton* specialForces;
@property BOOL deluxe;
@property(strong) NSProgressIndicator* progress;
@property(strong) NSTextField* status;
@property BOOL cancelling;
@property(strong) NSSlider* captureVolume;
@property(strong) NSSlider* captureThreshold;
@property(strong) NSButton* boost;
@property(strong) LevelMeter* meter;
@property(strong) NSTextField* micStatus;
@property(strong) NSButton* micButton;
// retail has no slider for it: carried through unchanged
@property double playbackVolume;
- (void)pasteKey:(NSString*)value row:(NSInteger)row;
@end

@implementation KeyField
- (void)paste:(id)sender
{
	[self.controller pasteKey:[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] ?: @"" row:self.index / 5];
}
@end

@implementation WindowController {
	// the input checks of the installer run side by side and report one item each: a line per item
	NSMutableArray<NSString*>* _itemNames;
	NSMutableDictionary<NSString*, NSString*>* _itemLines;
	NSString* _phase;
}

- (instancetype)init
{
	if (!(self = [super init]))
		return nil;

	_itemNames = [NSMutableArray array];
	_itemLines = [NSMutableDictionary dictionary];
	_playbackVolume = 1;

	self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 660, 480) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
	self.window.title = @"OpenBF2";
	self.window.delegate = self;
	self.window.releasedWhenClosed = NO;
	[self.window center];

	__weak WindowController* weakSelf = self;
	self.disc = [[FileWell alloc] initWithTitle:@"Battlefield 2 DVD or ISO" directories:YES];
	self.disc.chosen = ^(NSString* path) { [weakSelf send:Event::Kind::disc_chosen path:path]; };
	self.patch141 = [[FileWell alloc] initWithTitle:@"Patch 1.0 → 1.41" directories:NO];
	self.patch141.chosen = ^(NSString* path) { [weakSelf send:Event::Kind::patch141_chosen path:path]; };
	self.patch150 = [[FileWell alloc] initWithTitle:@"Patch 1.41 → 1.50" directories:NO];
	self.patch150.chosen = ^(NSString* path) { [weakSelf send:Event::Kind::patch150_chosen path:path]; };

	self.specialForces = [NSButton checkboxWithTitle:@"Install Special Forces" target:self action:@selector(specialForcesChanged:)];
	self.specialForces.frame = NSMakeRect(20, 100, 300, 22);
	self.specialForces.state = NSControlStateValueOn;

	NSMutableArray<KeyField*>* keys = [NSMutableArray array];
	for (NSInteger i = 0; i < 10; ++i) {
		KeyField* field = [[KeyField alloc] initWithFrame:NSMakeRect(20 + (i % 5) * 106, i < 5 ? 154 : 64, 92, 32)];
		field.controller = self;
		field.index = i;
		field.delegate = self;
		field.font = [NSFont monospacedSystemFontOfSize:20 weight:NSFontWeightRegular];
		field.accessibilityLabel = [NSString stringWithFormat:@"%@ CD key group %ld of 5", i < 5 ? @"Battlefield 2" : @"Special Forces", (long)(i % 5) + 1];
		[keys addObject:field];
	}
	self.keys = keys;

	self.captureVolume = [NSSlider sliderWithValue:1 minValue:0 maxValue:1 target:self action:@selector(voiceChanged:)];
	self.captureVolume.frame = NSMakeRect(170, 118, 320, 24);
	self.captureVolume.continuous = YES;
	self.captureVolume.accessibilityLabel = @"Capture volume";
	self.captureThreshold = [NSSlider sliderWithValue:0.1 minValue:0 maxValue:1 target:self action:@selector(voiceChanged:)];
	self.captureThreshold.frame = NSMakeRect(170, 82, 320, 24);
	self.captureThreshold.continuous = YES;
	self.captureThreshold.accessibilityLabel = @"Activation threshold";
	self.boost = [NSButton checkboxWithTitle:@"Microphone boost" target:self action:@selector(voiceChanged:)];
	self.boost.frame = NSMakeRect(20, 46, 300, 22);
	self.meter = [[LevelMeter alloc] initWithFrame:NSMakeRect(20, 158, 560, 20)];
	self.meter.threshold = 0.1;
	self.meter.accessibilityLabel = @"Microphone level";

	self.next = [NSButton buttonWithTitle:@"Continue" target:self action:@selector(next:)];
	self.next.frame = NSMakeRect(510, 22, 115, 34);
	self.next.keyEquivalent = @"\r";
	self.back = [NSButton buttonWithTitle:@"Back" target:self action:@selector(back:)];
	self.back.frame = NSMakeRect(34, 22, 90, 34);
	[self.window.contentView addSubview:self.next];
	[self.window.contentView addSubview:self.back];

	NSMenu* menu = [NSMenu new];
	NSMenuItem* application = [NSMenuItem new];
	[menu addItem:application];
	NSMenu* applicationMenu = [NSMenu new];
	[applicationMenu addItemWithTitle:@"Quit OpenBF2" action:@selector(terminate:) keyEquivalent:@"q"];
	application.submenu = applicationMenu;

	NSMenuItem* edit = [NSMenuItem new];
	edit.title = @"Edit";
	[menu addItem:edit];
	NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
	[editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
	[editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
	[editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
	[editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
	edit.submenu = editMenu;
	NSApp.mainMenu = menu;

	return self;
}

- (void)send:(Event::Kind)kind path:(NSString*)path
{
	debug(@"event kind=%d page=%d", int(kind), int(self.page));
	if (self.emit)
		self.emit(kind, path);
}

- (void)next:(id)sender
{
	[self send:Event::Kind::next path:nil];
}

- (void)back:(id)sender
{
	[self send:Event::Kind::back path:nil];
}

- (void)voiceSetup:(id)sender
{
	[self send:Event::Kind::voice_setup path:nil];
}

- (void)modsFolder:(id)sender
{
	[self send:Event::Kind::mods_folder path:nil];
}

// the user may have been in the Finder, putting a mod into the library
- (void)applicationDidBecomeActive:(NSNotification*)notification
{
	[self send:Event::Kind::activated path:nil];
}

- (void)micButton:(id)sender
{
	[self send:Event::Kind::microphone path:nil];
}

// the window stays until the application says otherwise: it may have to stop an installation first,
// or refuse because the game runs
- (BOOL)windowShouldClose:(NSWindow*)sender
{
	[self send:Event::Kind::close path:nil];
	return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
	if (self.closing)
		return NSTerminateNow;

	[self send:Event::Kind::close path:nil];
	return NSTerminateCancel;
}

- (NSTextField*)label:(NSString*)string y:(CGFloat)y height:(CGFloat)height size:(CGFloat)size
{
	NSTextField* label = [NSTextField wrappingLabelWithString:string];
	label.frame = NSMakeRect(20, y, 560, height);
	label.font = [NSFont systemFontOfSize:size];
	[self.body addSubview:label];
	return label;
}

// an empty page with its title and its buttons. the pages add their content below
- (void)begin:(Page)page back:(BOOL)back nextEnabled:(BOOL)nextEnabled
{
	debug(@"page %d", int(page));
	self.page = page;
	[self.body removeFromSuperview];
	self.body = [[NSView alloc] initWithFrame:NSMakeRect(30, 76, 600, 370)];
	[self.window.contentView addSubview:self.body];
	[self label:titles[int(page)] y:280 height:76 size:27];

	self.next.title = next_titles[int(page)];
	self.next.enabled = nextEnabled;
	self.back.hidden = !back;
	self.back.enabled = YES;
	for (FileWell* well in @[ self.disc, self.patch141, self.patch150 ])
		well.enabled = YES;

	self.meter.level = 0;
}

- (void)showWelcome
{
	[self begin:Page::welcome back:NO nextEnabled:YES];
	[self label:@"Install from your Battlefield 2 DVD or ISO and the official 1.41 and 1.50 patches. Game data, your key and profiles are stored in your Library’s Application Support/OpenBF2 folder." y:155 height:76 size:16];
}

- (void)showDisc
{
	[self begin:Page::disc back:YES nextEnabled:YES];
	self.disc.frame = NSMakeRect(20, 150, 560, 92);
	[self.body addSubview:self.disc];
	[self label:@"Choose an English BF2 or BF2 Deluxe DVD or ISO. Deluxe includes Special Forces. Your original files are read only." y:50 height:76 size:14];
}

- (void)showPatches
{
	[self begin:Page::patches back:YES nextEnabled:NO];
	self.patch141.frame = NSMakeRect(20, 165, 560, 92);
	self.patch150.frame = NSMakeRect(20, 60, 560, 92);
	[self.body addSubview:self.patch141];
	[self.body addSubview:self.patch150];
}

- (void)setPatch:(PatchSlot)slot status:(PatchStatus)status fraction:(double)fraction
{
	FileWell* well = slot == PatchSlot::Patch141 ? self.patch141 : self.patch150;
	NSString* chosen = well.path ?: @"";
	well.checking.hidden = status != PatchStatus::checking;
	well.checking.doubleValue = fraction;
	if (status == PatchStatus::checking)
		well.label.stringValue = [NSString stringWithFormat:@"%@\nChecking SHA-256: %.0f%%", chosen, fraction * 100];
	else
		well.label.stringValue = [chosen stringByAppendingString:status == PatchStatus::verified ? @"\nVerified official patch" : @"\nPatch verification failed"];
}

- (void)showKeys:(BOOL)deluxe back:(BOOL)back
{
	[self begin:Page::keys back:back nextEnabled:NO];
	self.deluxe = deluxe;
	[self label:@"Enter the key printed with your game. You can paste the whole key." y:205 height:76 size:16];
	[self label:@"Battlefield 2" y:190 height:22 size:14];
	if (deluxe)
		[self.body addSubview:self.specialForces];

	NSMutableArray<NSTextField*>* statuses = [NSMutableArray array];
	for (NSInteger row = 0; row < (deluxe ? 2 : 1); ++row)
		[statuses addObject:[self label:@"" y:row == 0 ? 126 : 36 height:22 size:13]];
	self.keyStatuses = statuses;

	for (KeyField* field in self.keys) {
		if (field.index >= 5 && !deluxe)
			continue;

		field.enabled = field.index < 5 || self.specialForces.state == NSControlStateValueOn;
		[self.body addSubview:field];
	}

	[self.window makeFirstResponder:self.keys.firstObject];
}

- (NSString*)keyForRow:(NSInteger)row
{
	NSMutableString* key = [NSMutableString string];
	for (NSInteger i = row * 5; i < row * 5 + 5; ++i)
		[key appendString:self.keys[i].stringValue];

	return key;
}

- (void)clearKeys
{
	for (KeyField* field in self.keys)
		field.stringValue = @"";
}

- (void)setKeyStatus:(KeyStatus)status row:(NSInteger)row
{
	if (row >= (NSInteger)self.keyStatuses.count)
		return;

	NSTextField* label = self.keyStatuses[row];
	NSString* product = row == 0 ? @"Battlefield 2" : @"Special Forces";
	switch (status) {
	case KeyStatus::valid:
		label.stringValue = @"Valid key — local check passed";
		label.textColor = NSColor.systemGreenColor;
		break;
	case KeyStatus::invalid:
		label.stringValue = @"Invalid key — check the characters and product";
		label.textColor = NSColor.systemRedColor;
		break;
	case KeyStatus::incomplete:
		label.stringValue = @"Enter all five groups";
		label.textColor = NSColor.secondaryLabelColor;
		break;
	case KeyStatus::not_installed:
		label.stringValue = @"Special Forces will not be installed";
		label.textColor = NSColor.secondaryLabelColor;
		break;
	}

	label.accessibilityLabel = status == KeyStatus::not_installed ? @"Special Forces: not installed" : [NSString stringWithFormat:@"%@: %@", product, label.stringValue];
}

- (void)specialForcesChanged:(id)sender
{
	for (KeyField* field in self.keys)
		field.enabled = field.index < 5 || self.specialForces.state == NSControlStateValueOn;

	[self send:Event::Kind::keys_edited path:nil];
}

// a whole key in one piece: spread over the five groups of its row
- (void)pasteKey:(NSString*)value row:(NSInteger)row
{
	NSString* compact;
	try {
		compact = ::text(assistant::normalizedKey(std::string{ value.UTF8String ?: "" }));
	}
	catch (const std::exception&) {
		[self message:@"Check the key’s format" detail:@"Paste 20 letters or numbers, optionally in five groups separated by hyphens."];
		return;
	}

	for (NSUInteger i = 0; i < 5; ++i)
		self.keys[row * 5 + i].stringValue = [[compact substringWithRange:NSMakeRange(i * 4, 4)] uppercaseString];

	[self.window makeFirstResponder:self.keys[row * 5 + 4]];
	[self send:Event::Kind::keys_edited path:nil];
}

// the field editor of NSTextField owns the keyboard commands, the delegate routes paste and backspace
- (BOOL)control:(NSControl*)control textView:(NSTextView*)view doCommandBySelector:(SEL)selector
{
	KeyField* field = (KeyField*)control;
	if (selector == @selector(paste:)) {
		NSString* value = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
		if (value.length >= 20) {
			[self pasteKey:value row:field.index / 5];
			return YES;
		}
	}

	if (selector == @selector(deleteBackward:) && field.stringValue.length == 0 && field.index % 5 > 0) {
		[self.window makeFirstResponder:self.keys[field.index - 1]];
		return YES;
	}

	return NO;
}

- (void)controlTextDidChange:(NSNotification*)notification
{
	KeyField* field = notification.object;
	NSString* value = field.stringValue.uppercaseString;
	if (value.length >= 20) {
		[self pasteKey:value row:field.index / 5];
		return;
	}

	NSMutableString* filtered = [NSMutableString string];
	for (NSUInteger i = 0; i < value.length && filtered.length < 4; ++i) {
		unichar c = [value characterAtIndex:i];
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			[filtered appendFormat:@"%C", c];
	}

	field.stringValue = filtered;
	if (filtered.length == 4 && field.index % 5 < 4)
		[self.window makeFirstResponder:self.keys[field.index + 1]];

	[self send:Event::Kind::keys_edited path:nil];
}

- (void)showInstall
{
	[self begin:Page::install back:NO nextEnabled:YES];
	self.cancelling = NO;
	_phase = nil;
	[_itemNames removeAllObjects];
	[_itemLines removeAllObjects];

	self.progress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 187, 560, 18)];
	self.progress.indeterminate = NO;
	self.progress.minValue = 0;
	self.progress.maxValue = 1;
	[self.body addSubview:self.progress];
	self.status = [self label:@"Checking your media…" y:40 height:130 size:14];
}

- (void)showProgress:(const Progress&)progress
{
	if (self.page != Page::install || self.cancelling)
		return;

	NSDictionary<NSString*, NSString*>* names = @{
		@"inputs": @"Checking media",
		@"disc": @"Installing DVD files",
		@"preserveRenderer": @"Preparing patch files",
		@"patch141": @"Applying patch 1.41",
		@"patch141-xp1": @"Patching Special Forces",
		@"overlay150": @"Installing patch 1.50",
		@"verify": @"Verifying game data",
	};
	NSString* phase = ::text(progress.phase);
	NSString* item = ::text(progress.path);
	if (![_phase isEqualToString:phase]) {
		_phase = phase;
		[_itemNames removeAllObjects];
		[_itemLines removeAllObjects];
	}

	NSMutableString* description = [NSMutableString stringWithString:names[phase] ?: @"Installing"];
	if (progress.itemTotalBytes) {
		if (!_itemLines[item])
			[_itemNames addObject:item];

		_itemLines[item] = [NSString stringWithFormat:@"%@: %.0f%% of %.1f MB", item, 100.0 * double(progress.itemCompletedBytes) / double(progress.itemTotalBytes), progress.itemTotalBytes / 1e6];
		for (NSString* name in _itemNames)
			[description appendFormat:@"\n%@", _itemLines[name]];
	}
	else if (item.length)
		[description appendFormat:@" — %@", item];

	NSMutableArray<NSString*>* counts = [NSMutableArray array];
	if (progress.totalFiles)
		[counts addObject:[NSString stringWithFormat:@"%zu of %zu files", progress.completedFiles, progress.totalFiles]];
	if (progress.totalBytes)
		[counts addObject:[NSString stringWithFormat:@"%.1f of %.1f MB", progress.completedBytes / 1e6, progress.totalBytes / 1e6]];
	if (counts.count)
		[description appendFormat:@"\n%@", [counts componentsJoinedByString:@" · "]];

	if (progress.totalBytes)
		self.progress.doubleValue = double(progress.completedBytes) / double(progress.totalBytes);
	else
		self.progress.doubleValue = progress.totalFiles ? double(progress.completedFiles) / double(progress.totalFiles) : 0;

	self.status.stringValue = description;
}

// modelled on retail's BF2VoiceSetup ("BF2 BattleCommo"): level meter, capture volume, threshold, boost
- (void)showVoice:(const assistant::VoiceSettings&)settings profilesExist:(BOOL)profilesExist
{
	[self begin:Page::voice back:NO nextEnabled:YES];
	self.playbackVolume = settings.playbackVolume;
	self.captureVolume.doubleValue = settings.captureVolume;
	self.captureThreshold.doubleValue = settings.captureThreshold;
	self.boost.state = settings.boost ? NSControlStateValueOn : NSControlStateValueOff;

	[self label:@"Battlefield 2 voice chat uses your microphone. Speak to see your level; voice is sent while the level is above the threshold marker." y:214 height:76 size:14];
	self.micStatus = [self label:@"" y:184 height:22 size:13];
	self.micStatus.frame = NSMakeRect(20, 184, 420, 22);
	self.micButton = [NSButton buttonWithTitle:@"" target:self action:@selector(micButton:)];
	self.micButton.frame = NSMakeRect(440, 179, 140, 32);
	self.micButton.hidden = YES;
	[self.body addSubview:self.micButton];
	[self.body addSubview:self.meter];

	[self label:@"Capture volume" y:120 height:22 size:14].frame = NSMakeRect(20, 120, 150, 22);
	[self.body addSubview:self.captureVolume];
	[self label:@"Activation threshold" y:84 height:22 size:14].frame = NSMakeRect(20, 84, 150, 22);
	[self.body addSubview:self.captureThreshold];
	[self.body addSubview:self.boost];

	NSTextField* note = [self label:profilesExist ? @"Saved as the default for new profiles. Profiles that already exist keep the voice settings from the game’s audio options." : @"Saved as the voice default for your first profile. You can change it later in the game’s audio options." y:4 height:36 size:12];
	note.textColor = NSColor.secondaryLabelColor;
	[self voiceChanged:nil];
}

- (void)voiceChanged:(id)sender
{
	self.meter.threshold = self.captureThreshold.doubleValue;
	self.captureVolume.toolTip = [NSString stringWithFormat:@"%.0f%%", self.captureVolume.doubleValue * 100];
	self.captureThreshold.toolTip = [NSString stringWithFormat:@"%.0f%%", self.captureThreshold.doubleValue * 100];
}

- (void)setMicrophone:(MicrophoneStatus)status
{
	if (self.page != Page::voice)
		return;

	self.micButton.hidden = status != MicrophoneStatus::ask && status != MicrophoneStatus::denied;
	switch (status) {
	case MicrophoneStatus::skipped:
		self.micStatus.stringValue = @"Microphone check skipped in test mode";
		break;
	case MicrophoneStatus::ask:
		self.micStatus.stringValue = @"Allow microphone access so the game never has to ask mid-match.";
		self.micButton.title = @"Allow Microphone";
		break;
	case MicrophoneStatus::denied:
		self.micStatus.stringValue = @"Microphone access is off. Allow Battlefield 2 in System Settings › Privacy & Security › Microphone.";
		self.micButton.title = @"Open System Settings";
		break;
	case MicrophoneStatus::ready:
		self.micStatus.stringValue = @"Microphone ready. Speak to test your level.";
		break;
	case MicrophoneStatus::missing:
		self.micStatus.stringValue = @"No microphone was found. You can still save your settings.";
		break;
	}
}

- (void)showReady:(BOOL)clientPresent mods:(const std::vector<Mod>&)mods selected:(const std::string&)selected
{
	[self begin:Page::ready back:NO nextEnabled:clientPresent];
	[self label:clientPresent ? @"Installation verified. Your game data is ready." : @"Installation verified. The game client has not been included in this build." y:165 height:76 size:16];

	self.modsFolder = [NSButton buttonWithTitle:@"Open Mods Folder" target:self action:@selector(modsFolder:)];
	self.modsFolder.frame = NSMakeRect(170, 40, 170, 32);
	[self.body addSubview:self.modsFolder];

	// with Battlefield 2 alone there is nothing to choose
	self.selectedMod = ::text(selected);
	self.mods = nil;
	if (mods.size() > 1) {
		[self label:@"Mod" y:126 height:22 size:14].frame = NSMakeRect(20, 126, 50, 22);
		self.mods = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(70, 122, 360, 26) pullsDown:NO];
		self.mods.accessibilityLabel = @"Mod";
		for (const auto& mod : mods) {
			// two mods may carry the same title, and a menu drops the second of two equal items
			NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:text(mod.title) action:nil keyEquivalent:@""];
			item.representedObject = ::text(mod.directory);
			[self.mods.menu addItem:item];
			if (mod.directory == selected)
				[self.mods selectItem:item];
		}

		[self.body addSubview:self.mods];
	}

	self.voiceSetup = [NSButton buttonWithTitle:@"Voice Setup…" target:self action:@selector(voiceSetup:)];
	self.voiceSetup.frame = NSMakeRect(20, 40, 140, 32);
	[self.body addSubview:self.voiceSetup];
}

- (void)setBusy:(NSString*)label
{
	debug(@"busy %@", label);
	self.next.title = label;
	self.next.enabled = NO;
	self.back.enabled = NO;
	self.voiceSetup.enabled = NO;
	self.modsFolder.enabled = NO;
	self.mods.enabled = NO;
	for (FileWell* well in @[ self.disc, self.patch141, self.patch150 ])
		well.enabled = NO;
}

- (void)message:(NSString*)title detail:(NSString*)detail
{
	debug(@"message title=%@ detail=%@", title, detail);
	NSAlert* alert = [NSAlert new];
	alert.messageText = title;
	alert.informativeText = detail;
	[alert beginSheetModalForWindow:self.window completionHandler:nil];
}

- (void)snapshot:(NSString*)file
{
	NSView* view = self.window.contentView;
	[self.window display];
	NSBitmapImageRep* bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
	[view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
	NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
	if (![png writeToFile:file atomically:YES])
		throw std::runtime_error{ "UI snapshot write failed" };
}
@end

struct Window::Impl
{
	boost::asio::io_context& context;
	// what the user did and the application has not looked at yet
	boost::asio::experimental::channel<void(boost::system::error_code, Event)> events;
	WindowController* controller = nil;
	bool hidden = false;

	explicit Impl(boost::asio::io_context& context)
		: context{ context }, events{ context, 64 }
	{
	}

	// a click on a page which is gone means nothing on the page which replaced it
	void Forget()
	{
		while (events.try_receive([](boost::system::error_code, Event) {})) {
		}
	}
};

Window::Window(boost::asio::io_context& context)
	: m_Impl{ std::make_unique<Impl>(context) }
{
	[NSApplication sharedApplication];
	[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
	m_Impl->controller = [WindowController new];
	NSApp.delegate = m_Impl->controller;

	auto impl = m_Impl.get();
	m_Impl->controller.emit = ^(Event::Kind kind, NSString* chosen) {
		impl->events.try_send(boost::system::error_code{}, Event{ kind, ::path(chosen) });
	};
}

Window::~Window()
{
	m_Impl->controller.emit = nil;
	NSApp.delegate = nil;
}

void Window::Run()
{
	if (!m_Impl->hidden) {
		[m_Impl->controller.window makeKeyAndOrderFront:nil];
		[NSApp activateIgnoringOtherApps:YES];
	}

	// the coroutines run between the events of AppKit. the common modes keep them running while a sheet,
	// a menu or a slider tracks the mouse
	auto impl = m_Impl.get();
	NSTimer* timer = [NSTimer timerWithTimeInterval:0.01 repeats:YES block:^(NSTimer*) {
		impl->context.poll();
		if (impl->context.stopped())
			impl->context.restart();
	}];
	[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
	[NSApp run];
	[timer invalidate];
}

void Window::Close()
{
	m_Impl->controller.closing = YES;
	[m_Impl->controller.window orderOut:nil];
	[NSApp stop:nil];
	// stop only takes effect after an event: this one is it
	[NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:YES];
}

task<Event> Window::WaitForEvent()
{
	co_return co_await m_Impl->events.async_receive(boost::asio::use_awaitable);
}

void Window::ShowWelcome()
{
	m_Impl->Forget();
	[m_Impl->controller showWelcome];
}

void Window::ShowDisc()
{
	m_Impl->Forget();
	[m_Impl->controller showDisc];
}

void Window::ShowPatches()
{
	m_Impl->Forget();
	[m_Impl->controller showPatches];
}

void Window::ShowKeys(bool deluxe, bool back)
{
	m_Impl->Forget();
	[m_Impl->controller showKeys:deluxe back:back];
}

void Window::ShowInstall()
{
	m_Impl->Forget();
	[m_Impl->controller showInstall];
}

void Window::ShowVoice(const assistant::VoiceSettings& settings, bool profilesExist)
{
	m_Impl->Forget();
	[m_Impl->controller showVoice:settings profilesExist:profilesExist];
}

void Window::ShowReady(bool clientPresent, const std::vector<Mod>& mods, const std::string& selected)
{
	m_Impl->Forget();
	[m_Impl->controller showReady:clientPresent mods:mods selected:selected];
}

void Window::SetBusy(const std::string& label)
{
	m_Impl->Forget();
	[m_Impl->controller setBusy:text(label)];
}

void Window::EnableNext(bool enabled)
{
	m_Impl->controller.next.enabled = enabled;
}

void Window::SetPatch(PatchSlot slot, PatchStatus status, double fraction)
{
	[m_Impl->controller setPatch:slot status:status fraction:fraction];
}

KeyEntry Window::Keys() const
{
	WindowController* controller = m_Impl->controller;
	auto keys = KeyEntry{};
	keys.key = [controller keyForRow:0].UTF8String;
	keys.install_special_forces = controller.deluxe && controller.specialForces.state == NSControlStateValueOn;
	if (controller.deluxe)
		keys.special_forces_key = [controller keyForRow:1].UTF8String;

	return keys;
}

void Window::ClearKeys()
{
	[m_Impl->controller clearKeys];
}

void Window::SetKeyStatus(KeyEdition row, KeyStatus status)
{
	[m_Impl->controller setKeyStatus:status row:row == KeyEdition::BF2 ? 0 : 1];
}

void Window::SetProgress(const Progress& progress)
{
	[m_Impl->controller showProgress:progress];
}

void Window::SetStatus(const std::string& text)
{
	WindowController* controller = m_Impl->controller;
	if (controller.page != Page::install)
		return;

	// the installer keeps reporting until it has stopped, this line stays
	controller.cancelling = YES;
	controller.next.enabled = NO;
	controller.status.stringValue = ::text(text);
}

std::string Window::SelectedMod() const
{
	WindowController* controller = m_Impl->controller;
	NSString* chosen = controller.mods.selectedItem.representedObject ?: controller.selectedMod;
	return chosen.UTF8String ?: "";
}

assistant::VoiceSettings Window::Voice() const
{
	WindowController* controller = m_Impl->controller;
	return { controller.playbackVolume, controller.captureVolume.doubleValue, controller.captureThreshold.doubleValue, controller.boost.state == NSControlStateValueOn };
}

void Window::SetMicrophone(MicrophoneStatus status)
{
	[m_Impl->controller setMicrophone:status];
}

void Window::SetLevel(double peak)
{
	// the client applies the capture volume itself, the meter previews the same scaling
	WindowController* controller = m_Impl->controller;
	if (controller.page == Page::voice)
		controller.meter.level = peak * controller.captureVolume.doubleValue;
}

void Window::Message(const std::string& title, const std::string& detail)
{
	[m_Impl->controller message:text(title) detail:text(detail)];
}

void Window::Reveal(const std::filesystem::path& directory)
{
	[NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:text(directory.string()) isDirectory:YES]];
}

void Window::StayHidden()
{
	m_Impl->hidden = true;
	[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
}

Page Window::Shown() const
{
	return m_Impl->controller.page;
}

void Window::Click(Event::Kind button)
{
	WindowController* controller = m_Impl->controller;
	if (button == Event::Kind::next)
		[controller.next performClick:nil];
	else if (button == Event::Kind::back)
		[controller.back performClick:nil];
	else if (button == Event::Kind::voice_setup)
		[controller.voiceSetup performClick:nil];
	else if (button == Event::Kind::microphone)
		[controller.micButton performClick:nil];
}

void Window::Choose(Event::Kind well, const std::filesystem::path& file)
{
	WindowController* controller = m_Impl->controller;
	FileWell* target = well == Event::Kind::disc_chosen ? controller.disc : well == Event::Kind::patch141_chosen ? controller.patch141 : controller.patch150;
	target.path = ::text(file.string());
}

void Window::PasteKey(KeyEdition row, const std::string& text)
{
	NSString* pasted = ::text(text);
	[m_Impl->controller pasteKey:pasted row:row == KeyEdition::BF2 ? 0 : 1];
}

void Window::Snapshot(const std::filesystem::path& png)
{
	[m_Impl->controller snapshot:text(png.string())];
}
