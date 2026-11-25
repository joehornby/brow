#import "Page.h"
#import "FocusColor.h"
#import "Slider.h"
#import <CoreImage/CoreImage.h>
#import <QuartzCore/QuartzCore.h>
#import <WebKit/WebKit.h>
#import <dlfcn.h>

extern size_t brow_count(void);
extern uint64_t brow_current(void), brow_id(size_t);
extern bool brow_new(bool, bool);
extern void brow_select(size_t), brow_close_tab(void), brow_free(char *);
extern char *brow_destination(const char *);

@protocol CrAppProtocol
- (BOOL)isHandlingSendEvent;
@end
@protocol CrAppControlProtocol <CrAppProtocol>
- (void)setHandlingSendEvent:(BOOL)value;
@end
@protocol CefAppProtocol <CrAppControlProtocol>
@end

static BOOL dragPageWindow(NSEvent *event);
static BOOL prepareQuit(void);
static void dismissPillTip(void);
static void moveViewport(NSUInteger from, NSUInteger to);
static BOOL reducedMotion(void);

@interface BrowApplication : NSApplication <CefAppProtocol>
@property(getter=isHandlingSendEvent) BOOL handlingSendEvent;
@end
@implementation BrowApplication
- (void)terminate:(id)sender {
    if (!prepareQuit())
        [super terminate:sender];
}
- (void)sendEvent:(NSEvent *)event {
    BOOL previous = self.handlingSendEvent;
    self.handlingSendEvent = YES;
    @try {
        if (event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeKeyDown) dismissPillTip();
        if (event.type == NSEventTypeLeftMouseDown && dragPageWindow(event)) return;
        if (event.type == NSEventTypeKeyDown && !self.modalWindow) {
            NSEventModifierFlags flags = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
            NSString *key = event.charactersIgnoringModifiers.lowercaseString;
            BOOL reserved = (flags == NSEventModifierFlagCommand && [@"ltwrq[],123456789\\" containsString:key]) ||
                (flags == (NSEventModifierFlagOption | NSEventModifierFlagCommand) && [@"cf" containsString:key]) ||
                ((flags == (NSEventModifierFlagShift | NSEventModifierFlagCommand) ||
                  flags == (NSEventModifierFlagControl | NSEventModifierFlagCommand)) && [key isEqual:@"f"]);
            if (reserved && key.length == 1 && [self.mainMenu performKeyEquivalent:event])
                return;
        }
        [super sendEvent:event];
    } @finally {
        self.handlingSendEvent = previous;
    }
}
@end

static void openLink(NSString *url, bool chromium);
static NSView<BrowPage> *createPage(bool chromium);

@interface WebPage : WKWebView <BrowPage, WKUIDelegate>
@end
static char webNavigationContext;
@implementation WebPage
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
    if (context == &webNavigationContext)
        [NSNotificationCenter.defaultCenter postNotificationName:BrowNavigationChanged object:self];
    else [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
}
- (NSString *)address {
    return self.URL.absoluteString ?: @"";
}
- (NSString *)pageTitle {
    return self.title ?: @"";
}
- (void)navigate:(NSString *)address {
    [self loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:address]]];
}
- (void)refresh {
    [self reload];
}
- (BOOL)loading { return self.isLoading; }
- (BOOL)emulateViewport:(NSSize)size { return NO; }
- (void)samplePage:(void (^)(NSImage *))completion {
    WKSnapshotConfiguration *options = [WKSnapshotConfiguration new];
    options.snapshotWidth = @48;
    [self takeSnapshotWithConfiguration:options completionHandler:^(NSImage *image, NSError *error) {
        completion(error ? nil : image);
    }];
}
- (void)focus {
    [self.window makeFirstResponder:self];
}
- (void)dispose {
    for (NSString *key in @[@"URL", @"canGoBack", @"canGoForward", @"loading"])
        [self removeObserver:self forKeyPath:key context:&webNavigationContext];
    [self stopLoading];
}
- (WKWebView *)webView:(WKWebView *)view
    createWebViewWithConfiguration:(WKWebViewConfiguration *)config
               forNavigationAction:(WKNavigationAction *)action
                    windowFeatures:(WKWindowFeatures *)features {
    if (!action.targetFrame)
        openLink(action.request.URL.absoluteString, false);
    return nil;
}
@end

@interface BrowWindow : NSWindow
@end
@implementation BrowWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

@interface PillPanel : NSPanel
@end
@implementation PillPanel
- (BOOL)canBecomeKeyWindow { return YES; }
@end

@interface Tile : NSView
@property NSView<BrowPage> *page;
@property NSString *address;
@property CFTimeInterval followed;
@property BOOL sawLoading;
@property CALayer *caption;
@property NSInteger preset;
@end
@implementation Tile
- (void)follow:(NSString *)address {
    self.followed = CACurrentMediaTime();
    self.sawLoading = NO;
    if (!address) return;
    self.address = address;
    [self.page navigate:address];
}
- (BOOL)led {
    NSString *now = self.page.address;
    BOOL changed = now.length && ![now isEqual:self.address];
    if (changed) self.address = now;
    BOOL loading = self.page.loading;
    if (!self.sawLoading && !loading && CACurrentMediaTime() - self.followed > 3) self.followed = 0;
    if (!self.followed) return changed;
    if (loading) self.sawLoading = YES;
    else if (self.sawLoading || changed) self.followed = 0;
    return NO;
}
@end

static void arrangeViewports(const NSSize *sizes, NSUInteger count, NSRect area, CGFloat gap, CGFloat foot, NSRect *out) {
    double best = -1;
    NSRect trial[count];
    for (NSUInteger breaks = 0; breaks < 1u << (count - 1); breaks++) {
        double ratio[count], inverse = 0, slack = 0, sum = 0;
        NSUInteger last[count], rows = 0, start = 0;
        for (NSUInteger i = 0; i < count; i++) {
            sum += sizes[i].width / sizes[i].height;
            if (i + 1 < count && !(breaks >> i & 1)) continue;
            inverse += 1 / sum;
            slack += (i - start) / sum;
            ratio[rows] = sum;
            last[rows++] = i;
            sum = 0;
            start = i + 1;
        }
        double spacing = gap * (rows - 1) + foot * rows - gap * slack;
        double width = MIN(NSWidth(area), (NSHeight(area) - spacing) / inverse);
        double covered = 0, y = NSMidY(area) + (width * inverse + spacing) / 2;
        NSUInteger i = 0;
        for (NSUInteger row = 0; row < rows; row++) {
            double height = (width - gap * (last[row] - i)) / ratio[row], x = NSMidX(area) - width / 2;
            for (; i <= last[row]; i++) {
                double span = height * sizes[i].width / sizes[i].height;
                trial[i] = NSMakeRect(round(x), round(y - height), round(x + span) - round(x),
                                      round(y) - round(y - height));
                covered += span * height;
                x += span + gap;
            }
            y -= height + foot + gap;
        }
        if (width > 0 && covered > best) {
            best = covered;
            memcpy(out, trial, sizeof(trial));
        }
    }
}

