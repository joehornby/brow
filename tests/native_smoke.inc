// Compiled only in the ui-test build, which uses a separate app bundle and profile.
static void check(BOOL ok, const char *message) {
    if (!ok) { fprintf(stderr,"FAIL: %s\n",message); exit(2); }
}
static void shortcut(NSString *key) {
    NSEvent *event=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
        modifierFlags:NSEventModifierFlagCommand timestamp:NSProcessInfo.processInfo.systemUptime
        windowNumber:app.window.windowNumber context:nil characters:key charactersIgnoringModifiers:key
        isARepeat:NO keyCode:0];
    [NSApp sendEvent:event];
}
static void checkTopGesture(NSEventType followUp, BOOL shouldDrag) {
    NSPoint point = NSMakePoint(NSMidX(app.deck.bounds), NSMaxY(app.deck.bounds) - 16);
    NSPoint location = [app.deck convertPoint:point toView:nil];
    NSEvent *(^mouse)(NSEventType) = ^NSEvent *(NSEventType type) {
        return [NSEvent mouseEventWithType:type location:location modifierFlags:0 timestamp:0
            windowNumber:app.window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1];
    };
    [NSApp postEvent:mouse(followUp) atStart:YES];
    check(shouldStartWindowDrag(mouse(NSEventTypeLeftMouseDown)) == shouldDrag, "top strip distinguishes clicks and drags");
    NSEvent *queued = [NSApp nextEventMatchingMask:NSEventMaskFromType(followUp) untilDate:NSDate.distantPast
                                             inMode:NSEventTrackingRunLoopMode dequeue:YES];
    check(queued.type == followUp, "top strip leaves follow-up event available");
}
static void verifySwitches(void);
static NSButton *prefsButton(NSView *view, SEL action, NSInteger tag) {
    if ([view isKindOfClass:NSButton.class] && [(NSButton *)view action]==action && (tag == NSNotFound || view.tag==tag))
        return (NSButton *)view;
    for (NSView *child in view.subviews) {
        NSButton *button=prefsButton(child,action,tag);
        if (button) return button;
    }
    return nil;
}
static void focusSettingsTest(BOOL useChromium) {
    if (useChromium) check(brow_new(true,true),"Chromium focus test");
    [page() navigate:@"data:text/html,<title>Colour</title><body%20style='background:teal'>"];
    [app settings:nil];
    app.section=@"Window"; [app drawPreferences];
    for (NSUInteger i=0;i<4;i++) {
        if (!i) [app.geometryFields[i] selectText:nil];
        NSTextView *editor=(NSTextView *)app.preferences.firstResponder;
        check(editor && editor==app.geometryFields[i].currentEditor,"tab reaches next geometry field");
        [editor insertText:@[@"640",@"440",@"10",@"30"][i] replacementRange:NSMakeRange(0,editor.string.length)];
        if (i<3) [editor doCommandBySelector:@selector(insertTab:)];
    }
    [(NSTextView *)app.preferences.firstResponder doCommandBySelector:@selector(insertBacktab:)];
    check(app.preferences.firstResponder==app.geometryFields[2].currentEditor,"shift-tab reaches previous field");
    check(NSEqualSizes(app.window.frame.size,NSMakeSize(640,440)),"dimensions update during editing");
    check(NSMinX(app.window.frame)==NSMinX(app.window.screen.visibleFrame)+10,"position updates during editing");
    [prefsButton(app.preferences.contentView,@selector(applyGeometry:),1) performClick:nil];
    NSRect original=app.window.frame;
    check(NSEqualSizes(original.size,NSMakeSize(640,440)),"exact frame dimensions");
    check(fabs(NSMidX(original)-NSMidX(app.window.screen.visibleFrame))<=1,"window centered");
    [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"solidBackdrop"];
    NSNotificationCenter *center=NSNotificationCenter.defaultCenter;
    [center addObserverForName:NSWindowDidResizeNotification object:app.window queue:nil usingBlock:^(NSNotification *n) {
        (void)n;
        if (app.focusTransition) {
            check(NSEqualSizes(page().bounds.size, original.size), "engine viewport stays stable during Focus transition");
            check(!page().hiddenOrHasHiddenAncestor, "live page remains visible");
            check(!app.pill.parentWindow, "accessory stays out of the Spaces transition");
        }
    }];
    [center addObserverForName:NSWindowDidEnterFullScreenNotification object:app.window queue:nil usingBlock:^(NSNotification *n) {
        (void)n;
        NSScreen *screen = app.window.screen;
        check(NSContainsRect(app.window.frame, NSInsetRect(screen.visibleFrame, 1, 1)), "Focus covers the usable screen");
        check(app.focusMode && NSEqualSizes(app.deck.frame.size,original.size),"full-screen page preserves dimensions");
        check(fabs(NSMidX(app.deck.frame)-NSMidX(app.window.contentView.bounds))<=1,"centered focus page");
        __block BOOL changedPage=NO;
        NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:15];
        [NSTimer scheduledTimerWithTimeInterval:.1 repeats:YES block:^(NSTimer *timer) {
            NSArray *colors=app.backdrop.colors;
            NSColor *color=[NSColor colorWithCGColor:(__bridge CGColorRef)colors.firstObject];
            color=[color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
            BOOL reduce=NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
            if (!changedPage && color.greenComponent-color.redComponent>.12) {
                check(colors.count==FocusStopCount,"perceptual gradient stops");
                check(reduce || [app.backdrop animationForKey:@"siteColors"].duration==6,"gentle first sample");
                changedPage=YES;
                [page() navigate:@"data:text/html,<title>Warm</title><body%20style='background:coral'>"];
            } else if (changedPage && color.redComponent-color.greenComponent>.12) {
                check(reduce || [app.backdrop animationForKey:@"siteColors"].duration==14,"slow retarget after navigation");
                [timer invalidate]; [app toggleFocus:nil];
            } else check(deadline.timeIntervalSinceNow>0,"gradient follows rendered page colours");
        }];
    }];
    [center addObserverForName:NSWindowDidExitFullScreenNotification object:app.window queue:nil usingBlock:^(NSNotification *n) {
        (void)n;
        dispatch_async(dispatch_get_main_queue(), ^{
            check(NSEqualRects(app.window.frame,original),"focus restores frame");
            shortcut(@"q");
        });
    }];
    [app toggleFocus:nil];
}
static void smoke(void) {
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"TestToolbar"]) {
        NSRect screen = app.window.screen.visibleFrame, start = app.window.frame;
        check(NSWidth(start) == MIN(1200, NSWidth(screen)) && NSHeight(start) == MIN(800, NSHeight(screen)), "startup size");
        check(fabs(NSMidX(start)-NSMidX(screen)) <= 1 && fabs(NSMidY(start)-NSMidY(screen)) <= 1, "startup centre");
        check(app.pill.visible && app.pill.parentWindow == app.window, "visible child toolbar");
        NSView *controls = app.pill.contentView;
        NSButton *geometry = prefsButton(controls, @selector(quickGeometry:), NSNotFound);
        check(geometry.image != nil, "geometry button");
        [geometry performClick:nil];
        check(app.toolPopover.shown && app.quickFields.count == 4, "geometry popover");
        [app.quickFields[0] selectText:nil];
        NSTextView *editor = (NSTextView *)app.quickFields[0].currentEditor;
        check(editor != nil, "geometry popover accepts keyboard input");
        [editor insertText:@"640" replacementRange:NSMakeRange(0, editor.string.length)];
        check(NSWidth(app.window.frame) == 640 && app.toolPopover.shown, "popover resizes live");
        [editor doCommandBySelector:@selector(insertTab:)];
        check(app.quickFields[1].currentEditor != nil, "popover tab order");
        [app.toolPopover close];
        [prefsButton(controls, @selector(windowSettings:), NSNotFound) performClick:nil];
        check(app.preferences.visible && [app.section isEqual:@"Window"], "window settings deep link");
        [app.preferences close];
        [prefsButton(controls, @selector(fillWindow:), NSNotFound) performClick:nil];
        check(NSEqualRects(app.window.frame, screen) && NSContainsRect(screen, app.pill.frame), "fill keeps toolbar on display");
        [app.window setFrame:NSMakeRect(NSMinX(screen)+20,NSMidY(screen),640,300) display:YES];
        [prefsButton(controls, @selector(centreWindow:), NSNotFound) performClick:nil];
        check(fabs(NSMidX(app.window.frame)-NSMidX(screen)) <= 1 && fabs(NSMidY(app.window.frame)-NSMidY(screen)) <= 1, "centre action");
        check(fabs(NSMidX(app.window.frame)-NSMidX(app.pill.frame)) <= 1 && NSMaxY(app.pill.frame) < NSMinY(app.window.frame), "toolbar tracks centred frame");
        [prefsButton(controls, @selector(showShortcuts:), NSNotFound) performClick:nil];
        check(app.toolPopover.shown, "shortcuts popover");
        [app.toolPopover close];
        [prefsButton(controls, @selector(togglePill:), NSNotFound) performClick:nil];
        check(!app.pill.visible, "hide toolbar");
        shortcut(@"\\");
        check(app.pill.visible, "show toolbar by shortcut");
        shortcut(@"q"); return;
    }
    NSString *iconCase=[NSUserDefaults.standardUserDefaults stringForKey:@"TestIconCase"];
    NSString *focusCase=[NSUserDefaults.standardUserDefaults stringForKey:@"TestFocus"];
    if (focusCase) { focusSettingsTest([focusCase isEqual:@"chromium"]); return; }
    if (iconCase) {
        if ([iconCase isEqual:@"save"]) {
            [app settings:nil];
            NSButton *neon=prefsButton(app.preferences.contentView,@selector(pickIcon:),2);
            check(neon!=nil,"Neon icon row");
            [neon performClick:nil];
            check(app.preferences.isVisible,"selection leaves settings open");
            [app.preferences close];
        }
        check([selectedIcon() isEqual:@"Neon"], "icon selection persists");
        NSBitmapImageRep *actual=[NSBitmapImageRep imageRepWithData:NSApp.applicationIconImage.TIFFRepresentation];
        NSBitmapImageRep *expected=[NSBitmapImageRep imageRepWithData:iconImage(@"Neon").TIFFRepresentation];
        NSColor *a=[[actual colorAtX:actual.pixelsWide/2 y:actual.pixelsHigh/4] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
        NSColor *b=[[expected colorAtX:expected.pixelsWide/2 y:expected.pixelsHigh/4] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
        check(a && b && fabs(a.redComponent-b.redComponent)<0.05 && fabs(a.blueComponent-b.blueComponent)<0.05,
              "running app uses selected artwork");
        if ([iconCase isEqual:@"restore"]) [NSUserDefaults.standardUserDefaults removeObjectForKey:@"appIcon"];
        shortcut(@"q"); return;
    }

    check(!(app.window.styleMask & NSWindowStyleMaskTitled),"window is borderless");
    check(NSEqualSizes(app.deck.bounds.size,app.window.frame.size),"page fills the window");
    NSRect screen = app.window.screen.visibleFrame;
    NSRect floating = NSInsetRect(screen, 63, 71);
    [app.window setFrame:floating display:YES];
    NSRect half = screen;
    half.size.width /= 2;
    for (NSInteger side = 0; side < 2; ++side) {
        half.origin.x = NSMinX(screen) + side * NSWidth(half);
        [app.window setFrame:NSInsetRect(half, 8, 8) display:YES];
        check(app.window.contentView.layer.cornerRadius==16,"inset half uses standard corners");
    }
    NSRect example = NSMakeRect(-1440, 40, 1440, 900);
    for (int i=0;i<2;i++) {
        NSRect row = NSMakeRect(-1440,40+i*450,1440,450);
        check(tiledFrame(row,example),"top and bottom halves on secondary display");
        for (int j=0;j<2;j++)
            check(tiledFrame(NSMakeRect(-1440+j*720,row.origin.y,720,450),example),"quarter tile");
    }
    [app.window setFrame:floating display:YES];
    check(app.window.contentView.layer.cornerRadius==24 && app.window.contentView.layer.masksToBounds,"rounded page clipping");
    check(app.field.currentEditor!=nil,"palette has text focus");
    checkTopGesture(NSEventTypeLeftMouseDragged, YES);
    checkTopGesture(NSEventTypeLeftMouseUp, NO);
    NSString *quitCase = [NSUserDefaults.standardUserDefaults stringForKey:@"TestQuitCase"];
    if ([quitCase isEqual:@"fullscreen"]) {
        NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
        [center addObserverForName:NSWindowDidEnterFullScreenNotification object:app.window queue:nil
                        usingBlock:^(NSNotification *note) {
            check(app.window.contentView.layer.cornerRadius == 0, "full screen has square corners");
            dispatch_async(dispatch_get_main_queue(), ^{ [app.window toggleFullScreen:nil]; });
        }];
        [center addObserverForName:NSWindowDidExitFullScreenNotification object:app.window queue:nil
                        usingBlock:^(NSNotification *note) {
            dispatch_async(dispatch_get_main_queue(), ^{
                check(app.window.contentView.layer.cornerRadius == 24, "windowed rounding restored");
                shortcut(@"q");
            });
        }];
        [app.window toggleFullScreen:nil];
        return;
    }
    if ([quitCase isEqual:@"palette"]) { shortcut(@"q"); return; }
    if ([quitCase isEqual:@"last-window"]) {
        [app dismiss];
        check(brow_new(true,true),"Chromium is the only open tab");
        shortcut(@"w");
        return;
    }
    [app dismiss];
    [app applicationShouldHandleReopen:NSApp hasVisibleWindows:YES];
    check(app.field.currentEditor!=nil,"reopen focuses address palette");
    [app dismiss];
    [(WebPage *)page() loadHTMLString:@"<title>WebKit test</title><body style='background:teal'>" baseURL:nil];
    shortcut(@"t");
    check(brow_count()==2 && app.overlay,"new tab");
    [app dismiss];
    check(brow_new(true,true),"Chromium engine replacement");
    [page() navigate:@"data:text/html,<meta%20name=theme-color%20content=coral><title>Chromium%20test</title><body%20style='background:gold'>"];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:12];
    [NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *timer) {
        BOOL webkitReady=[app.pages[@(brow_id(0))].pageTitle isEqual:@"WebKit test"];
        if ((webkitReady && [page().pageTitle isEqual:@"Chromium test"]) || deadline.timeIntervalSinceNow <= 0) {
            [timer invalidate]; verifySwitches();
        }
    }];
}
static void verifySwitches(void) {

    check([page().pageTitle isEqual:@"Chromium test"],"Chromium rendered the document");
    shortcut(@"1");
    check([page().pageTitle isEqual:@"WebKit test"],"WebKit document survives switching");
    CALayer *layer=page().superview.layer;
    BOOL reduced=NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion ||
        [NSUserDefaults.standardUserDefaults boolForKey:@"TestReduceMotion"];
    check(([layer animationForKey:@"transform"]!=nil)!=reduced,"depth respects Reduce Motion");
    check(([layer animationForKey:@"filters.depthBlur.inputRadius"]!=nil)!=reduced,"blur respects Reduce Motion");
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,65*NSEC_PER_MSEC),dispatch_get_main_queue(),^{
        shortcut(@"2"); shortcut(@"1"); shortcut(@"2");
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,350*NSEC_PER_MSEC),dispatch_get_main_queue(),^{
            check(brow_current()==brow_id(1),"rapid reversal ends on intended tab");
            for (NSView *v in app.deck.subviews) {
                check(v.hidden==(v!=page().superview),"settled surface visibility");
                check(!v.layer.filters.count,"settled surfaces release blur filters");
            }
            shortcut(@"l"); check(app.field.currentEditor!=nil,"Chromium palette focus");
            [app dismiss]; shortcut(@"w");
            check(brow_count()==1,"close Chromium tab");
            check(brow_new(true,false),"quit with a live Chromium tab");
            [page() focus];
            [NSTimer scheduledTimerWithTimeInterval:0.01 repeats:NO block:^(NSTimer *unused) {
                (void)unused; fprintf(stderr,"Sending Command-Q with Chromium focused\n"); shortcut(@"q");
            }];
        });
    });
}
