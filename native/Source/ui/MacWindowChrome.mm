#include "MacWindowChrome.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <vector>

/** Puts the traffic lights back whenever AppKit moves them. AppKit re-lays the title
    bar on its own schedule — after the first display, on resize, entering and
    leaving full screen, key/main changes — and each time it puts the buttons back at
    their default spot. Watching each button's frame catches every one of those
    without guessing which notification AppKit will send. */
@interface SEButtonPlacer : NSObject
@property (nonatomic, assign) NSWindow* window;
@property (nonatomic, assign) CGFloat centreY;
@property (nonatomic, assign) std::vector<CGFloat> centresX;
@property (nonatomic, assign) BOOL placing;
- (void) place;
@end

@implementation SEButtonPlacer
- (void) place
{
    if (self.placing || self.window == nil) return;
    self.placing = YES;   // our own setFrameOrigin comes back through KVO

    const NSWindowButton kinds[] = { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton };
    for (size_t i = 0; i < 3 && i < self.centresX.size(); ++i)
    {
        NSButton* button = [self.window standardWindowButton: kinds[i]];
        NSView* parent = [button superview];
        if (button == nil || parent == nil) continue;

        // Window coordinates (origin bottom-left), converted into the superview,
        // whose own geometry AppKit is free to change.
        const NSSize size = [button frame].size;
        const CGFloat windowHeight = [[self.window contentView] frame].size.height;
        const NSPoint inWindow = NSMakePoint (self.centresX[i] - size.width * 0.5,
                                              windowHeight - self.centreY - size.height * 0.5);
        const NSPoint wanted = [parent convertPoint: inWindow fromView: nil];
        if (! NSEqualPoints ([button frame].origin, wanted))
            [button setFrameOrigin: wanted];
    }

    self.placing = NO;
}

- (void) observeValueForKeyPath: (NSString*) keyPath ofObject: (id) object
                         change: (NSDictionary*) change context: (void*) context
{
    juce::ignoreUnused (keyPath, object, change, context);
    // After AppKit has finished its own pass, not in the middle of it.
    dispatch_async (dispatch_get_main_queue(), ^{ [self place]; });
}
@end

namespace os8::chrome {

namespace {

NSWindow* nsWindowOf (juce::Component& c)
{
    if (auto* peer = c.getPeer())
        if (auto* view = (NSView*) peer->getNativeHandle())
            return [view window];
    return nil;
}

} // namespace

void adoptAppTopBar (juce::Component& c, float barCentreY, std::initializer_list<float> buttonCentresX)
{
    NSWindow* window = nsWindowOf (c);
    if (window == nil) return;

    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;
    // Dragging is the top bar's job (beginWindowDrag); content must not move the
    // window on a click meant for a sound Window.
    window.movableByWindowBackground = NO;

    // One placer per window, kept alive by the window itself (associated object)
    // for as long as the window lives.
    static char placerKey;
    SEButtonPlacer* placer = [[SEButtonPlacer alloc] init];
    placer.window = window;
    placer.centreY = barCentreY;
    std::vector<CGFloat> xs;
    for (float x : buttonCentresX) xs.push_back ((CGFloat) x);
    placer.centresX = xs;
    objc_setAssociatedObject (window, &placerKey, placer, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    for (NSWindowButton kind : { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton })
        if (NSButton* button = [window standardWindowButton: kind])
            [button addObserver: placer forKeyPath: @"frame" options: 0 context: nullptr];

    // AppKit also lays the title bar out through constraints, which move the
    // buttons without telling KVO. NSWindowDidUpdate comes after every event the
    // window handles; placing is a comparison when nothing has moved.
    [[NSNotificationCenter defaultCenter] addObserver: placer
                                             selector: @selector (place)
                                                 name: NSWindowDidUpdateNotification
                                               object: window];

    [placer place];
    [placer release];   // the association holds it now
}

void beginWindowDrag (juce::Component& c)
{
    NSWindow* window = nsWindowOf (c);
    NSEvent* event = [NSApp currentEvent];
    if (window != nil && event != nil && [event type] == NSEventTypeLeftMouseDown)
        [window performWindowDragWithEvent: event];
}

void titleBarDoubleClicked (juce::Component& c)
{
    NSWindow* window = nsWindowOf (c);
    if (window == nil) return;

    // System Settings ▸ Desktop & Dock ▸ "Double-click a window's title bar to".
    NSString* action = [[NSUserDefaults standardUserDefaults] stringForKey: @"AppleActionOnDoubleClick"];
    if ([action isEqualToString: @"Minimize"])  [window performMiniaturize: nil];
    else if (! [action isEqualToString: @"None"]) [window performZoom: nil];
}

} // namespace os8::chrome