static CATransform3D shown(CALayer *layer, NSRect at) {
    CGRect now = layer.frame;
    CGPoint pin = layer.position;
    CGFloat sx = NSWidth(at) / NSWidth(now), sy = NSHeight(at) / NSHeight(now);
    CATransform3D shift = CATransform3DMakeTranslation(NSMinX(at) - pin.x - sx * (NSMinX(now) - pin.x),
                                                       NSMinY(at) - pin.y - sy * (NSMinY(now) - pin.y), 0);
    return CATransform3DConcat(layer.transform, CATransform3DConcat(CATransform3DMakeScale(sx, sy, 1), shift));
}
static void spring(CALayer *layer, NSString *path, id from, id to, NSString *key) {
    CASpringAnimation *a = [[CASpringAnimation alloc] initWithPerceptualDuration:.36 bounce:.14];
    a.keyPath = path;
    a.fromValue = from;
    a.toValue = to;
    a.duration = a.settlingDuration;
    if ([key isEqual:@"lift"]) {
        a.fillMode = kCAFillModeForwards;
        a.removedOnCompletion = NO;
    }
    [layer addAnimation:a forKey:key];
}

@interface Surface : NSView
@property BOOL interactive;
@property NSArray<NSValue *> *viewports;
@property NSArray<NSString *> *titles;
@property Tile *held;
@property BOOL lifted;
@property NSPoint grip;
@property NSRect home, foot;
@property NSInteger slot;
@property CALayer *marker;
- (void)arrange;
@end
@implementation Surface
- (NSView *)hitTest:(NSPoint)point {
    return self.interactive ? [super hitTest:point] : nil;
}
- (void)resizeSubviewsWithOldSize:(NSSize)size {
    [self arrange];
}
- (NSRect)handle:(Tile *)tile {
    BOOL usable = self.viewports.count > 1 && tile.caption && !tile.caption.hidden;
    return usable ? NSInsetRect(tile.caption.frame, -6, -3) : NSZeroRect;
}
- (void)resetCursorRects {
    for (Tile *tile in self.subviews)
        [self addCursorRect:[self handle:tile] cursor:NSCursor.openHandCursor];
}
- (void)mouseDown:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    for (Tile *tile in self.subviews)
        if (NSPointInRect(point, [self handle:tile])) self.held = tile;
    if (!self.held) return [super mouseDown:event];
    self.grip = point;
    self.slot = -1;
    self.home = self.held.frame;
    self.foot = self.held.caption.frame;
}
- (void)mouseDragged:(NSEvent *)event {
    Tile *tile = self.held;
    if (!tile) return [super mouseDragged:event];
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    CGFloat dx = point.x - self.grip.x, dy = point.y - self.grip.y;
    if (!self.lifted) {
        if (hypot(dx, dy) < 4) return;
        self.lifted = YES;
        for (Tile *other in self.subviews) other.layer.zPosition = other.caption.zPosition = other == tile;
        [tile.layer removeAnimationForKey:@"settle"];
        [tile.caption removeAnimationForKey:@"settle"];
        NSRect raised = NSInsetRect(self.home, -NSWidth(self.home) * .012, -NSHeight(self.home) * .012);
        if (!reducedMotion())
            spring(tile.layer, @"transform", [NSValue valueWithCATransform3D:tile.layer.transform],
                   [NSValue valueWithCATransform3D:shown(tile.layer, raised)], @"lift");
    }
    [NSCursor.closedHandCursor set];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [tile setFrameOrigin:NSMakePoint(NSMinX(self.home) + dx, NSMinY(self.home) + dy)];
    tile.caption.frame = NSOffsetRect(self.foot, dx, dy);
    [CATransaction commit];
    [self markSlotNear:NSMakePoint(NSMidX(self.home) + dx, NSMidY(self.home) + dy)];
    [self.window invalidateShadow];
}
- (void)mouseUp:(NSEvent *)event {
    Tile *tile = self.held;
    if (!tile) return [super mouseUp:event];
    if (!self.lifted) {
        self.held = nil;
        return;
    }
    NSArray<Tile *> *tiles = self.subviews.copy;
    NSUInteger from = [tiles indexOfObjectIdenticalTo:tile];
    NSInteger slot = self.slot;
    NSMutableArray<NSValue *> *before = [NSMutableArray array];
    for (Tile *each in tiles) {
        NSRect f = each.frame;
        if (each == tile && [each.layer animationForKey:@"lift"])
            f = NSInsetRect(f, -NSWidth(f) * .012, -NSHeight(f) * .012);
        [before addObject:[NSValue valueWithRect:f]];
        [before addObject:[NSValue valueWithPoint:each.caption.position]];
    }
    if (slot >= 0) moveViewport(from, slot > (NSInteger)from ? slot - 1 : slot);
    else [self arrange];
    [NSCursor.arrowCursor set];
    if (reducedMotion()) return;
    [CATransaction begin];
    [CATransaction setCompletionBlock:^{ [self.window invalidateShadow]; }];
    for (NSUInteger i = 0; i < tiles.count; i++) {
        CALayer *layer = tiles[i].layer, *caption = tiles[i].caption;
        spring(layer, @"transform", [NSValue valueWithCATransform3D:shown(layer, before[i * 2].rectValue)],
               [NSValue valueWithCATransform3D:layer.transform], @"settle");
        spring(caption, @"position", before[i * 2 + 1], [NSValue valueWithPoint:caption.position], @"settle");
    }
    [CATransaction commit];
}
- (void)markSlotNear:(NSPoint)point {
    NSArray<Tile *> *tiles = self.subviews;
    NSUInteger from = [tiles indexOfObjectIdenticalTo:self.held];
    CGFloat best = CGFLOAT_MAX;
    NSInteger slot = -1;
    NSRect line = NSZeroRect;
    for (NSUInteger i = 0; i < tiles.count; i++) {
        NSRect f = i == from ? self.home : tiles[i].frame;
        CGFloat away = MAX(0, MAX(NSMinY(f) - point.y, point.y - NSMaxY(f)));
        for (NSUInteger side = 0; side < 2; side++) {
            CGFloat x = side ? NSMaxX(f) + 8 : NSMinX(f) - 8, distance = hypot(point.x - x, away);
            if (distance >= best) continue;
            best = distance;
            slot = i + side;
            line = NSMakeRect(MAX(2, MIN(NSWidth(self.bounds) - 5, x - 1.5)), NSMinY(f), 3, NSHeight(f));
        }
    }
    if (slot == (NSInteger)from || slot == (NSInteger)from + 1) slot = -1;
    [self mark:slot at:line];
}
- (void)mark:(NSInteger)slot at:(NSRect)line {
    if (!self.marker) {
        CALayer *marker = self.marker = [CALayer layer];
        marker.backgroundColor = [NSColor colorWithSRGBRed:.25 green:.62 blue:1 alpha:1].CGColor;
        marker.cornerRadius = 1.5;
        marker.shadowColor = NSColor.systemBlueColor.CGColor;
        marker.shadowOffset = CGSizeZero;
        marker.shadowRadius = 9;
        marker.shadowOpacity = 1;
        marker.zPosition = 2;
        marker.opacity = 0;
        [self.layer addSublayer:marker];
    }
    BOOL was = self.slot >= 0;
    self.slot = slot;
    [CATransaction begin];
    [CATransaction setDisableActions:!was || reducedMotion()];
    [CATransaction setAnimationDuration:.16];
    if (slot >= 0) self.marker.frame = line;
    [CATransaction commit];
    [CATransaction begin];
    [CATransaction setDisableActions:reducedMotion()];
    [CATransaction setAnimationDuration:.12];
    self.marker.opacity = slot >= 0;
    [CATransaction commit];
}
- (void)arrange {
    if (self.held) {
        [self.held.layer removeAnimationForKey:@"lift"];
        self.held = nil;
        self.lifted = NO;
        [self mark:-1 at:NSZeroRect];
    }
    NSArray<Tile *> *tiles = self.subviews;
    NSUInteger count = MIN(tiles.count, self.viewports.count);
    if (!tiles.count || NSWidth(self.bounds) < 64 || NSHeight(self.bounds) < 64) return;
    NSSize sizes[count + 1];
    NSRect rects[count + 1];
    sizes[0] = self.bounds.size;
    rects[0] = self.bounds;
    for (NSUInteger i = 0; i < count; i++)
        sizes[i] = self.viewports[i].sizeValue;
    if (count) arrangeViewports(sizes, count, self.bounds, 16, 26, rects);
    for (NSUInteger i = 0; i < MAX(count, 1); i++) {
        Tile *tile = tiles[i];
        NSRect inner = {NSZeroPoint, rects[i].size};
        tile.frame = rects[i];
        tile.bounds = inner;
        tile.page.frame = inner;
        if (![tile.page emulateViewport:count ? sizes[i] : NSZeroSize]) inner.size = sizes[i];
        tile.bounds = inner;
        tile.page.frame = inner;
        tile.layer.cornerRadius = count ? 16 * NSWidth(inner) / NSWidth(rects[i]) : 0;
        [self caption:tile text:count ? self.titles[i] : nil under:rects[i]];
    }
    [self.window invalidateShadow];
    [self.window invalidateCursorRectsForView:self];
}
- (void)caption:(Tile *)tile text:(NSString *)text under:(NSRect)rect {
    if (!tile.caption) {
        tile.caption = [CALayer layer];
        tile.caption.backgroundColor = [NSColor colorWithWhite:0 alpha:.55].CGColor;
        tile.caption.cornerRadius = 10;
        [tile.caption addSublayer:[CATextLayer layer]];
        [self.layer addSublayer:tile.caption];
    }
    NSDictionary *style = @{NSFontAttributeName: [NSFont systemFontOfSize:11 weight:NSFontWeightMedium],
                            NSForegroundColorAttributeName: NSColor.whiteColor};
    NSString *shown = text ?: @"";
    NSSize size = [shown sizeWithAttributes:style];
    if (size.width + 16 > NSWidth(rect)) {
        shown = [shown componentsSeparatedByString:@" · "].firstObject;
        size = [shown sizeWithAttributes:style];
    }
    CATextLayer *label = (CATextLayer *)tile.caption.sublayers.firstObject;
    CGFloat width = ceil(size.width) + 16;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    label.string = [[NSAttributedString alloc] initWithString:shown attributes:style];
    label.contentsScale = self.window.backingScaleFactor ?: 2;
    label.frame = CGRectMake(8, round((20 - ceil(size.height)) / 2), ceil(size.width), ceil(size.height));
    tile.caption.frame = CGRectMake(round(NSMidX(rect) - width / 2), NSMinY(rect) - 26, width, 20);
    tile.caption.hidden = !text || width > NSWidth(rect);
    [CATransaction commit];
}
- (void)add:(NSView<BrowPage> *)page {
    Tile *tile = [[Tile alloc] initWithFrame:self.bounds];
    tile.preset = -1;
    tile.wantsLayer = YES;
    tile.layer.masksToBounds = YES;
    tile.layer.cornerCurve = kCACornerCurveContinuous;
    tile.page = page;
    page.autoresizingMask = NSViewNotSizable;
    [tile addSubview:page];
    [self addSubview:tile];
}
- (void)drop:(Tile *)tile {
    [tile.page dispose];
    [tile.caption removeFromSuperlayer];
    [tile removeFromSuperview];
}
- (void)discard:(NSUInteger)keep {
    while (self.subviews.count > keep) [self drop:self.subviews.lastObject];
}
@end

