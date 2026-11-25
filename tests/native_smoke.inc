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
        [app settings:nil];
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
                check(app.preferences.isOnActiveSpace && app.window.isOnActiveSpace,"settings open on the Focus Space");
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
static BOOL near(CGFloat a, CGFloat b) { return fabs(a-b)<=1; }
static BOOL drawsText(CALayer *layer) {
    NSInteger w=CGRectGetWidth(layer.bounds)*2, h=CGRectGetHeight(layer.bounds)*2, bright=0;
    NSBitmapImageRep *rep=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h bitsPerSample:8
        samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    CGContextRef c=[NSGraphicsContext graphicsContextWithBitmapImageRep:rep].CGContext;
    CGContextSetRGBFillColor(c,0,0,0,1);
    CGContextFillRect(c,CGRectMake(0,0,w,h));
    CGContextScaleCTM(c,2,2);
    [layer renderInContext:c];
    for (NSInteger x=0;x<w;x++)
        for (NSInteger y=0;y<h;y++) bright+=[rep colorAtX:x y:y].brightnessComponent>.6;
    return bright>20;
}
static void viewportsTest(BOOL useChromium) {
    [app dismiss];
    [NSUserDefaults.standardUserDefaults removeObjectForKey:@"isolateViewports"];
    if (useChromium) check(brow_new(true,true),"Chromium viewports test");
    [page() navigate:@"data:text/html,<body%20style='background:teal'><script>c='';onclick=e=>c='%20'+e.clientX+','+e.clientY;setInterval(()=>document.title=innerWidth+'x'+innerHeight+c,50)</script>"];
    NSButton *open=prefsButton(app.pill.contentView,@selector(showViewports:),NSNotFound);
    check(open.image!=nil && NSMaxX(open.frame)<NSWidth(app.pill.frame),"viewports button");
    __block NSArray<NSNumber *> *chosen=@[@0,@1,@4];
    __block int stage=0;
    void (^toggle)(NSArray<NSNumber *> *)=^(NSArray<NSNumber *> *tags) {
        [open performClick:nil];
        check(app.toolPopover.shown,"viewports popover");
        for (NSNumber *tag in tags)
            [prefsButton(app.toolPopover.contentViewController.view,@selector(toggleViewport:),tag.integerValue) performClick:nil];
        check(app.toolPopover.shown,"popover stays open during selection");
        [app.toolPopover close];
    };
    Surface *surface=surfaceOf(page());
    void (^aligned)(void)=^{
        NSArray<Tile *> *tiles=surface.subviews;
        NSRect area=surface.bounds, all=NSZeroRect;
        check(CGColorGetAlpha(app.window.contentView.layer.backgroundColor)==0 && app.window.contentView.layer.cornerRadius==0,"window is an invisible container");
        for (Tile *tile in tiles) all=NSUnionRect(all,tile.frame);
        NSRect block=NSMakeRect(NSMinX(all),NSMinY(all)-26,NSWidth(all),NSHeight(all)+26);
        check(NSContainsRect(NSInsetRect(area,-1,-1),block),"tiles and captions stay in the window");
        check((near(NSWidth(block),NSWidth(area)) || near(NSHeight(block),NSHeight(area))) &&
              near(NSMidX(block),NSMidX(area)) && near(NSMidY(block),NSMidY(area)),"grid fills and centres");
        for (NSUInteger i=0;i<tiles.count;i++) {
            NSRect f=tiles[i].frame;
            NSArray *preset=viewportPresets()[chosen[i].unsignedIntegerValue];
            CGFloat ratio=[preset[1] doubleValue]/[preset[2] doubleValue];
            check(fabs(NSWidth(f)/NSHeight(f)-ratio)<.02,"tile keeps the aspect ratio");
            CALayer *caption=tiles[i].caption;
            NSString *text=[(NSAttributedString *)((CATextLayer *)caption.sublayers.firstObject).string string];
            check(caption.hidden || ([text hasPrefix:preset[0]] && near(CGRectGetMidX(caption.frame),NSMidX(f)) &&
                  CGRectGetMaxY(caption.frame)<NSMinY(f)),"caption names the viewport below its tile");
            check(fabs(NSWidth(tiles[i].page.frame)-NSWidth(tiles[i].bounds))<.01 &&
                  fabs(NSHeight(tiles[i].page.frame)-NSHeight(tiles[i].bounds))<.01,"page fills its tile");
            BOOL left=near(NSMinX(f),NSMinX(all)), right=near(NSMaxX(f),NSMaxX(all));
            BOOL top=near(NSMaxY(f),NSMaxY(all)), bottom=near(NSMinY(f),NSMinY(all));
            for (Tile *other in tiles) {
                NSRect o=other.frame;
                if (other==tiles[i]) continue;
                BOOL sameRow=near(NSMinY(o),NSMinY(f));
                check(!sameRow || near(NSHeight(o),NSHeight(f)),"row shares top and bottom edges");
                check(!NSIntersectsRect(o,f),"tiles do not overlap");
                left|=sameRow && near(NSMaxX(o)+16,NSMinX(f));
                right|=sameRow && near(NSMinX(o)-16,NSMaxX(f));
                top|=near(NSMinY(o)-42,NSMaxY(f));
                bottom|=near(NSMaxY(o)+42,NSMinY(f));
            }
            check(left && right && top && bottom,"each tile edge meets the grid edge or a gap");
        }
    };
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:20];
    [NSTimer scheduledTimerWithTimeInterval:.1 repeats:YES block:^(NSTimer *timer) {
        check(deadline.timeIntervalSinceNow>0,stage==0 ? "page loads" : stage<=2 ? "each engine lays out at its viewport size"
              : stage>=4 ? "all viewports follow a navigation in one viewport" : "a click reaches the scaled page at the correct point");
        NSArray<Tile *> *tiles=surface.subviews;
        if (stage==0) {
            if (!page().pageTitle.length) return;
            toggle(chosen);
            check(surface.subviews.count==3 && currentPages().count==3,"one tile for each selected viewport");
            aligned();
            NSRect screen=app.window.screen.visibleFrame;
            for (NSValue *size in @[[NSValue valueWithSize:NSMakeSize(520,760)],[NSValue valueWithSize:NSMakeSize(1100,420)],
                                    [NSValue valueWithSize:NSMakeSize(1200,800)]]) {
                [app.window setFrame:NSMakeRect(NSMinX(screen)+20,NSMinY(screen)+20,size.sizeValue.width,size.sizeValue.height) display:YES];
                aligned();
            }
            stage=1;
            return;
        }
        BOOL ready=YES;
        if (stage==4) {
            for (Tile *tile in tiles) ready&=[tile.page.address containsString:@"second"];
            if (!ready) return;
            stage=5;
        }
        for (NSUInteger i=0;i<tiles.count;i++) {
            NSArray *preset=viewportPresets()[chosen[i].unsignedIntegerValue];
            int w=0,h=0,x=-9,y=-9;
            int parts=sscanf(tiles[i].page.pageTitle.UTF8String,"%dx%d %d,%d",&w,&h,&x,&y);
            ready&=parts>=2 && w==[preset[1] intValue] && h==[preset[2] intValue];
            if (stage==3 && i==1) ready&=parts==4 && abs(x-w/2)<=2 && abs(y-h/2)<=2;
        }
        if (!ready && stage!=2) return;
        if (stage==1) {
            [app.window makeKeyWindow];
            stage=2;
            return;
        }
        if (stage==2) {
            NSRect f=[tiles[1] convertRect:tiles[1].bounds toView:nil];
            for (NSNumber *type in @[@(NSEventTypeLeftMouseDown),@(NSEventTypeLeftMouseUp)])
                [NSApp sendEvent:[NSEvent mouseEventWithType:type.integerValue location:NSMakePoint(NSMidX(f),NSMidY(f))
                    modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:app.window.windowNumber
                    context:nil eventNumber:1 clickCount:1 pressure:1]];
            stage=3;
            return;
        }
        if (stage==3) {
            [tiles[1].page navigate:@"data:text/html,<body%20id=second%20style='background:coral'><script>setInterval(()=>document.title=innerWidth+'x'+innerHeight,50)</script>"];
            stage=4;
            return;
        }
        if (stage++<10) return;
        [timer invalidate];
        Tile *first=tiles[0], *second=tiles[1], *third=tiles[2];
        NSRect home=first.frame, target=second.frame;
        check(!first.caption.hidden,"caption shows for the drag");
        NSPoint grip=NSMakePoint(CGRectGetMidX(first.caption.frame),CGRectGetMidY(first.caption.frame));
        CGFloat dx=NSMaxX(target)+8-NSMidX(home), dy=NSMidY(target)-NSMidY(home);
        void (^mouse)(NSEventType,CGFloat,CGFloat)=^(NSEventType type,CGFloat x,CGFloat y) {
            [NSApp sendEvent:[NSEvent mouseEventWithType:type
                location:[surface convertPoint:NSMakePoint(grip.x+x,grip.y+y) toView:nil]
                modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:app.window.windowNumber
                context:nil eventNumber:1 clickCount:1 pressure:1]];
        };
        mouse(NSEventTypeLeftMouseDown,0,0);
        mouse(NSEventTypeLeftMouseDragged,2,0);
        check(surface.held==first && NSEqualRects(first.frame,home) && surface.marker.opacity==0,
              "a small movement does not lift the viewport");
        mouse(NSEventTypeLeftMouseDragged,dx,dy);
        check(near(NSMinX(first.frame),NSMinX(home)+dx) && near(NSMinY(first.frame),NSMinY(home)+dy) &&
              near(CGRectGetMidX(first.caption.frame),NSMidX(first.frame)),"the viewport and its caption move with the pointer");
        check(surface.slot==2 && surface.marker.opacity==1 && NSHeight(surface.marker.frame)==NSHeight(target) &&
              NSMinX(surface.marker.frame)>=NSMaxX(target)-6,"the marker shows where the viewport will land");
        mouse(NSEventTypeLeftMouseUp,dx,dy);
        chosen=@[@1,@0,@4];
        check([app.viewports isEqual:chosen] && surface.subviews[0]==second && surface.subviews[1]==first &&
              surface.marker.opacity==0 && !surface.held && page()==second.page,"a drop moves the viewport to the marked position");
        check([first.page.pageTitle hasPrefix:@"402x874"],"a moved viewport keeps its page and its size");
        aligned();
        mouse(NSEventTypeLeftMouseDown,0,0);
        mouse(NSEventTypeLeftMouseUp,0,0);
        check([app.viewports isEqual:chosen],"a click on a caption does not move a viewport");
        NSButton *(^syncBox)(void)=^{ return prefsButton(app.toolPopover.contentViewController.view,@selector(toggleViewportSync:),NSNotFound); };
        [open performClick:nil];
        [syncBox() performClick:nil];
        [app.toolPopover close];
        check(page()==second.page && surface.selected==second,"a different page in each viewport selects one viewport");
        NSPoint inside=[surface convertPoint:NSMakePoint(NSMidX(first.frame),NSMidY(first.frame)) toView:nil];
        for (NSNumber *type in @[@(NSEventTypeLeftMouseDown),@(NSEventTypeLeftMouseUp)])
            [NSApp sendEvent:[NSEvent mouseEventWithType:type.integerValue location:inside modifierFlags:0
                timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:app.window.windowNumber
                context:nil eventNumber:1 clickCount:1 pressure:1]];
        check(page()==first.page && surface.selected==first &&
              CGColorEqualToColor(first.caption.backgroundColor,captionColor(YES)) &&
              CGColorEqualToColor(second.caption.backgroundColor,captionColor(NO)),"a click in a viewport selects it");
        check(surface.bars && !first.bar.hidden && !second.bar.hidden && !third.bar.hidden &&
              CGRectGetMaxY(first.bar.frame)<CGRectGetMinY(first.caption.frame),"an address bar shows under each viewport");
        check(drawsText(first.bar) && drawsText(second.bar) && drawsText(third.bar),"each address bar shows its address");
        NSPoint barPoint=[surface convertPoint:NSMakePoint(CGRectGetMidX(second.bar.frame),CGRectGetMidY(second.bar.frame)) toView:nil];
        for (NSNumber *type in @[@(NSEventTypeLeftMouseDown),@(NSEventTypeLeftMouseUp)])
            [NSApp sendEvent:[NSEvent mouseEventWithType:type.integerValue location:barPoint modifierFlags:0
                timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:app.window.windowNumber
                context:nil eventNumber:1 clickCount:1 pressure:1]];
        NSTextView *barEditor=(NSTextView *)app.barEditor.currentEditor;
        check(page()==second.page && app.barEditor.superview && barEditor,"a click on an address bar edits that viewport");
        check(!app.barEditor.cell.wraps && app.barEditor.cell.scrollable && NSMinX(barEditor.superview.bounds)>0,
              "a long address stays on one line and shows its end");
        [barEditor doCommandBySelector:@selector(cancelOperation:)];
        check(!app.barEditor.superview && [second.address containsString:@"id=second"],"Escape cancels a viewport address edit");
        barPoint=[surface convertPoint:NSMakePoint(CGRectGetMidX(first.bar.frame),CGRectGetMidY(first.bar.frame)) toView:nil];
        for (NSNumber *type in @[@(NSEventTypeLeftMouseDown),@(NSEventTypeLeftMouseUp)])
            [NSApp sendEvent:[NSEvent mouseEventWithType:type.integerValue location:barPoint modifierFlags:0
                timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:app.window.windowNumber
                context:nil eventNumber:1 clickCount:1 pressure:1]];
        barEditor=(NSTextView *)app.barEditor.currentEditor;
        check(page()==first.page && barEditor,"the address bar edits the viewport under it");
        [barEditor insertText:@"brow viewport" replacementRange:NSMakeRange(0,barEditor.string.length)];
        [barEditor doCommandBySelector:@selector(insertNewline:)];
        check(!app.barEditor.superview && [first.address containsString:@"q=brow+viewport"] &&
              [second.address containsString:@"id=second"] && [third.address containsString:@"id=second"],
              "Return in an address bar loads only that viewport");
        navigate(@"data:text/html,<body%20id=own>");
        check([first.address containsString:@"id=own"] && [second.address containsString:@"id=second"] &&
              [third.address containsString:@"id=second"],
              "the address field loads only the selected viewport");
        NSDate *loaded=[NSDate dateWithTimeIntervalSinceNow:20];
        [NSTimer scheduledTimerWithTimeInterval:.1 repeats:YES block:^(NSTimer *wait) {
            check(loaded.timeIntervalSinceNow>0,"the selected viewport loads its own page");
            if (![first.page.address containsString:@"id=own"]) return;
            [wait invalidate];
            check([second.page.address containsString:@"id=second"] && [third.page.address containsString:@"id=second"],
                  "the other viewports keep their pages");
            [app togglePill:nil];
            check(!surface.bars && first.bar.hidden && second.bar.hidden && third.bar.hidden && !first.caption.hidden,
                  "hiding the toolbar hides the address bars");
            [app togglePill:nil];
            check(surface.bars && !first.bar.hidden,"showing the toolbar shows the address bars");
            [open performClick:nil];
            [syncBox() performClick:nil];
            [app.toolPopover close];
            check(!surface.selected && page()==second.page,"the same page in all viewports clears the selection");
            NSDate *followed=[NSDate dateWithTimeIntervalSinceNow:20];
            [NSTimer scheduledTimerWithTimeInterval:.1 repeats:YES block:^(NSTimer *all) {
                check(followed.timeIntervalSinceNow>0,"the same page in all viewports loads the selected page everywhere");
                if (![second.page.address containsString:@"id=own"] || ![third.page.address containsString:@"id=own"]) return;
                [all invalidate];
                toggle(@[@0]);
                check(surface.subviews.count==2 && surface.subviews[0]==second && surface.subviews[1]==third && page()==second.page,
                      "a cleared viewport leaves the other viewports and their pages in place");
                toggle(@[@1,@4]);
                check(surface.subviews.count==1 && NSEqualRects(surface.subviews[0].frame,surface.bounds) &&
                      NSEqualSizes(page().bounds.size,app.deck.bounds.size) && surface.subviews[0].layer.cornerRadius==0 &&
                      ((Tile *)surface.subviews[0]).caption.hidden &&
                      CGColorGetAlpha(app.window.contentView.layer.backgroundColor)==1 && app.window.contentView.layer.cornerRadius==24,
                      "no viewports restores the full page");
                shortcut(@"q");
            }];
        }];
    }];
}
static void smoke(void) {
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"TestToolbar"]) {
        NSRect screen = app.window.screen.visibleFrame, start = app.window.frame;
        check(NSWidth(start) == MIN(1200, NSWidth(screen)) && NSHeight(start) == MIN(800, NSHeight(screen)), "startup size");
        check(fabs(NSMidX(start)-NSMidX(screen)) <= 1 && fabs(NSMidY(start)-NSMidY(screen)) <= 1, "startup centre");
        check(app.pill.visible && app.pill.parentWindow == app.window, "visible child toolbar");
        NSView *controls = app.pill.contentView;
        [app.pillAddress performClick:nil];
        NSTextView *addressEditor = (NSTextView *)app.pillEditor.currentEditor;
        check(addressEditor && !app.pillEditor.hidden, "address edits in pill");
        [addressEditor insertText:@"https://example.org/demo" replacementRange:NSMakeRange(0, addressEditor.string.length)];
        [addressEditor doCommandBySelector:@selector(cancelOperation:)];
        check(app.pillEditor.hidden && !app.pillAddress.hidden, "Escape cancels inline editing");
        NSButton *geometry = prefsButton(controls, @selector(quickGeometry:), NSNotFound);
        check(geometry.image != nil, "geometry button");
        [geometry performClick:nil];
        check(app.toolPopover.shown && app.quickFields.count == 4, "geometry popover");
        [app.quickFields[0] selectText:nil];
        NSTextView *editor = (NSTextView *)app.quickFields[0].currentEditor;
        check(editor != nil, "geometry popover accepts keyboard input");
        [editor insertText:@"640" replacementRange:NSMakeRange(0, editor.string.length)];
        check(NSWidth(app.window.frame) == 640 && app.toolPopover.shown, "popover resizes live");
        [editor insertText:@"99999" replacementRange:NSMakeRange(0, editor.string.length)];
        check(NSWidth(app.window.frame) == NSWidth(screen) && editor.string.integerValue == NSWidth(screen), "a width that is too large snaps to the screen width");
        [editor insertText:@"640" replacementRange:NSMakeRange(0, editor.string.length)];
        SizeSlider *slider = nil;
        for (SizeSlider *view in app.toolPopover.contentViewController.view.subviews)
            if ([view isKindOfClass:SizeSlider.class] && !view.index) slider = view;
        check(slider && slider.value == 640 && slider.maximum == NSWidth(screen) && slider.minimum == app.window.minSize.width, "width slider follows the frame");
        [slider accessibilityPerformIncrement];
        check(NSWidth(app.window.frame) == 650 && editor.string.integerValue == 650, "slider resizes the window and updates the field");
        [editor insertText:@"640" replacementRange:NSMakeRange(0, editor.string.length)];
        [editor doCommandBySelector:@selector(insertTab:)];
        check(app.quickFields[1].currentEditor != nil, "popover tab order");
        [app.toolPopover close];
        check(app.pill.firstResponder == app.pill, "closed popover leaves no editor in the toolbar panel");
        check(!prefsButton(controls, @selector(windowSettings:), NSNotFound) && !prefsButton(controls, @selector(fillWindow:), NSNotFound) &&
              !prefsButton(controls, @selector(toggleFullScreen:), NSNotFound), "toolbar omits window settings, fill and full screen");
        NSButton *hide = prefsButton(controls, @selector(togglePill:), NSNotFound);
        check(hide.imagePosition == NSImageOnly && NSWidth(hide.frame) == 32, "hide button shows no shortcut text");
        [geometry performClick:nil];
        [prefsButton(app.toolPopover.contentViewController.view, @selector(windowSettings:), NSNotFound) performClick:nil];
        check(app.preferences.visible && [app.section isEqual:@"Window"], "window settings deep link");
        [app.preferences close];
        [app fillWindow:nil];
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
    NSString *viewportsCase=[NSUserDefaults.standardUserDefaults stringForKey:@"TestViewports"];
    if (viewportsCase) { viewportsTest([viewportsCase isEqual:@"chromium"]); return; }
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
    [page() navigate:@"data:text/html,<meta%20name=theme-color%20content=coral><title>Chromium%20test</title><body%20style='background:gold'><script>let%20c=new%20AudioContext;setInterval(()=>{if(c.state=='running')document.title='Chromium%20autoplay'},20)</script>"];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:12];
    [NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *timer) {
        BOOL webkitReady=[app.pages[@(brow_id(0))].pageTitle isEqual:@"WebKit test"];
        if ((webkitReady && [page().pageTitle isEqual:@"Chromium autoplay"]) || deadline.timeIntervalSinceNow <= 0) {
            [timer invalidate]; verifySwitches();
        }
    }];
}
static void verifySwitches(void) {

    check([page().pageTitle isEqual:@"Chromium autoplay"],"Chromium rendered the document and permits autoplay with sound");
    shortcut(@"1");
    check([page().pageTitle isEqual:@"WebKit test"],"WebKit document survives switching");
    CALayer *layer=surfaceOf(page()).layer;
    BOOL reduced=NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion ||
        [NSUserDefaults.standardUserDefaults boolForKey:@"TestReduceMotion"];
    check(([layer animationForKey:@"transform"]!=nil)!=reduced,"depth respects Reduce Motion");
    check(([layer animationForKey:@"filters.depthBlur.inputRadius"]!=nil)!=reduced,"blur respects Reduce Motion");
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,65*NSEC_PER_MSEC),dispatch_get_main_queue(),^{
        shortcut(@"2"); shortcut(@"1"); shortcut(@"2");
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,350*NSEC_PER_MSEC),dispatch_get_main_queue(),^{
            check(brow_current()==brow_id(1),"rapid reversal ends on intended tab");
            for (NSView *v in app.deck.subviews) {
                check(v.hidden==(v!=surfaceOf(page())),"settled surface visibility");
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
