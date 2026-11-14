#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#include "Installer.h"
#include "SetupSession.h"
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

static NSString* smokeDirectory=nil;
static NSString* testHome=nil;
static BOOL discActionTest=NO;
static BOOL captureAllowed=YES; // tests never touch the microphone or its permission prompt
static void Debug(NSString* format,...) NS_FORMAT_FUNCTION(1,2);
static void Debug(NSString* format,...) {
    const char* enabled=std::getenv("SETUP_DEBUG");if(!enabled||std::strcmp(enabled,"1"))return;
    va_list args;va_start(args,format);NSString* line=[[NSString alloc] initWithFormat:format arguments:args];va_end(args);
    std::fprintf(stderr,"[setup] [%s] %s\n",NSThread.isMainThread?"main":"worker",line.UTF8String);
}
static NSString* Text(const std::string& s) { return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"Unable to display this message"; }
// NSString rejects fileSystemRepresentation on an empty string with an ObjC
// exception, which a C++ std::exception handler cannot catch.
static setup::fs::path Path(NSString* s) { return s.length?setup::fs::path(s.fileSystemRepresentation):setup::fs::path{}; }
static NSString* Support() { return [(testHome ?: NSHomeDirectory()) stringByAppendingPathComponent:@"Library/Application Support/OpenBF2"]; }
static setup::fs::path Contents() { return Path([NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Contents"]); }
static NSString* Manifest(setup::DiscEdition edition) { return [NSBundle.mainBundle pathForResource:@(setup::assistant::manifestName(edition)) ofType:@"tsv" inDirectory:@"Setup"]; }

@interface FileWell : NSView
@property(nonatomic,copy) NSString* path;
@property(copy) void (^changed)(void);
@property BOOL disc;
@property BOOL verified;
@property NSUInteger revision;
@property(strong) NSProgressIndicator* checking;
@property(strong) NSTextField* label;
- (instancetype)initWithTitle:(NSString*)title disc:(BOOL)disc;
@end
@implementation FileWell
- (instancetype)initWithTitle:(NSString*)title disc:(BOOL)disc {
    if((self=[super initWithFrame:NSMakeRect(0,0,560,92)])) {
        _disc=disc;self.wantsLayer=YES;self.layer.cornerRadius=8;self.layer.borderWidth=1;self.layer.borderColor=NSColor.separatorColor.CGColor;
        NSTextField* heading=[NSTextField labelWithString:title];heading.frame=NSMakeRect(16,60,420,22);[self addSubview:heading];
        _label=[NSTextField labelWithString:@"Drop a file here or choose…"];_label.frame=NSMakeRect(16,16,425,36);_label.lineBreakMode=NSLineBreakByTruncatingMiddle;[self addSubview:_label];
        NSButton* choose=[NSButton buttonWithTitle:@"Choose…" target:self action:@selector(choose:)];choose.frame=NSMakeRect(450,27,95,32);[self addSubview:choose];
        choose.accessibilityLabel=[@"Choose " stringByAppendingString:title];self.accessibilityLabel=title;
        _checking=[[NSProgressIndicator alloc] initWithFrame:NSMakeRect(16,6,528,5)];_checking.indeterminate=NO;_checking.maxValue=1;_checking.hidden=YES;[self addSubview:_checking];
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    }return self;
}
- (void)setPath:(NSString*)path { _path=[path copy];++self.revision;self.verified=NO;Debug(@"selection well=%@ revision=%lu path=%@",self.accessibilityLabel,(unsigned long)self.revision,path);self.label.stringValue=path ?: @"Drop a file here or choose…";if(self.changed)self.changed(); }
- (void)choose:(id)sender {
    Debug(@"action choose well=%@",self.accessibilityLabel);
    NSOpenPanel* panel=NSOpenPanel.openPanel;panel.canChooseDirectories=self.disc;panel.canChooseFiles=YES;panel.allowsMultipleSelection=NO;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response){Debug(@"action choose-result well=%@ response=%ld",self.accessibilityLabel,(long)response);if(response==NSModalResponseOK)self.path=panel.URL.path;}];
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {return NSDragOperationCopy;}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray<NSURL*>* urls=[sender.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    Debug(@"action drop well=%@ count=%lu",self.accessibilityLabel,(unsigned long)urls.count);if(urls.count!=1)return NO;self.path=urls.firstObject.path;return YES;
}
@end

// A 0-1 peak meter with the activation threshold drawn as a marker, the same
// scale retail's BF2VoiceSetup shows against its threshold.
@interface LevelMeter : NSView
@property(nonatomic) double level;
@property(nonatomic) double threshold;
@end
@implementation LevelMeter
- (void)setLevel:(double)level {_level=fmin(1.0,fmax(0.0,level));self.needsDisplay=YES;}
- (void)setThreshold:(double)threshold {_threshold=fmin(1.0,fmax(0.0,threshold));self.needsDisplay=YES;}
- (BOOL)isAccessibilityElement {return YES;}
- (NSAccessibilityRole)accessibilityRole {return NSAccessibilityLevelIndicatorRole;}
- (id)accessibilityValue {return [NSString stringWithFormat:@"%.0f%%",self.level*100];}
- (void)drawRect:(NSRect)dirty {
    NSRect bounds=NSInsetRect(self.bounds,0.5,0.5);
    NSBezierPath* frame=[NSBezierPath bezierPathWithRoundedRect:bounds xRadius:4 yRadius:4];
    [NSColor.quaternaryLabelColor setFill];[frame fill];
    NSRect fill=bounds;fill.size.width=bounds.size.width*self.level;
    [(self.level>=self.threshold&&self.level>0?NSColor.systemGreenColor:NSColor.systemGrayColor) setFill];
    [[NSBezierPath bezierPathWithRoundedRect:fill xRadius:4 yRadius:4] fill];
    CGFloat x=bounds.origin.x+bounds.size.width*self.threshold;
    [NSColor.labelColor setStroke];NSBezierPath* marker=[NSBezierPath bezierPath];marker.lineWidth=2;
    [marker moveToPoint:NSMakePoint(x,bounds.origin.y)];[marker lineToPoint:NSMakePoint(x,NSMaxY(bounds))];[marker stroke];
    [NSColor.separatorColor setStroke];[frame stroke];
}
@end

@class Assistant;
@interface KeyField : NSTextField
@property(weak) Assistant* assistant;
@property NSInteger index;
@end
@interface Assistant : NSObject <NSApplicationDelegate,NSWindowDelegate,NSTextFieldDelegate>
@property(strong) NSWindow* window;
@property(strong) NSView* body;
@property(strong) NSButton* next;
@property(strong) NSButton* back;
@property(strong) NSTextField* status;
@property(strong) NSProgressIndicator* progress;
@property(strong) FileWell* disc;
@property(strong) FileWell* patch141;
@property(strong) FileWell* patch150;
@property(strong) NSArray<KeyField*>* keys;
@property(strong) NSArray<NSTextField*>* keyStatuses;
@property(strong) NSButton* specialForces; // Deluxe media only: install the expansion (default) or the base game alone
@property(nonatomic) NSInteger step;
@property(nonatomic) BOOL working;
@property BOOL deluxe;
@property(copy) NSString* gameRoot;
@property(strong) NSTask* client;
@property(strong) NSSlider* captureVolume;
@property(strong) NSSlider* captureThreshold;
@property(strong) NSButton* boost;
@property(strong) LevelMeter* meter;
@property(strong) NSTextField* micStatus;
@property(strong) NSButton* micButton;
@property(strong) AVAudioEngine* engine;
- (void)pasteKey:(NSString*)value;
- (void)pasteKey:(NSString*)value row:(NSInteger)row;
- (void)runDiscActionTest;
@end
@implementation KeyField
- (void)paste:(id)sender { [self.assistant pasteKey:[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] ?: @"" row:self.index/5]; }
// NSTextField's field editor owns keyboard commands; the delegate routes paste.
@end
@implementation Assistant {
    std::atomic<bool> _cancelled;
    std::unique_ptr<setup::Lock> _clientLock;
    std::shared_ptr<std::atomic<bool>> _checking141,_checking150;
    setup::assistant::VoiceSettings _voice; // playback volume is carried through unchanged (retail has no slider)
}
@synthesize step=_step,working=_working;
- (void)setStep:(NSInteger)step {Debug(@"state step %ld -> %ld working=%d",(long)_step,(long)step,_working);_step=step;}
- (void)setWorking:(BOOL)working {Debug(@"state working %d -> %d step=%ld",_working,working,(long)_step);_working=working;}
- (void)trace:(NSString*)action {
    Debug(@"%@ step=%ld working=%d next.enabled=%d next.title=%@ target=%@ action=%@ disc=%@ revision=%lu patches=%d/%d deluxe=%d special-forces=%d game-root=%@",action,(long)self.step,self.working,self.next.enabled,self.next.title,self.next.target==self?@"assistant":@"other/nil",NSStringFromSelector(self.next.action),self.disc.path,(unsigned long)self.disc.revision,self.patch141.verified,self.patch150.verified,self.deluxe,[self installSpecialForces],self.gameRoot);
}
- (BOOL)installSpecialForces {return self.deluxe&&self.specialForces.state==NSControlStateValueOn;}
- (setup::DiscEdition)edition {return self.deluxe?([self installSpecialForces]?setup::DiscEdition::Deluxe:setup::DiscEdition::DeluxeBase):setup::DiscEdition::BF2;}
- (void)specialForcesChanged:(id)sender {
    Debug(@"action special-forces=%d",[self installSpecialForces]);
    for(KeyField* f in self.keys)f.enabled=f.index<5||[self installSpecialForces];
    [self updateKeyStatus];
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    Debug(@"action launch test-home=%@ smoke=%d disc-action-test=%d",testHome,smokeDirectory!=nil,discActionTest);
    _cancelled=false;self.step=0;
    self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,660,480) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title=@"OpenBF2";self.window.delegate=self;[self.window center];
    self.disc=[[FileWell alloc] initWithTitle:@"Battlefield 2 DVD or ISO" disc:YES];
    self.patch141=[[FileWell alloc] initWithTitle:@"Patch 1.0 → 1.41" disc:NO];
    self.patch150=[[FileWell alloc] initWithTitle:@"Patch 1.41 → 1.50" disc:NO];
    __weak Assistant* weakSelf=self;
    self.patch141.changed=^{[weakSelf checkPatch:weakSelf.patch141 slot:setup::PatchSlot::Patch141];};
    self.patch150.changed=^{[weakSelf checkPatch:weakSelf.patch150 slot:setup::PatchSlot::Patch150];};
    self.specialForces=[NSButton checkboxWithTitle:@"Install Special Forces" target:self action:@selector(specialForcesChanged:)];self.specialForces.frame=NSMakeRect(20,100,300,22);self.specialForces.state=NSControlStateValueOn;
    NSMutableArray* keys=[NSMutableArray array];
    for(NSInteger i=0;i<10;++i){KeyField* f=[[KeyField alloc] initWithFrame:NSMakeRect(20+(i%5)*106,i<5?154:64,92,32)];f.assistant=self;f.index=i;f.delegate=self;f.font=[NSFont monospacedSystemFontOfSize:20 weight:NSFontWeightRegular];f.accessibilityLabel=[NSString stringWithFormat:@"%@ CD key group %ld of 5",i<5?@"Battlefield 2":@"Special Forces",(long)(i%5)+1];[keys addObject:f];}self.keys=keys;
    self.captureVolume=[NSSlider sliderWithValue:1 minValue:0 maxValue:1 target:self action:@selector(voiceChanged:)];self.captureVolume.frame=NSMakeRect(170,118,320,24);self.captureVolume.continuous=YES;self.captureVolume.accessibilityLabel=@"Capture volume";
    self.captureThreshold=[NSSlider sliderWithValue:0.1 minValue:0 maxValue:1 target:self action:@selector(voiceChanged:)];self.captureThreshold.frame=NSMakeRect(170,82,320,24);self.captureThreshold.continuous=YES;self.captureThreshold.accessibilityLabel=@"Activation threshold";
    self.boost=[NSButton checkboxWithTitle:@"Microphone boost" target:self action:@selector(voiceChanged:)];self.boost.frame=NSMakeRect(20,46,300,22);
    self.meter=[[LevelMeter alloc] initWithFrame:NSMakeRect(20,158,560,20)];self.meter.threshold=0.1;self.meter.accessibilityLabel=@"Microphone level";
    self.next=[NSButton buttonWithTitle:@"Continue" target:self action:@selector(next:)];self.next.frame=NSMakeRect(510,22,115,34);self.next.keyEquivalent=@"\r";
    self.back=[NSButton buttonWithTitle:@"Back" target:self action:@selector(back:)];self.back.frame=NSMakeRect(34,22,90,34);
    [self.window.contentView addSubview:self.next];[self.window.contentView addSubview:self.back];
    [self show];if(!discActionTest){[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];}
    NSMenu* menu=[NSMenu new];NSMenuItem* app=[NSMenuItem new];[menu addItem:app];NSMenu* submenu=[NSMenu new];
    [submenu addItemWithTitle:@"Quit OpenBF2" action:@selector(terminate:) keyEquivalent:@"q"];app.submenu=submenu;NSApp.mainMenu=menu;
    NSMenuItem* edit=[NSMenuItem new];edit.title=@"Edit";[menu addItem:edit];NSMenu* editMenu=[[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];edit.submenu=editMenu;
    if(discActionTest) {
        [self runDiscActionTest];
    } else if(smokeDirectory) {
        dispatch_async(dispatch_get_main_queue(),^{
            try { @try {
                for(NSInteger step=0;step<7;++step){self.step=step;[self show];if(step==5)[self meterPeak:0.35];[self.window display];
                    NSView* view=self.window.contentView;NSBitmapImageRep* rep=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
                    NSString* file=[smokeDirectory stringByAppendingPathComponent:[NSString stringWithFormat:@"wizard-%ld.png",(long)step]];
                    NSData* png=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];if(![png writeToFile:file atomically:YES])throw std::runtime_error("UI snapshot write failed");
                }
                self.step=3;[self show];if(self.next.enabled)throw std::runtime_error("Empty key enabled Install");[self pasteKey:@"ABCD-EFGH-1234-5678-IJKL"];
                if(![self.key isEqualToString:@"ABCDEFGH12345678IJKL"])throw std::runtime_error("CD key paste failed");
                if(self.next.enabled||![self.keyStatuses[0].stringValue hasPrefix:@"Invalid key"])throw std::runtime_error("Invalid base key status/gating failed");
                self.deluxe=YES;[self show];[self pasteKey:@"1111-2222-3333-4444-5555" row:1];
                if(![[self keyForRow:1] isEqualToString:@"11112222333344445555"]||![self.key isEqualToString:@"ABCDEFGH12345678IJKL"])throw std::runtime_error("Special Forces key paste crossed rows");
                if(self.next.enabled||![self.keyStatuses[1].stringValue hasPrefix:@"Invalid key"])throw std::runtime_error("Invalid Special Forces key status/gating failed");
                self.keys[9].stringValue=@"";[self updateKeyStatus];
                if(self.next.enabled||![self.keyStatuses[1].stringValue isEqualToString:@"Enter all five groups"])throw std::runtime_error("Incomplete Special Forces key status/gating failed");
                [self pasteKey:@"1111-2222-3333-4444-5555" row:1];
                if([self edition]!=setup::DiscEdition::Deluxe||!self.keys[9].enabled)throw std::runtime_error("Deluxe default must install Special Forces with enabled key fields");
                self.specialForces.state=NSControlStateValueOff;[self specialForcesChanged:self.specialForces];
                if([self edition]!=setup::DiscEdition::DeluxeBase||self.keys[5].enabled||self.keys[9].enabled||![self.keyStatuses[1].stringValue isEqualToString:@"Special Forces will not be installed"]||self.next.enabled)throw std::runtime_error("Unchecking Special Forces did not disable its key row or select the base edition");
                self.specialForces.state=NSControlStateValueOn;[self specialForcesChanged:self.specialForces];
                if([self edition]!=setup::DiscEdition::Deluxe||!self.keys[5].enabled||![self.keyStatuses[1].stringValue hasPrefix:@"Invalid key"])throw std::runtime_error("Re-checking Special Forces did not restore its key row");
                NSView* view=self.window.contentView;NSBitmapImageRep* rep=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
                NSData* png=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                if(![png writeToFile:[smokeDirectory stringByAppendingPathComponent:@"wizard-deluxe-3.png"] atomically:YES])throw std::runtime_error("Deluxe UI snapshot write failed");
                for(KeyField* field in self.keys)field.stringValue=@"";
                self.step=5;[self show];[self meterPeak:0.5];if(self.meter.level<0.49||self.meter.level>0.51||self.meter.threshold!=0.1)throw std::runtime_error("Voice meter level/threshold not rendered from defaults");
                self.captureVolume.doubleValue=0.5;[self voiceChanged:self.captureVolume];[self meterPeak:0.5];if(self.meter.level<0.24||self.meter.level>0.26)throw std::runtime_error("Capture volume not applied to the meter");
                if(setup::assistant::voiceSettingsText([self voiceSettings])!="1\n0.5\n0.1\n0\n")throw std::runtime_error("Voice settings text from the controls is wrong");
                self.captureVolume.doubleValue=1;[self voiceChanged:self.captureVolume];
                std::cout<<"Seven wizard screens plus Deluxe key screen rendered; grouped paste, live invalid/incomplete status, Install gating and the voice meter/settings passed\n";[NSApp terminate:nil];
            } @catch(NSException* exception){throw std::runtime_error("AppKit exception in UI smoke test");}
            }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::exit(1);}
        });
    } else [self detectInstallation];
}
- (void)message:(NSString*)title detail:(NSString*)detail {
    Debug(@"alert title=%@ detail=%@",title,detail);
    NSAlert* alert=[NSAlert new];alert.messageText=title;alert.informativeText=detail;[alert beginSheetModalForWindow:self.window completionHandler:nil];
}
- (void)runDiscActionTest {
    // Exercise production launch, FileWell's shared panel/drop setter, the real
    // buttons and async validation. No direct step assignment or validation call.
    dispatch_async(dispatch_get_main_queue(),^{
        try { @try {
            if(self.step!=0||self.working||self.patch141.path||self.patch150.path)throw std::runtime_error("Expected fresh Welcome with empty patch wells");
            if(self.next.target!=self||self.next.action!=@selector(next:)||![self.next.keyEquivalent isEqualToString:@"\r"])throw std::runtime_error("Continue target/action/Return binding lost");
            [self.next performClick:nil];
            if(self.step!=1||self.working||!self.next.enabled||self.next.target!=self||self.next.action!=@selector(next:))throw std::runtime_error("Welcome click did not reach disc selection with a live Continue button");
            self.disc.path=NSProcessInfo.processInfo.environment[@"SETUP_TEST_ISO"];
            if(![self.disc.label.stringValue isEqualToString:self.disc.path])throw std::runtime_error("FileWell label and selected path differ");
            [self.next performClick:nil];
            if(self.step!=1||!self.working||self.next.enabled||self.back.enabled||![self.next.title isEqualToString:@"Checking…"])throw std::runtime_error("Disc click did not start async validation");
            __block NSUInteger ticks=0;
            [NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer* timer){
                if(!self.working&&self.step==2){
                    if(self.window.attachedSheet||self.next.enabled||!self.back.enabled||self.next.target!=self||self.next.action!=@selector(next:)){
                        std::cerr<<"Disc click reached an invalid patch-page state\n";std::exit(1);
                    }
                    [timer invalidate];
                    std::cout<<"Wizard clicks passed: Welcome -> disc selection -> Checking -> patches; patch paths remained empty\n";
                    [NSApp terminate:nil];
                } else if(!self.working||++ticks>=1200){
                    [self trace:@"disc-action-test failed"];
                    std::cerr<<"Disc click failed or timed out before reaching patches\n";std::exit(1);
                }
            }];
        } @catch(NSException* exception){
            std::cerr<<"Objective-C exception in wizard click test: "<<exception.reason.UTF8String<<'\n';std::exit(1);
        }}catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::exit(1);}
    });
}
- (NSTextField*)label:(NSString*)text y:(CGFloat)y size:(CGFloat)size {
    NSTextField* label=[NSTextField wrappingLabelWithString:text];label.frame=NSMakeRect(20,y,560,76);label.font=[NSFont systemFontOfSize:size];[self.body addSubview:label];return label;
}
- (void)show {
    [self.body removeFromSuperview];self.body=[[NSView alloc] initWithFrame:NSMakeRect(30,76,600,370)];[self.window.contentView addSubview:self.body];
    if(self.step!=5)[self stopMeter];
    self.back.hidden=self.step==0||self.step>=4||(self.step==3&&self.gameRoot);self.next.enabled=!self.working&&(self.step!=2||(self.patch141.verified&&self.patch150.verified));self.back.enabled=!self.working;
    self.next.title=self.step==3?@"Install":self.step==4?@"Cancel":self.step==5?@"Save":self.step==6?@"Play":@"Continue";
    NSArray* titles=@[@"Welcome to Battlefield 2",@"Choose your disc",@"Choose the two patches",@"Enter your CD key",@"Installing Battlefield 2",@"Set up voice chat",@"Ready to play"];
    [self label:titles[self.step] y:280 size:27];
    if(self.step==0)[self label:@"Install from your Battlefield 2 DVD or ISO and the official 1.41 and 1.50 patches. Game data, your key and profiles are stored in your Library’s Application Support/OpenBF2 folder." y:155 size:16];
    if(self.step==1){self.disc.frame=NSMakeRect(20,150,560,92);[self.body addSubview:self.disc];[self label:@"Choose an English BF2 or BF2 Deluxe DVD or ISO. Deluxe includes Special Forces. Your original files are read only." y:50 size:14];}
    if(self.step==2){self.patch141.frame=NSMakeRect(20,165,560,92);self.patch150.frame=NSMakeRect(20,60,560,92);[self.body addSubview:self.patch141];[self.body addSubview:self.patch150];}
    if(self.step==3){[self label:@"Enter the key printed with your game. You can paste the whole key." y:205 size:16];NSTextField* base=[self label:@"Battlefield 2" y:190 size:14];base.frame=NSMakeRect(20,190,560,22);
        if(self.deluxe)[self.body addSubview:self.specialForces];
        NSMutableArray* statuses=[NSMutableArray array];
        for(NSInteger row=0;row<(self.deluxe?2:1);++row){NSTextField* status=[self label:@"" y:row==0?126:36 size:13];status.frame=NSMakeRect(20,row==0?126:36,560,22);[statuses addObject:status];}
        self.keyStatuses=statuses;
        for(KeyField* f in self.keys)if(f.index<5||self.deluxe){f.enabled=f.index<5||[self installSpecialForces];[self.body addSubview:f];}[self updateKeyStatus];[self.window makeFirstResponder:self.keys.firstObject];}
    if(self.step==4){
        self.progress=[[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20,187,560,18)];self.progress.indeterminate=NO;self.progress.minValue=0;self.progress.maxValue=1;[self.body addSubview:self.progress];
        self.status=[self label:@"Checking your media…" y:85 size:14];self.status.frame=NSMakeRect(20,40,560,130);
    }
    if(self.step==5)[self showVoiceSetup];
    if(self.step==6){
        BOOL client=setup::assistant::clientPresent(Contents());
        [self label:client?@"Installation verified. Your game data is ready.":@"Installation verified. The game client has not been included in this build." y:165 size:16];self.next.enabled=client&&!self.client;
        NSButton* voice=[NSButton buttonWithTitle:@"Voice Setup…" target:self action:@selector(voiceSetup:)];voice.frame=NSMakeRect(20,40,140,32);voice.enabled=!self.client;[self.body addSubview:voice];
    }
    [self trace:@"show"];
}
- (void)pasteKey:(NSString*)value {[self pasteKey:value row:0];}
- (void)pasteKey:(NSString*)value row:(NSInteger)row {
    Debug(@"action paste-key row=%ld (value omitted)",(long)row);
    NSString* compact;
    try {compact=Text(setup::assistant::normalizedKey(std::string(value.UTF8String)));}
    catch(const std::exception&){[self message:@"Check the key’s format" detail:@"Paste 20 letters or numbers, optionally in five groups separated by hyphens."];return;}
    for(NSUInteger i=0;i<5;++i)self.keys[row*5+i].stringValue=[[compact substringWithRange:NSMakeRange(i*4,4)] uppercaseString];[self.window makeFirstResponder:self.keys[row*5+4]];
    [self updateKeyStatus];
}
- (BOOL)control:(NSControl*)control textView:(NSTextView*)view doCommandBySelector:(SEL)selector {
    Debug(@"action key-command row=%ld selector=%@",(long)((KeyField*)control).index/5,NSStringFromSelector(selector));
    if(selector==@selector(paste:)){
        NSString* value=[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];if(value.length>=20){[self pasteKey:value row:((KeyField*)control).index/5];return YES;}
    }
    if(selector==@selector(deleteBackward:)) {KeyField* f=(KeyField*)control;if(f.stringValue.length==0&&f.index%5>0){[self.window makeFirstResponder:self.keys[f.index-1]];return YES;}}
    return NO;
}
- (void)controlTextDidChange:(NSNotification*)notification {
    Debug(@"action edit-key (value omitted)");
    KeyField* field=notification.object;NSString* value=field.stringValue.uppercaseString;
    if(value.length>=20){[self pasteKey:value row:field.index/5];return;}
    NSMutableString* filtered=[NSMutableString string];
    for(NSUInteger i=0;i<value.length&&filtered.length<4;++i){unichar c=[value characterAtIndex:i];if((c>='A'&&c<='Z')||(c>='0'&&c<='9'))[filtered appendFormat:@"%C",c];}
    field.stringValue=filtered;if(filtered.length==4&&field.index%5<4)[self.window makeFirstResponder:self.keys[field.index+1]];
    [self updateKeyStatus];
}
- (void)updateKeyStatus {
    if(self.step!=3)return;
    BOOL allValid=YES;
    for(NSInteger row=0;row<(self.deluxe?2:1);++row){
        if(row==1&&![self installSpecialForces]){NSTextField* status=self.keyStatuses[1];status.stringValue=@"Special Forces will not be installed";status.textColor=NSColor.secondaryLabelColor;status.accessibilityLabel=@"Special Forces: not installed";continue;}
        NSString* key=[self keyForRow:row];
        BOOL valid=setup::validateKey(row==0?setup::KeyEdition::BF2:setup::KeyEdition::SpecialForces,std::string(key.UTF8String));
        allValid=allValid&&valid;
        NSTextField* status=self.keyStatuses[row];
        status.stringValue=valid?@"Valid key — local check passed":key.length==20?@"Invalid key — check the characters and product":@"Enter all five groups";
        status.textColor=valid?NSColor.systemGreenColor:key.length==20?NSColor.systemRedColor:NSColor.secondaryLabelColor;
        status.accessibilityLabel=[NSString stringWithFormat:@"%@: %@",row==0?@"Battlefield 2":@"Special Forces",status.stringValue];
    }
    self.next.enabled=!self.working&&allValid;
    Debug(@"state keys-valid=%d next.enabled=%d",allValid,self.next.enabled);
}
- (NSString*)key {
    return [self keyForRow:0];
}
- (NSString*)keyForRow:(NSInteger)row {NSMutableString* key=[NSMutableString string];for(NSInteger i=row*5;i<row*5+5;++i)[key appendString:self.keys[i].stringValue];return key;}
- (void)back:(id)sender {[self trace:@"action back"];if(!self.working&&self.step>0&&self.step<4){--self.step;[self show];}}
- (void)next:(id)sender {
    [self trace:@"action next"];
    if(self.working&&self.step!=4){Debug(@"next ignored: working");return;}
    if(self.step==4){_cancelled.store(true);Debug(@"state cancellation requested by Cancel");self.next.enabled=NO;self.status.stringValue=@"Cancelling after the current patch record…";return;}
    if(self.step==5){[self saveVoice];return;}
    if(self.step==6){[self play];return;}
    try {
        if(self.step==1||self.step==2){
            if(self.step==1&&!self.disc.path.length)throw std::runtime_error("Choose your DVD or ISO first.");
            if(self.step==2&&(!self.patch141.path.length||!self.patch150.path.length))throw std::runtime_error("Choose both patches first.");
            if(self.step==1){[self validateSelection];return;}
            if(!self.patch141.verified||!self.patch150.verified)throw std::runtime_error("Wait for both patch checks to finish, then choose matching files.");
            ++self.step;[self show];return;
        }
        if(self.step==3){if([self installSpecialForces]&&!setup::validateKey(setup::KeyEdition::SpecialForces,std::string([self keyForRow:1].UTF8String)))throw std::runtime_error("Special Forces: invalid CD key.");if(!setup::validateKey(setup::KeyEdition::BF2,std::string(self.key.UTF8String)))throw std::runtime_error("Battlefield 2: invalid CD key.");if(self.gameRoot){[self saveKey];for(KeyField* field in self.keys)field.stringValue=@"";self.step=6;[self show];return;}self.step=4;[self show];[self install];return;}
        ++self.step;[self show];
    }catch(const std::exception& e){[self message:@"Check your setup files" detail:Text(e.what())];}
}
- (void)checkPatch:(FileWell*)well slot:(setup::PatchSlot)slot {
    Debug(@"patch-check start slot=%d revision=%lu",int(slot),(unsigned long)well.revision);
    auto& pending=slot==setup::PatchSlot::Patch141?_checking141:_checking150;
    if(pending){pending->store(true);Debug(@"patch-check cancellation requested slot=%d",int(slot));}
    pending=std::make_shared<std::atomic<bool>>(false);auto cancelled=pending;
    NSUInteger revision=well.revision;NSString* selected=well.path;auto path=Path(selected);
    well.checking.hidden=NO;well.checking.doubleValue=0;well.label.stringValue=[selected stringByAppendingString:@"\nChecking SHA-256…"];
    if(self.step==2)self.next.enabled=NO;
    std::thread([self,well,slot,path,selected,revision,cancelled]{@autoreleasepool{
        NSString* error=nil;setup::Control control;control.cancelled=cancelled.get();
        control.progress=[self,well,selected,revision](const setup::Progress& p){
            if(p.completedBytes%(8*1024*1024)&&p.completedBytes!=p.totalBytes)return;
            double fraction=p.totalBytes?double(p.completedBytes)/p.totalBytes:0;
            dispatch_async(dispatch_get_main_queue(),^{if(well.revision==revision){well.checking.doubleValue=fraction;well.label.stringValue=[NSString stringWithFormat:@"%@\nChecking SHA-256: %.0f%%",selected,fraction*100];}});
        };
        try{setup::verifyPatch(setup::View(path),slot,control);}catch(const std::exception& e){error=Text(e.what());}
        dispatch_async(dispatch_get_main_queue(),^{
            if(well.revision!=revision){Debug(@"patch-check stale slot=%d revision=%lu",int(slot),(unsigned long)revision);return;}
            well.checking.hidden=YES;well.verified=error==nil;
            well.label.stringValue=[selected stringByAppendingString:error?@"\nPatch verification failed":@"\nVerified official patch"];
            if(self.step==2)self.next.enabled=!self.working&&self.patch141.verified&&self.patch150.verified;
            if(error)[self message:@"Check your patch file" detail:error];
            [self trace:@"patch-check complete"];
        });
    }}).detach();
}
- (void)validateSelection {
    [self trace:@"disc-check start"];
    NSInteger step=self.step;NSUInteger revision=self.disc.revision;auto disc=Path(self.disc.path);
    self.working=YES;self.next.enabled=NO;self.back.enabled=NO;self.next.title=@"Checking…";
    [self trace:@"disc-check pending"];
    try {std::thread([self,step,revision,disc]{@autoreleasepool{
        NSString* error=nil;BOOL deluxe=NO;
        try {deluxe=setup::assistant::validateDisc(disc)==setup::DiscEdition::Deluxe;}
        catch(const std::exception& e){error=Text(e.what());}
        dispatch_async(dispatch_get_main_queue(),^{
            self.working=NO;
            BOOL unchanged=self.step==step&&self.disc.revision==revision;
            Debug(@"disc-check complete unchanged=%d deluxe=%d error=%@",unchanged,deluxe,error);
            if(!error&&unchanged){self.deluxe=deluxe;++self.step;}[self show];if(error&&unchanged)[self message:@"Check your setup files" detail:error];
        });
    }}).detach();}catch(...){self.working=NO;[self show];throw;}
}
- (void)saveKey {
    Debug(@"action save-keys (values omitted)");
    auto platform=setup::nativePlatform();setup::assistant::saveKeys(Path(Support()),std::string(self.key.UTF8String),[self installSpecialForces]?std::string([self keyForRow:1].UTF8String):"",*platform);
}
- (void)install {
    [self trace:@"action install"];
    self.working=YES;_cancelled=false;
    setup::InstallOptions options{Path(self.disc.path),Path(self.patch141.path),Path(self.patch150.path),Path(Support()),Path(Manifest([self edition])),{}};
    options.control.cancelled=&_cancelled;options.specialForces=[self installSpecialForces];
    // Parallel input checks report one item each; keep a line per item (main thread only).
    NSMutableArray<NSString*>* itemNames=[NSMutableArray array];NSMutableDictionary<NSString*,NSString*>* itemLines=[NSMutableDictionary dictionary];NSMutableString* lastPhase=[NSMutableString string];
    options.control.progress=[self,itemNames,itemLines,lastPhase](const setup::Progress& p){
        const setup::Progress copy=p;
        dispatch_async(dispatch_get_main_queue(),^{
            if(self.step!=4)return;
            NSDictionary* names=@{@"inputs":@"Checking media",@"disc":@"Installing DVD files",@"preserveRenderer":@"Preparing patch files",@"patch141":@"Applying patch 1.41",@"patch141-xp1":@"Patching Special Forces",@"overlay150":@"Installing patch 1.50",@"verify":@"Verifying game data"};
            NSString* phaseName=Text(copy.phase);NSString* phase=names[phaseName] ?: @"Installing";NSString* path=Text(copy.path);
            double fraction=copy.totalBytes?double(copy.completedBytes)/copy.totalBytes:(copy.totalFiles?double(copy.completedFiles)/copy.totalFiles:0);
            if(![lastPhase isEqualToString:phaseName]){[lastPhase setString:phaseName];[itemNames removeAllObjects];[itemLines removeAllObjects];}
            NSMutableString* description=[NSMutableString stringWithString:phase];
            if(copy.itemTotalBytes){
                if(!itemLines[path])[itemNames addObject:path];
                itemLines[path]=[NSString stringWithFormat:@"%@: %.0f%% of %.1f MB",path,100.0*double(copy.itemCompletedBytes)/double(copy.itemTotalBytes),copy.itemTotalBytes/1e6];
                for(NSString* name in itemNames)[description appendFormat:@"\n%@",itemLines[name]];
            } else if(path.length)[description appendFormat:@" — %@",path];
            NSMutableArray* counts=[NSMutableArray array];
            if(copy.totalFiles)[counts addObject:[NSString stringWithFormat:@"%zu of %zu files",copy.completedFiles,copy.totalFiles]];
            if(copy.totalBytes)[counts addObject:[NSString stringWithFormat:@"%.1f of %.1f MB",copy.completedBytes/1e6,copy.totalBytes/1e6]];
            if(counts.count)[description appendFormat:@"\n%@",[counts componentsJoinedByString:@" · "]];
            self.progress.doubleValue=fraction;self.status.stringValue=description;
        });
    };
    std::thread([self,options]{@autoreleasepool{
        try {auto platform=setup::nativePlatform();auto root=setup::install(options,*platform);NSString* game=Text(root.string());
            dispatch_async(dispatch_get_main_queue(),^{self.working=NO;self.gameRoot=game;try{[self saveKey];for(KeyField* field in self.keys)field.stringValue=@"";self.step=5;[self show];}catch(const std::exception& e){self.step=3;[self show];[self message:@"Key could not be saved" detail:Text(e.what())];}});
        }catch(const std::exception& e){NSString* error=Text(e.what());dispatch_async(dispatch_get_main_queue(),^{self.working=NO;self.step=3;[self show];[self message:@"Setup paused" detail:[error stringByAppendingString:@"\nYour previous installation is retained. Choose Install to retry or resume."]];});}
    }}).detach();
}
- (void)detectInstallation {
    [self trace:@"action detect-installation"];
    // Verify the generation before presenting Play. A receipt alone is insufficient.
    auto current=Path(Support())/"Setup/Current.txt";if(!setup::fs::exists(current)&&!setup::fs::is_symlink(setup::fs::symlink_status(current))){Debug(@"detect-installation: no current generation");return;}
    self.working=YES;self.next.enabled=NO;self.next.title=@"Verifying…";
    std::thread([self,current]{@autoreleasepool{
        NSString* game=nil;NSString* error=nil;BOOL hasStoredKey=NO;BOOL deluxe=NO;
        try {auto platform=setup::nativePlatform();auto lock=platform->lock(Path(Support())/"Setup/Install.lock");auto root=setup::assistant::currentGeneration(Path(Support()));
            setup::Control control;control.cancelled=&self->_cancelled;auto edition=setup::assistant::currentEdition(Path(Support()));deluxe=edition==setup::DiscEdition::Deluxe;setup::verifyManifest(root,Path(Manifest(edition)),control,true); // the client writes settings/logs beside the data
            hasStoredKey=setup::assistant::storedKey(Path(Support()))&&(!deluxe||setup::assistant::storedKey(Path(Support()),true));game=Text(root.string());
        }catch(const std::exception& e){error=Text(e.what());}
        dispatch_async(dispatch_get_main_queue(),^{self.working=NO;if(game){self.deluxe=deluxe;self.gameRoot=game;self.step=hasStoredKey?7:3;}[self show];if(error)[self message:@"Existing installation could not be verified" detail:error];});
    }}).detach();
}
- (void)showVoiceSetup {
    // Modelled on retail's BF2VoiceSetup ("BF2 BattleCommo"): level meter, capture volume, threshold, boost.
    try {_voice=setup::assistant::loadVoiceSettings(Path(Support()));}catch(const std::exception& e){Debug(@"voice-load failed: %s",e.what());_voice={};}
    self.captureVolume.doubleValue=_voice.captureVolume;self.captureThreshold.doubleValue=_voice.captureThreshold;self.boost.state=_voice.boost?NSControlStateValueOn:NSControlStateValueOff;
    [self label:@"Battlefield 2 voice chat uses your microphone. Speak to see your level; voice is sent while the level is above the threshold marker." y:214 size:14];
    self.micStatus=[self label:@"" y:184 size:13];self.micStatus.frame=NSMakeRect(20,184,420,22);
    self.micButton=[NSButton buttonWithTitle:@"" target:self action:@selector(micButton:)];self.micButton.frame=NSMakeRect(440,179,140,32);self.micButton.hidden=YES;[self.body addSubview:self.micButton];
    self.meter.level=0;[self.body addSubview:self.meter];
    NSTextField* volume=[self label:@"Capture volume" y:120 size:14];volume.frame=NSMakeRect(20,120,150,22);[self.body addSubview:self.captureVolume];
    NSTextField* threshold=[self label:@"Activation threshold" y:84 size:14];threshold.frame=NSMakeRect(20,84,150,22);[self.body addSubview:self.captureThreshold];
    [self.body addSubview:self.boost];
    BOOL seedOnly=NO;try{seedOnly=setup::assistant::profilesExist(Path(Support()));}catch(const std::exception&){}
    NSTextField* note=[self label:seedOnly?@"Saved as the default for new profiles. Profiles that already exist keep the voice settings from the game’s audio options.":@"Saved as the voice default for your first profile. You can change it later in the game’s audio options." y:4 size:12];note.frame=NSMakeRect(20,4,560,36);note.textColor=NSColor.secondaryLabelColor;
    [self voiceChanged:nil];[self refreshMicrophone];
}
- (setup::assistant::VoiceSettings)voiceSettings {
    setup::assistant::VoiceSettings v=_voice;
    v.captureVolume=self.captureVolume.doubleValue;v.captureThreshold=self.captureThreshold.doubleValue;v.boost=self.boost.state==NSControlStateValueOn;return v;
}
- (void)voiceChanged:(id)sender {
    self.meter.threshold=self.captureThreshold.doubleValue;
    self.captureVolume.toolTip=[NSString stringWithFormat:@"%.0f%%",self.captureVolume.doubleValue*100];self.captureThreshold.toolTip=[NSString stringWithFormat:@"%.0f%%",self.captureThreshold.doubleValue*100];
    if(sender)Debug(@"action voice-changed capture=%.2f threshold=%.2f boost=%d",self.captureVolume.doubleValue,self.captureThreshold.doubleValue,self.boost.state==NSControlStateValueOn);
}
- (void)voiceSetup:(id)sender {[self trace:@"action voice-setup"];if(self.client||self.working)return;self.step=5;[self show];}
- (void)saveVoice {
    Debug(@"action save-voice");
    try {auto platform=setup::nativePlatform();setup::assistant::saveVoiceSettings(Path(Support()),[self voiceSettings],*platform);self.step=6;[self show];}
    catch(const std::exception& e){[self message:@"Voice settings could not be saved" detail:Text(e.what())];}
}
- (void)refreshMicrophone {
    if(self.step!=5)return;
    if(!captureAllowed){self.micStatus.stringValue=@"Microphone check skipped in test mode";self.micButton.hidden=YES;return;}
    AVAuthorizationStatus status=[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    Debug(@"state microphone authorization=%ld",(long)status);
    self.micButton.hidden=NO;
    if(status==AVAuthorizationStatusAuthorized){self.micButton.hidden=YES;[self startMeter];return;}
    if(status==AVAuthorizationStatusNotDetermined){self.micStatus.stringValue=@"Allow microphone access so the game never has to ask mid-match.";self.micButton.title=@"Allow Microphone";return;}
    self.micStatus.stringValue=@"Microphone access is off. Allow Battlefield 2 in System Settings › Privacy & Security › Microphone.";self.micButton.title=@"Open System Settings";
}
- (void)micButton:(id)sender {
    AVAuthorizationStatus status=[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    Debug(@"action mic-button authorization=%ld",(long)status);
    if(status==AVAuthorizationStatusNotDetermined){
        self.micButton.enabled=NO;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted){dispatch_async(dispatch_get_main_queue(),^{Debug(@"state microphone granted=%d",granted);self.micButton.enabled=YES;[self refreshMicrophone];});}];
        return;
    }
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"]];
}
- (void)meterPeak:(double)peak {
    // The client applies the capture volume itself; preview the same scaling here.
    if(self.step==5)self.meter.level=peak*self.captureVolume.doubleValue;
}
- (void)startMeter {
    if(self.engine)return;
    @try {
        AVAudioEngine* engine=[AVAudioEngine new];AVAudioInputNode* input=engine.inputNode;AVAudioFormat* format=[input outputFormatForBus:0];
        if(format.sampleRate<=0||format.channelCount==0)throw [NSException exceptionWithName:@"NoInput" reason:@"no input device" userInfo:nil];
        __weak Assistant* weakSelf=self;
        [input installTapOnBus:0 bufferSize:2048 format:format block:^(AVAudioPCMBuffer* buffer,AVAudioTime* when){
            float peak=0;AVAudioFrameCount frames=buffer.frameLength;
            if(buffer.floatChannelData)for(AVAudioChannelCount c=0;c<buffer.format.channelCount;++c){const float* samples=buffer.floatChannelData[c];for(AVAudioFrameCount i=0;i<frames;++i)peak=fmaxf(peak,fabsf(samples[i]));}
            dispatch_async(dispatch_get_main_queue(),^{[weakSelf meterPeak:peak];});
        }];
        NSError* error=nil;if(![engine startAndReturnError:&error])throw [NSException exceptionWithName:@"NoStart" reason:error.localizedDescription ?: @"audio engine did not start" userInfo:nil];
        self.engine=engine;self.micStatus.stringValue=@"Microphone ready. Speak to test your level.";Debug(@"state meter started rate=%.0f channels=%u",format.sampleRate,format.channelCount);
    } @catch(NSException* exception){
        Debug(@"state meter unavailable: %@",exception.reason);
        self.micStatus.stringValue=@"No microphone was found. You can still save your settings.";
    }
}
- (void)stopMeter {
    if(!self.engine)return;
    Debug(@"state meter stopped");[self.engine.inputNode removeTapOnBus:0];[self.engine stop];self.engine=nil;self.meter.level=0;
}
- (void)play {
    [self trace:@"action play"];
    try {
        if(self.client){Debug(@"play ignored: client running");return;}
        auto platform=setup::nativePlatform();_clientLock=platform->lock(Path(Support())/"Setup/Install.lock");
        auto launch=setup::assistant::clientLaunch(Contents(),Path(Support()));
        if(launch.directory!=Path(self.gameRoot))throw std::runtime_error("The active installation changed. Reopen OpenBF2 to verify it.");
        NSTask* task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:Text(launch.executable.string())];task.currentDirectoryURL=[NSURL fileURLWithPath:Text(launch.directory.string())];
        NSMutableArray* args=[NSMutableArray array];for(const auto& arg:launch.arguments)[args addObject:Text(arg)];task.arguments=args;
        NSMutableDictionary* env=[NSProcessInfo.processInfo.environment mutableCopy];
        if(testHome)env[@"HOME"]=testHome; // ClientPaths resolves Registry.con and profiles from HOME.
        env[@"OPENBF2_DATA_ROOT"]=Text(launch.directory.string());env[@"OPENBF2_SUPPORT_ROOT"]=Support();task.environment=env;
        task.terminationHandler=^(NSTask* child){dispatch_async(dispatch_get_main_queue(),^{Debug(@"state client stopped status=%d",child.terminationStatus);self.client=nil;self->_clientLock.reset();[self show];if(child.terminationStatus!=0)[self message:@"Battlefield 2 stopped" detail:[NSString stringWithFormat:@"The client exited with status %d.",child.terminationStatus]];});};
        NSError* error=nil;if(![task launchAndReturnError:&error])throw std::runtime_error(std::string(error.localizedDescription.UTF8String));
        self.client=task;self.next.enabled=NO;Debug(@"state client started");
    }catch(const std::exception& e){_clientLock.reset();[self message:@"Could not start Battlefield 2" detail:Text(e.what())];}
}
- (BOOL)windowShouldClose:(NSWindow*)sender {[self trace:@"action close"];[self stopMeter];if(self.client){[self message:@"Battlefield 2 is running" detail:@"Quit the game before closing this window."];return NO;}if(self.working){_cancelled=true;Debug(@"state cancellation requested by close");return NO;}[NSApp terminate:nil];return YES;}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {[self trace:@"action quit"];if(self.client)return NSTerminateCancel;if(self.working){_cancelled=true;Debug(@"state cancellation requested by quit");return NSTerminateCancel;}return NSTerminateNow;}
@end
int main(int argc,const char** argv) {
    if(argc==2&&std::string(argv[1])=="--test-disc-selection") {
        const char* iso=std::getenv("SETUP_TEST_ISO");
        if(!iso||!*iso){std::cout<<"SKIP: set SETUP_TEST_ISO to the owner's DVD ISO path\n";return 77;}
        discActionTest=YES;
    }
    if(discActionTest||(argc==3&&std::string(argv[1])=="--smoke-test"))captureAllowed=NO;
    if(!discActionTest&&argc!=1&&!(argc==3&&(std::string(argv[1])=="--smoke-test"||std::string(argv[1])=="--test-home")))return 2;
    if(argc==3&&std::string(argv[1])=="--test-home") {
        auto path=setup::fs::absolute(argv[2]).lexically_normal();auto allowed=setup::fs::canonical(setup::fs::path(SETUP_UI_TEST_ROOT));
        if(path.parent_path()!=allowed||setup::fs::is_symlink(setup::fs::symlink_status(path)))return 2;
        setup::fs::create_directories(path);testHome=Text(path.string());
    }
    if(argc==3&&std::string(argv[1])=="--smoke-test") {
        auto path=setup::fs::absolute(argv[2]).lexically_normal();auto allowed=setup::fs::canonical(setup::fs::path(SETUP_UI_TEST_ROOT));
        if(path.parent_path()!=allowed||setup::fs::exists(path))return 2;
        setup::fs::create_directory(path);smokeDirectory=Text(path.string());
        // Render from defaults: never read the owner's real Application Support (voice settings, installation).
        setup::fs::create_directory(path/"home");testHome=Text((path/"home").string());
    }
    @autoreleasepool {Debug(@"startup test-home=%@ smoke=%d disc-action-test=%d",testHome,smokeDirectory!=nil,discActionTest);NSApplication* app=NSApplication.sharedApplication;[app setActivationPolicy:discActionTest?NSApplicationActivationPolicyProhibited:NSApplicationActivationPolicyRegular];Assistant* assistant=[Assistant new];app.delegate=assistant;[app run];}return 0;
}