@interface Overlay : NSView
@property(copy) void (^dismiss)(void);
@end
@implementation Overlay
- (void)mouseDown:(NSEvent *)event {
    self.dismiss();
}
@end

@interface Browser : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTextFieldDelegate, NSToolbarDelegate, NSPopoverDelegate>
@property NSWindow *window;
@property BOOL inFullScreen, focusTransition;
@property NSView *deck;
@property NSWindow *preferences;
@property NSPanel *pill;
@property NSPanel *pillTip;
@property NSButton *tipAnchor;
@property CFTimeInterval lastTipTime;
- (void)hidePillTip;
@property NSView *pillControls;
@property NSTextField *pillEditor;
@property NSButton *pillAddress, *pillBack, *pillForward, *pillReload;
@property NSPopover *toolPopover;
@property NSButton *popoverAnchor;
@property NSRectEdge popoverEdge;
@property NSArray<NSTextField *> *quickFields;
@property NSArray<NSNumber *> *viewports;
- (void)syncViewports;
- (void)moveViewport:(NSUInteger)from to:(NSUInteger)to;
- (void)positionPill;
- (void)applyFields:(NSArray<NSTextField *> *)fields centre:(BOOL)centre;
@property NSString *section;
@property NSArray<NSTextField *> *geometryFields;
@property BOOL focusMode;
@property NSRect preferredFrame;
@property CAGradientLayer *backdrop;
@property NSTimer *sampleTimer;
@property NSArray *lastSampleKey;
@property NSUInteger sampleVersion;
@property BOOL hasSitePalette;
@property CFTimeInterval nextPaletteSample;
- (void)syncGeometryFields;
- (void)applyGeometry:(NSButton *)centre;
- (void)refreshBackdrop;
@property NSMutableDictionary<NSNumber *, NSView<BrowPage> *> *pages;
@property Overlay *overlay;
@property NSView *card, *body;
@property NSTextField *field;
@property NSArray<NSNumber *> *matches;
@property NSInteger selected;
@property NSUInteger generation;
@property void *plugin;
@property BOOL quitting, readyToQuit, owesTerminationReply;
@end
static Browser *app;
static void dismissPillTip(void) { if (app.tipAnchor) [app hidePillTip]; }
static void moveViewport(NSUInteger from, NSUInteger to) { [app moveViewport:from to:to]; }
static BOOL reducedMotion(void) {
#ifdef BROW_UI_TEST
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"TestReduceMotion"]) return YES;
#endif
    return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
}
static NSView<BrowPage> *page(void) { return app.pages[@(brow_current())]; }
static Surface *surfaceOf(NSView<BrowPage> *p) { return (Surface *)p.superview.superview; }
static NSArray<NSView<BrowPage> *> *currentPages(void) { return [surfaceOf(page()).subviews valueForKey:@"page"]; }
static void navigate(NSString *address) {
    for (Tile *tile in surfaceOf(page()).subviews) [tile follow:address];
}
static void command(SEL action) {
    for (Tile *tile in surfaceOf(page()).subviews) {
        NSView<BrowPage> *p = tile.page;
        BOOL able = action == @selector(goBack) ? p.canGoBack : action == @selector(goForward) ? p.canGoForward : p.address.length > 0;
        if (!able) continue;
        [tile follow:nil];
        [@[p] makeObjectsPerformSelector:action];
    }
}
static NSComparisonResult presetOrder(__kindof NSView *a, __kindof NSView *b, void *context) {
    NSArray<NSNumber *> *order = (__bridge NSArray *)context;
    return [@([order indexOfObject:@(((Tile *)a).preset)]) compare:@([order indexOfObject:@(((Tile *)b).preset)])];
}
static NSArray<NSArray *> *viewportPresets(void) {
    return @[@[@"iPhone", @402, @874], @[@"iPhone Duo", @466, @678], @[@"iPhone Duo, open", @669, @951],
             @[@"iPad", @820, @1180], @[@"Desktop", @1440, @900]];
}
static BOOL chromium(void) { return ![page() isKindOfClass:WebPage.class]; }
static BOOL defaultChromium(void) { return [NSUserDefaults.standardUserDefaults boolForKey:@"chromium"]; }
static NSString *pluginPath(void) {
    return [NSBundle.mainBundle.privateFrameworksPath stringByAppendingPathComponent:@"BrowChromium.dylib"];
}
static void item(NSMenu *menu, NSString *title, SEL action, NSString *key, id target) {
    NSMenuItem *i = [menu addItemWithTitle:title action:action keyEquivalent:key];
    i.target = target;
}

#ifdef BROW_UI_TEST
static void smoke(void);
#endif
static BOOL shouldStartWindowDrag(NSEvent *event) {
    if (!app || event.window != app.window || app.window.attachedSheet ||
        (app.window.styleMask & NSWindowStyleMaskFullScreen)) return NO;
    NSRect bounds = app.deck.bounds;
    NSPoint point = [app.deck convertPoint:event.locationInWindow fromView:nil];
    if (!NSPointInRect(point, bounds)) return NO;

    NSEventModifierFlags modifiers = event.modifierFlags &
        (NSEventModifierFlagShift | NSEventModifierFlagControl | NSEventModifierFlagOption | NSEventModifierFlagCommand);
    if (modifiers == NSEventModifierFlagOption && !app.overlay) return YES;
    CGFloat depth = NSMaxY(bounds) - point.y;
    if (modifiers || depth < 4 || depth > 32 || point.x < 5 || point.x > NSMaxX(bounds) - 5 ||
        (app.card && NSPointInRect([app.card convertPoint:event.locationInWindow fromView:nil], app.card.bounds)))
        return NO;

    NSEvent *next = [NSApp nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp
                                      untilDate:NSDate.distantFuture inMode:NSEventTrackingRunLoopMode dequeue:NO];
    return next.type == NSEventTypeLeftMouseDragged;
}

static BOOL dragPageWindow(NSEvent *event) {
    if (!shouldStartWindowDrag(event)) return NO;
    [app.window performWindowDragWithEvent:event];
    return YES;
}

static BOOL prepareQuit(void) {
    if (!app || app.readyToQuit) return NO;
    if (app.quitting) return YES;
    app.quitting = YES;
    [app.toolPopover close];
    [app.pill orderOut:nil];
    [app.preferences orderOut:nil];
    [app.sampleTimer invalidate];
    [app.window orderOut:nil];
    CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, ^{
        [app.window makeFirstResponder:nil];
        @autoreleasepool {
            for (Surface *surface in app.deck.subviews.copy) {
                [surface discard:0];
                [surface removeFromSuperview];
            }
            [app.pages removeAllObjects];
        }
        void (^finish)(void) = ^{
            app.readyToQuit = YES;
            if (app.owesTerminationReply) [NSApp replyToApplicationShouldTerminate:YES];
            else [NSApp terminate:nil];
        };
        if (app.plugin) {
            void (*shutdown)(void (^)(void)) = dlsym(app.plugin, "brow_chromium_shutdown");
            shutdown(finish);
        } else {
            finish();
        }
    });
    CFRunLoopWakeUp(CFRunLoopGetMain());
    return YES;
}

static BOOL tileBoundary(CGFloat offset, CGFloat length) {
    const CGFloat positions[] = {0, 0.25, 1.0/3, 0.5, 2.0/3, 0.75, 1};
    for (size_t i = 0; i < sizeof(positions)/sizeof(positions[0]); ++i)
        if (fabs(offset - length * positions[i]) <= 16) return YES;
    return NO;
}

static BOOL tiledFrame(NSRect frame, NSRect screen) {
    if (NSIsEmptyRect(screen)) return NO;
    return tileBoundary(NSMinX(frame)-NSMinX(screen), NSWidth(screen)) &&
           tileBoundary(NSMaxX(frame)-NSMinX(screen), NSWidth(screen)) &&
           tileBoundary(NSMinY(frame)-NSMinY(screen), NSHeight(screen)) &&
           tileBoundary(NSMaxY(frame)-NSMinY(screen), NSHeight(screen));
}

static NSArray<NSString *> *iconNames(void) {
    return @[@"Ink", @"Candy", @"Neon", @"Prism"];
}

static NSImage *iconImage(NSString *name) {
    NSURL *url = [NSBundle.mainBundle URLForResource:name withExtension:@"png" subdirectory:@"Icons"];
    return url ? [[NSImage alloc] initWithContentsOfURL:url] : nil;
}

static NSString *selectedIcon(void) {
    NSString *saved = [NSUserDefaults.standardUserDefaults stringForKey:@"appIcon"];
    return saved && [iconNames() containsObject:saved] ? saved : @"Ink";
}

@implementation Browser
- (void)updateWindowCorners {
    CGFloat radius = 24;
    if (self.inFullScreen || (self.window.styleMask & NSWindowStyleMaskFullScreen)) {
        radius = 0;
    } else if (tiledFrame(self.window.frame, self.window.screen.visibleFrame)) {
        if (@available(macOS 26, *)) radius = 16;
        else radius = 10;
    }
    BOOL split = self.viewports.count > 0;
    if (split) radius = 0;
    CGColorRef fill = (split ? NSColor.clearColor : NSColor.windowBackgroundColor).CGColor;
    CALayer *layer = self.window.contentView.layer;
    if (layer.cornerRadius == radius && CGColorEqualToColor(layer.backgroundColor, fill)) return;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    layer.cornerRadius = radius;
    layer.backgroundColor = fill;
    [CATransaction commit];
    [self.window invalidateShadow];
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)visible {
    if (self.quitting) return NO;
    [self.window deminiaturize:nil];
    [self.window makeKeyAndOrderFront:nil];
    [self palette:nil];
    return NO;
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    NSApp.applicationIconImage = iconImage(selectedIcon());
    self.pages = [NSMutableDictionary dictionary];
    NSUInteger style = NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable;
    NSRect usable = NSScreen.mainScreen.visibleFrame;
    CGFloat width = MIN(1200, NSWidth(usable)), height = MIN(800, NSHeight(usable));
    NSRect initial = NSMakeRect(round(NSMidX(usable) - width / 2), round(NSMidY(usable) - height / 2), width, height);
    self.window = [[BrowWindow alloc] initWithContentRect:initial
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.delegate = self;
    self.window.title = @"Brow";
    self.window.opaque = NO;
    self.window.backgroundColor = NSColor.clearColor;
    self.window.hasShadow = YES;
    self.window.collectionBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;
    NSView *root = self.window.contentView;
    root.wantsLayer = YES;
    root.layer.cornerCurve = kCACornerCurveContinuous;
    root.layer.masksToBounds = YES;
    root.layer.backgroundColor = NSColor.windowBackgroundColor.CGColor;
    self.window.minSize = NSMakeSize(360, 280);
    self.window.releasedWhenClosed = NO;
    self.backdrop = [CAGradientLayer layer];
    self.backdrop.startPoint = CGPointMake(0, 1);
    self.backdrop.endPoint = CGPointMake(1, 0);
    self.backdrop.hidden = YES;
    [root.layer addSublayer:self.backdrop];
    self.deck = [[NSView alloc] initWithFrame:self.window.contentView.bounds];
    self.deck.wantsLayer = YES;
    self.deck.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [self.window.contentView addSubview:self.deck];
    self.window.restorable = NO;
    [self.window makeKeyAndOrderFront:nil];
    [self updateWindowCorners];
    NSMenu *main = [NSMenu new];
    NSMenu *application = [NSMenu new], *tabs = [[NSMenu alloc] initWithTitle:@"Tab"],
           *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    for (NSMenu *menu in @[ application, tabs, edit ]) {
        NSMenuItem *root = [NSMenuItem new];
        [main addItem:root];
        root.submenu = menu;
    }
    item(application, @"Settings…", @selector(settings:), @",", self);
    item(application, @"Quit Brow", @selector(terminate:), @"q", NSApp);
    item(tabs, @"Address and Tabs…", @selector(palette:), @"l", self);
    item(tabs, @"New Tab", @selector(newTab:), @"t", self);
    item(tabs, @"Close Tab", @selector(closeTab:), @"w", self);
    item(tabs, @"Back", @selector(back:), @"[", self);
    item(tabs, @"Forward", @selector(forward:), @"]", self);
    item(tabs, @"Reload", @selector(refresh:), @"r", self);
    for (NSInteger i = 1; i <= 9; i++) {
        NSString *n = [@(i) stringValue];
        item(tabs, [@"Switch to Tab " stringByAppendingString:n], @selector(number:), n, self);
        tabs.itemArray.lastObject.tag = i - 1;
    }
    item(edit, @"Undo", @selector(undo:), @"z", nil);
    item(edit, @"Cut", @selector(cut:), @"x", nil);
    item(edit, @"Copy", @selector(copy:), @"c", nil);
    item(edit, @"Paste", @selector(paste:), @"v", nil);
    item(edit, @"Select All", @selector(selectAll:), @"a", nil);
    NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    NSMenuItem *windowItem = [NSMenuItem new];
    [main addItem:windowItem];
    windowItem.submenu = windowMenu;
    item(windowMenu, @"Minimize", @selector(performMiniaturize:), @"m", self.window);
    item(windowMenu, @"Zoom", @selector(performZoom:), @"", self.window);
    item(windowMenu, @"Enter Full Screen", @selector(toggleFullScreen:), @"f", self.window);
    windowMenu.itemArray.lastObject.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
    item(windowMenu, @"Toggle Focus Full Screen", @selector(toggleFocus:), @"f", self);
    windowMenu.itemArray.lastObject.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
    for (NSArray *command in @[@[@"Centre on Screen", @"centreWindow:", @"c"],
                               @[@"Fill Screen", @"fillWindow:", @"f"]]) {
        item(windowMenu, command[0], NSSelectorFromString(command[1]), command[2], self);
        windowMenu.itemArray.lastObject.keyEquivalentModifierMask = NSEventModifierFlagOption | NSEventModifierFlagCommand;
    }
    item(windowMenu, @"Hide Toolbar", @selector(togglePill:), @"\\", self);
    NSApp.windowsMenu = windowMenu;
    NSApp.mainMenu = main;
    [self buildPill];
    [NSApp activateIgnoringOtherApps:YES];
    if (!brow_new(defaultChromium(), false))
        brow_new(false, false);
    [self palette:nil];
#ifdef BROW_UI_TEST
    dispatch_async(dispatch_get_main_queue(), ^{
      smoke();
    });
#endif
}
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action == @selector(togglePill:)) {
        item.title = self.pill.parentWindow ? @"Hide Toolbar" : @"Show Toolbar";
        return YES;
    }
    if (item.action == @selector(fillWindow:) || item.action == @selector(centreWindow:))
        return !self.inFullScreen;
    if (!self.preferences.isKeyWindow) return YES;
    SEL action=item.action;
    return action==@selector(settings:) || action==@selector(closeTab:) || action==@selector(toggleFocus:);
}
- (void)windowWillClose:(NSNotification *)note {
    if (note.object == self.window) { [self.toolPopover close]; [self.pill close]; [self.preferences close]; }
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    if (!prepareQuit()) return NSTerminateNow;
    self.owesTerminationReply = YES;
    return NSTerminateLater;
}
- (void)dismiss {
    [self.overlay removeFromSuperview];
    self.overlay = nil;
    self.card = nil;
    self.body = nil;
    self.field = nil;
    [page() focus];
}
- (void)newTab:(id)sender {
    [self dismiss];
    if (brow_new(defaultChromium(), false))
        [self palette:nil];
}
- (void)closeTab:(id)sender {
    if (self.toolPopover.shown) { [self.toolPopover close]; return; }
    if (self.preferences.isKeyWindow) { [self.preferences close]; return; }
    [self dismiss];
    brow_close_tab();
}
- (void)number:(NSMenuItem *)sender {
    if ((NSUInteger)sender.tag < brow_count()) {
        [self dismiss];
        brow_select(sender.tag);
    }
}
- (void)back:(id)sender {
    [self dismiss];
    command(@selector(goBack));
}
- (void)forward:(id)sender {
    [self dismiss];
    command(@selector(goForward));
}
- (void)refresh:(id)sender {
    [self dismiss];
    command(@selector(refresh));
}
- (void)palette:(id)sender {
    if (self.pill.isKeyWindow) [self.window makeKeyWindow];
    if (self.overlay) {
        [self.field selectText:nil];
        return;
    }
    self.overlay = [[Overlay alloc] initWithFrame:self.window.contentView.bounds];
    self.overlay.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.overlay.dismiss = ^{
      [app dismiss];
    };
    [self.window.contentView addSubview:self.overlay positioned:NSWindowAbove relativeTo:nil];
    self.body = [NSView new];
    if (@available(macOS 26, *)) {
        NSGlassEffectView *glass = [NSGlassEffectView new];
        glass.cornerRadius = 24;
        glass.contentView = self.body;
        self.card = glass;
    } else {
        NSVisualEffectView *v = [NSVisualEffectView new];
        v.material = NSVisualEffectMaterialPopover;
        v.wantsLayer = YES;
        v.layer.cornerRadius = 24;
        v.layer.masksToBounds = YES;
        [v addSubview:self.body];
        self.card = v;
    }
    self.card.shadow = [NSShadow new];
    [self.overlay addSubview:self.card];
    self.field = [NSTextField new];
    self.field.bordered = NO;
    self.field.drawsBackground = NO;
    self.field.focusRingType = NSFocusRingTypeNone;
    self.field.font = [NSFont systemFontOfSize:24];
    self.field.placeholderString = @"Search or enter URL";
    self.field.accessibilityLabel = @"Search or enter URL";
    self.field.stringValue = [page() address];
    self.field.delegate = self;
    self.selected = -1;
    [self filter:@""];
    [self.window makeFirstResponder:self.field];
    [self.field selectText:nil];
}
- (void)filter:(NSString *)query {
    NSMutableArray *matches = [NSMutableArray array];
    for (size_t i = 0; i < brow_count(); i++) {
        NSView<BrowPage> *p = self.pages[@(brow_id(i))];
        NSString *text = [NSString stringWithFormat:@"%@ %@", p.pageTitle, p.address];
        if (!query.length || [text localizedCaseInsensitiveContainsString:query])
            [matches addObject:@(i)];
    }
    self.matches = matches;
    [self drawPalette];
}
- (void)drawPalette {
    for (NSView *v in self.body.subviews.copy)
        if (v != self.field)
            [v removeFromSuperview];
    CGFloat w = MIN(636, NSWidth(self.overlay.bounds) - 28);
    NSInteger limit = MIN(6, MAX(1, (NSHeight(self.overlay.bounds) - 120) / 46));
    NSInteger rows = MIN((NSInteger)self.matches.count, limit);
    CGFloat h = 72 + rows * 46 + (rows ? 8 : 0);
    CGFloat inset = MIN(NSHeight(self.overlay.bounds) * 0.19, MAX(8, NSHeight(self.overlay.bounds) - h - 8));
    self.card.frame =
        NSMakeRect((NSWidth(self.overlay.bounds) - w) / 2, NSHeight(self.overlay.bounds) - inset - h, w, h);
    self.body.frame = NSMakeRect(0, 0, w, h);
    self.field.frame = NSMakeRect(54, h - 54, w - 76, 36);
    if (!self.field.superview)
        [self.body addSubview:self.field];
    NSImageView *icon = [NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:@"magnifyingglass"
                                                                  accessibilityDescription:nil]];
    icon.frame = NSMakeRect(20, h - 47, 23, 23);
    icon.contentTintColor = NSColor.secondaryLabelColor;
    [self.body addSubview:icon];
    NSInteger start = MAX(0, self.selected - limit + 1);
    for (NSInteger i = 0; i < rows; i++) {
        NSInteger index = self.matches[start + i].integerValue;
        uint64_t ident = brow_id(index);
        NSView<BrowPage> *p = self.pages[@(ident)];
        NSString *title =
            p.pageTitle.length ? p.pageTitle : ([NSURL URLWithString:p.address].host ?: @"New tab");
        NSView *row = [[NSView alloc] initWithFrame:NSMakeRect(8, h - 72 - (i + 1) * 46, w - 16, 44)];
        if (start + i == self.selected) {
            row.wantsLayer = YES;
            row.layer.cornerRadius = 12;
            row.layer.backgroundColor = [NSColor.controlAccentColor colorWithAlphaComponent:0.18].CGColor;
        }
        NSImageView *glyph =
            [NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:ident == brow_current()
                                                                                   ? @"circle.inset.filled"
                                                                                   : @"globe"
                                                      accessibilityDescription:nil]];
        glyph.frame = NSMakeRect(12, 13, 18, 18);
        glyph.contentTintColor = NSColor.secondaryLabelColor;
        [row addSubview:glyph];
        NSTextField *name = [NSTextField labelWithString:title];
        name.font = [NSFont systemFontOfSize:14 weight:NSFontWeightMedium];
        name.frame = NSMakeRect(42, 21, w - 140, 20);
        name.lineBreakMode = NSLineBreakByTruncatingTail;
        [row addSubview:name];
        NSString *engine = [p isKindOfClass:WebPage.class] ? @"WebKit" : @"Chromium";
        NSTextField *detail = [NSTextField labelWithString:p.address.length ? p.address : engine];
        detail.font = [NSFont systemFontOfSize:11];
        detail.textColor = NSColor.secondaryLabelColor;
        detail.frame = NSMakeRect(42, 5, w - 140, 16);
        detail.lineBreakMode = NSLineBreakByTruncatingTail;
        [row addSubview:detail];
        if (index < 9) {
            NSTextField *hint = [NSTextField labelWithString:[NSString stringWithFormat:@"⌘%ld", index + 1]];
            hint.textColor = NSColor.secondaryLabelColor;
            hint.frame = NSMakeRect(w - 62, 13, 36, 18);
            [row addSubview:hint];
        }
        NSButton *button = [NSButton buttonWithTitle:@"" target:self action:@selector(number:)];
        button.transparent = YES;
        button.frame = row.bounds;
        button.tag = index;
        button.accessibilityLabel = title;
        [row addSubview:button];
        [self.body addSubview:row];
    }
}
- (NSArray<NSWindow *> *)customWindowsToEnterFullScreenForWindow:(NSWindow *)window {
    return self.focusMode ? @[window] : nil;
}
- (NSArray<NSWindow *> *)customWindowsToExitFullScreenForWindow:(NSWindow *)window {
    return self.focusMode ? @[window] : nil;
}
- (void)window:(NSWindow *)window startCustomAnimationToEnterFullScreenOnScreen:(NSScreen *)screen
        withDuration:(NSTimeInterval)duration {
    [window setFrame:screen.frame display:NO animate:NO];
    [self layoutFocus];
    [window displayIfNeeded];
}
- (void)window:(NSWindow *)window startCustomAnimationToExitFullScreenWithDuration:(NSTimeInterval)duration {
    [window setFrame:self.preferredFrame display:NO animate:NO];
    [self layoutFocus];
    [window displayIfNeeded];
}
- (void)beginFocusTransition {
    self.focusTransition = YES;
    [self hidePillTip];
    [self.toolPopover close];
    if (self.pill.parentWindow) {
        [self.window removeChildWindow:self.pill];
        [self.pill orderOut:nil];
    }
}
- (void)endFocusTransition {
    if (!self.focusTransition) return;
    self.focusTransition = NO;
    if (![NSUserDefaults.standardUserDefaults boolForKey:@"toolbarHidden"] && !self.quitting)
        [self attachPill];
}
- (void)windowWillExitFullScreen:(NSNotification *)note {
    if (self.focusMode) [self beginFocusTransition];
}
- (void)windowDidFailToExitFullScreen:(NSWindow *)window {
    if (self.focusMode) {
        [self layoutFocus];
        [self endFocusTransition];
    }
}
- (void)windowWillEnterFullScreen:(NSNotification *)note {
    [self.toolPopover close];
    self.inFullScreen = YES;
    [self updatePillActions];
    [self updateWindowCorners];
}
- (void)windowDidEnterFullScreen:(NSNotification *)note {
    if (self.focusMode) {
        [self layoutFocus];
        [self endFocusTransition];
        self.lastSampleKey = nil;
        [self refreshBackdrop];
    } else [self positionPill];
}
- (void)windowDidExitFullScreen:(NSNotification *)note {
    [self finishFocus];
    self.inFullScreen = NO;
    [self endFocusTransition];
    [self updatePillActions];
    [self positionPill];
    [self updateWindowCorners];
}
- (void)windowDidFailToEnterFullScreen:(NSWindow *)window {
    [self finishFocus];
    self.inFullScreen = NO;
    [self endFocusTransition];
    [self updatePillActions];
    [self positionPill];
    [self updateWindowCorners];
}
- (void)windowDidMove:(NSNotification *)note { [self positionPill]; [self updateWindowCorners]; [self syncGeometryFields]; }
- (void)windowDidChangeScreen:(NSNotification *)note { [self positionPill]; [self updateWindowCorners]; }
- (void)windowDidResize:(NSNotification *)note {
    [self updateWindowCorners];
    if (self.focusMode) [self layoutFocus];
    else [self positionPill];
    [self syncGeometryFields];
    if (self.overlay)
        [self drawPalette];
}
- (void)windowDidResignKey:(NSNotification *)note {
    if (!self.quitting && self.overlay && page().address.length)
        [self dismiss];
}
- (void)controlTextDidEndEditing:(NSNotification *)note {
    if (note.object == self.pillEditor) { [self endPillEditing]; return; }
    if ([self.geometryFields containsObject:note.object] || [self.quickFields containsObject:note.object]) {
        dispatch_async(dispatch_get_main_queue(), ^{ [self syncGeometryFields]; });
    }
}
- (void)controlTextDidChange:(NSNotification *)note {
    if (note.object == self.pillEditor) return;
    if ([self.geometryFields containsObject:note.object] || [self.quickFields containsObject:note.object]) {
        [self applyFields:[self.quickFields containsObject:note.object] ? self.quickFields : self.geometryFields centre:NO];
        return;
    }
    self.selected = -1;
    [self filter:self.field.stringValue];
}
- (BOOL)control:(NSControl *)control textView:(NSTextView *)editor doCommandBySelector:(SEL)sel {
    if (control == self.pillEditor) {
        if (sel != @selector(insertNewline:) && sel != @selector(cancelOperation:)) return NO;
        NSString *input = editor.string.copy;
        [self.window makeKeyAndOrderFront:nil];
        [self endPillEditing];
        [self dismiss];
        if (sel == @selector(insertNewline:) && input.length) {
            char *destination = brow_destination(input.UTF8String);
            if (destination) {
                navigate(@(destination));
                brow_free(destination);
            }
        }
        return YES;
    }
    NSArray *fields = [self.quickFields containsObject:(NSTextField *)control] ? self.quickFields : self.geometryFields;
    if ([fields containsObject:control]) {
        if (sel == @selector(insertTab:) || sel == @selector(insertBacktab:)) {
            NSInteger index = [fields indexOfObject:control];
            NSInteger next = index + (sel == @selector(insertTab:) ? 1 : -1);
            if (next >= 0 && next < (NSInteger)fields.count)
                [fields[next] selectText:nil];
            else if (next < 0) [control.window selectPreviousKeyView:control];
            else [control.window selectNextKeyView:control];
            return YES;
        }
        return NO;
    }
    if (sel == @selector(cancelOperation:))
        [self dismiss];
    else if (sel == @selector(moveDown:) || sel == @selector(moveUp:)) {
        self.selected = MAX(-1, MIN((NSInteger)self.matches.count - 1,
                                    self.selected + (sel == @selector(moveDown:) ? 1 : -1)));
        [self drawPalette];
    } else if (sel == @selector(insertNewline:)) {
        if (self.selected >= 0) {
            NSUInteger index = self.matches[self.selected].unsignedIntegerValue;
            [self dismiss];
            brow_select(index);
        } else {
            char *s = brow_destination(self.field.stringValue.UTF8String);
            [self dismiss];
            if (s) {
                navigate(@(s));
                brow_free(s);
            }
        }
    } else
        return NO;
    return YES;
}
#include "Preferences.inc"
#include "WindowToolbar.inc"
#include "Viewports.inc"

@end

static void openLink(NSString *url, bool useChromium) {
    if (app.quitting) return;
    NSString *scheme = [NSURL URLWithString:url].scheme.lowercaseString;
    if (![@[ @"http", @"https" ] containsObject:scheme])
        return;
    [app dismiss];
    if (brow_new(useChromium, false))
        navigate(url);
}
static void chromiumLink(NSString *url) { openLink(url, true); }

static NSView<BrowPage> *createPage(bool useChromium) {
    if (useChromium) {
        if (!app.plugin)
            app.plugin = dlopen(pluginPath().fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
        NSView<BrowPage> *(*create)(NSView *, void (*)(NSString *)) =
            app.plugin ? dlsym(app.plugin, "brow_chromium_page") : NULL;
        return create ? create(app.deck, chromiumLink) : nil;
    }
    WKWebViewConfiguration *config = [WKWebViewConfiguration new];
    NSURL *safari = [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:@"com.apple.Safari"];
    NSString *version = safari ? [NSBundle bundleWithURL:safari].infoDictionary[@"CFBundleShortVersionString"] : nil;
    if (version.length)
        config.applicationNameForUserAgent = [NSString stringWithFormat:@"Version/%@ Safari/605.1.15", version];
    WebPage *wk = [[WebPage alloc] initWithFrame:app.deck.bounds configuration:config];
    for (NSString *key in @[@"URL", @"canGoBack", @"canGoForward", @"loading"])
        [wk addObserver:wk forKeyPath:key options:0 context:&webNavigationContext];
    wk.UIDelegate = wk;
    wk.allowsBackForwardNavigationGestures = YES;
    return wk;
}

bool host_create(uint64_t ident, bool useChromium) {
    NSView<BrowPage> *p = createPage(useChromium);
    if (!p) {
        NSAlert *alert = [NSAlert new];
        alert.messageText = @"Chromium could not start";
        alert.informativeText = @"Launch the Brow application bundle built with scripts/build-app.";
        [alert runModal];
        return false;
    }
    Surface *surface = [[Surface alloc] initWithFrame:app.deck.bounds];
    surface.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    surface.wantsLayer = YES;
    [surface add:p];
    [surface arrange];
    surface.hidden = YES;
    [app.deck addSubview:surface];
    app.pages[@(ident)] = p;
    return true;
}

static void animate(CALayer *layer, NSString *key, id from, id to, double duration) {
    CABasicAnimation *a = [CABasicAnimation animationWithKeyPath:key];
    a.fromValue = from;
    a.toValue = to;
    a.duration = duration;
    a.timingFunction = [CAMediaTimingFunction functionWithControlPoints:0.20:0.78:0.18:1.0];
    [layer addAnimation:a forKey:key];
}

static CATransform3D depth(NSView *surface, CGFloat scale) {
    CGPoint anchor = surface.layer.anchorPoint;
    CGFloat x = (0.5 - anchor.x) * NSWidth(surface.bounds), y = (0.5 - anchor.y) * NSHeight(surface.bounds);
    CATransform3D t = CATransform3DMakeTranslation(x * (1 - scale), y * (1 - scale), 0);
    return CATransform3DScale(t, scale, scale, 1);
}

void host_present(uint64_t ident, uint64_t previous, int direction) {
    if (app.pill.isKeyWindow) [app.window makeKeyWindow];
    NSUInteger ticket = ++app.generation;
    [app syncViewports];
    NSView *incoming = surfaceOf(app.pages[@(ident)]), *outgoing = surfaceOf(app.pages[@(previous)]);
    BOOL motion = direction && outgoing && !reducedMotion();
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (Surface *surface in app.deck.subviews) {
        BOOL entering = surface == incoming, leaving = surface == outgoing;
        surface.interactive = entering;
        BOOL inFlight = [surface.layer animationForKey:@"transform"] != nil;
        CALayer *live = surface.layer.presentationLayer;
        CATransform3D current = inFlight && live ? live.transform : CATransform3DIdentity;
        CGFloat opacity = inFlight && live ? live.opacity : 1;
        NSNumber *radius = inFlight ? [live valueForKeyPath:@"filters.depthBlur.inputRadius"] : nil;
        [surface.layer removeAllAnimations];
        surface.layer.filters = nil;
        surface.layer.transform = CATransform3DIdentity;
        surface.layer.opacity = 1;
        if (!motion || (!entering && !leaving)) {
            surface.hidden = !entering;
            continue;
        }
        surface.hidden = NO;
        CATransform3D from = entering && !inFlight ? depth(surface, 1 + 0.026 * direction) : current;
        CATransform3D to = entering ? CATransform3DIdentity : depth(surface, 1 - 0.032 * direction);
        surface.layer.transform = to;
        animate(surface.layer, @"transform", [NSValue valueWithCATransform3D:from],
                [NSValue valueWithCATransform3D:to], 0.29);
        BOOL foreground = direction > 0 ? entering : leaving;
        if (foreground) {
            surface.layer.opacity = entering ? 1 : 0;
            animate(surface.layer, @"opacity", @(inFlight ? opacity : (entering ? 0 : 1)),
                    @(surface.layer.opacity), 0.22);
        }
        CIFilter *filter = [CIFilter filterWithName:@"CIGaussianBlur"];
        filter.name = @"depthBlur";
        NSNumber *end = entering ? @0 : @3.5;
        [filter setValue:end forKey:kCIInputRadiusKey];
        surface.layer.filters = @[ filter ];
        animate(surface.layer, @"filters.depthBlur.inputRadius", radius ?: (entering ? @2.8 : @0), end, 0.26);
    }
    [app.deck addSubview:motion && direction < 0 ? outgoing : incoming
              positioned:NSWindowAbove
              relativeTo:nil];
    [CATransaction commit];
    [app.pages[@(ident)] focus];
    [app syncPillNavigation:nil];
    if (app.focusMode) [app refreshBackdrop];
    if (motion)
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 290 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
          if (ticket != app.generation)
              return;
          [CATransaction begin];
          [CATransaction setDisableActions:YES];
          outgoing.hidden = YES;
          outgoing.layer.transform = CATransform3DIdentity;
          outgoing.layer.filters = nil;
          incoming.layer.filters = nil;
          [CATransaction commit];
        });
}
void host_dispose(uint64_t ident) {
    Surface *surface = surfaceOf(app.pages[@(ident)]);
    [surface discard:0];
    [surface removeFromSuperview];
    [app.pages removeObjectForKey:@(ident)];
}
void host_close(void) { [app.window close]; }
void host_run(void) {
    @autoreleasepool {
        [BrowApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        app = [Browser new];
        NSApp.delegate = app;
        [NSApp run];
    }
}

#ifdef BROW_UI_TEST
#include "../tests/native_smoke.inc"
#endif
