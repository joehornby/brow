- (void)buildPill {
    NSRect bounds = NSMakeRect(0, 0, 520, 42);
    self.pill = [[PillPanel alloc] initWithContentRect:bounds
        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
        backing:NSBackingStoreBuffered defer:NO];
    self.pill.becomesKeyOnlyIfNeeded = YES;
    self.pill.title = @"Window toolbar";
    self.pill.releasedWhenClosed = NO;
    self.pill.opaque = NO;
    self.pill.backgroundColor = NSColor.clearColor;
    self.pill.hasShadow = YES;
    self.pill.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary;
    NSView *controls = [[NSView alloc] initWithFrame:bounds];
    self.pillControls = controls;
    if (@available(macOS 26, *)) {
        NSGlassEffectView *glass = [[NSGlassEffectView alloc] initWithFrame:bounds];
        glass.cornerRadius = NSHeight(bounds) / 2;
        glass.contentView = controls;
        self.pill.contentView = glass;
    } else {
        NSVisualEffectView *material = [[NSVisualEffectView alloc] initWithFrame:bounds];
        material.material = NSVisualEffectMaterialHUDWindow;
        material.state = NSVisualEffectStateActive;
        material.wantsLayer = YES;
        material.layer.cornerRadius = NSHeight(bounds) / 2;
        material.layer.masksToBounds = YES;
        [material addSubview:controls];
        self.pill.contentView = material;
    }

    NSArray *commands = @[
        @[@"Back", @"chevron.left", @"back:"],
        @[@"Forward", @"chevron.right", @"forward:"],
        @[@"Reload", @"arrow.clockwise", @"refresh:"],
        @[@"Edit address", @"magnifyingglass", @"editPillAddress:"],
        @[@"Window size and position", @"square.resize", @"quickGeometry:"],
        @[@"Viewports", @"macbook.and.iphone", @"showViewports:"],
        @[@"Focus mode", @"viewfinder", @"toggleFocus:"],
        @[@"Centre on screen", @"scope", @"centreWindow:"],
        @[@"Settings", @"gearshape", @"settings:"],
        @[@"Shortcuts", @"keyboard", @"showShortcuts:"],
        @[@"Hide toolbar", @"eye.slash", @"togglePill:"]
    ];
    CGFloat x = 8;
    for (NSUInteger i = 0; i < commands.count; i++) {
        if (i == 4 || i == 8) {
            NSBox *divider = [[NSBox alloc] initWithFrame:NSMakeRect(x + 3, 13, 1, 16)];
            divider.boxType = NSBoxSeparator;
            [controls addSubview:divider];
            x += 8;
        }
        NSArray *command = commands[i];
        SEL action = NSSelectorFromString(command[2]);
        NSImage *image = [NSImage imageWithSystemSymbolName:command[1] accessibilityDescription:command[0]]
            ?: [NSImage imageWithSystemSymbolName:@"macwindow" accessibilityDescription:command[0]];
        NSButton *button = [NSButton buttonWithImage:image target:self action:action];
        button.bezelStyle = NSBezelStyleAccessoryBarAction;
        button.showsBorderOnlyWhileMouseInside = YES;
        [button addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
            options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
            owner:self userInfo:@{@"button":[NSValue valueWithNonretainedObject:button]}]];
        button.accessibilityLabel = command[0];
        button.frame = NSMakeRect(x, 5, 32, 32);
        if (action == @selector(back:)) self.pillBack = button;
        if (action == @selector(forward:)) self.pillForward = button;
        if (action == @selector(refresh:)) self.pillReload = button;
        if (action == @selector(editPillAddress:)) {
            self.pillAddress = button;
            button.frame = NSMakeRect(x, 5, 168, 32);
            button.font = [NSFont systemFontOfSize:12 weight:NSFontWeightMedium];
            button.imagePosition = NSImageLeft;
            button.lineBreakMode = NSLineBreakByTruncatingTail;
        }
        [controls addSubview:button];
        x += NSWidth(button.frame);
    }
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(pillResignedKey:) name:NSWindowDidResignKeyNotification object:self.pill];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(syncPillNavigation:) name:BrowNavigationChanged object:nil];
    if (![NSUserDefaults.standardUserDefaults boolForKey:@"toolbarHidden"]) [self attachPill];
}
- (void)editPillAddress:(id)sender {
    [self dismiss];
    if (!self.pillEditor) {
        self.pillEditor = [[NSTextField alloc] initWithFrame:NSInsetRect(self.pillAddress.frame, 1, 4)];
        self.pillEditor.placeholderString = @"Search or enter URL";
        self.pillEditor.accessibilityLabel = @"Address";
        self.pillEditor.bezelStyle = NSTextFieldRoundedBezel;
        self.pillEditor.delegate = self;
        [self.pillControls addSubview:self.pillEditor];
    }
    self.pillAddress.hidden = YES;
    self.pillEditor.hidden = NO;
    self.pillEditor.stringValue = page().address ?: @"";
    [self.pill makeKeyWindow];
    [self.pillEditor selectText:nil];
}
- (void)pillResignedKey:(NSNotification *)note { [self endPillEditing]; }
- (void)endPillEditing {
    self.pillEditor.hidden = YES;
    self.pillAddress.hidden = NO;
}
- (void)syncPillNavigation:(NSNotification *)note {
    if (self.quitting) return;
    [self followNavigation];
    NSView<BrowPage> *current = page();
    if (note.object && note.object != current) return;
    NSString *address = current.address;
    NSURL *url = address.length ? [NSURL URLWithString:address] : nil;
    NSString *label = url.host;
    if (label.length && url.port) label = [label stringByAppendingFormat:@":%@", url.port];
    if (!label.length) label = url.isFileURL ? url.lastPathComponent : nil;
    BOOL blank = !address.length || [address isEqual:@"about:blank"];
    self.pillAddress.title = label.length ? label : (blank ? @"Search or enter URL" : @"Page address");
    self.pillBack.enabled = current.canGoBack;
    self.pillForward.enabled = current.canGoForward;
    self.pillReload.enabled = !blank;
}
- (void)mouseEntered:(NSEvent *)event {
    NSButton *button = (NSButton *)[event.trackingArea.userInfo[@"button"] nonretainedObjectValue];
    [self hidePillTip];
    if (!button.enabled || self.toolPopover.shown) return;
    self.tipAnchor = button;
    NSTimeInterval delay = CACurrentMediaTime() - self.lastTipTime < .3 ? .04 : .4;
    [self performSelector:@selector(showPillTip:) withObject:button afterDelay:delay];
}
- (void)mouseExited:(NSEvent *)event {
    if ([event.trackingArea.userInfo[@"button"] nonretainedObjectValue] == self.tipAnchor) [self hidePillTip];
}
- (void)applicationDidResignActive:(NSNotification *)note { [self hidePillTip]; }
- (void)hidePillTip {
    if (self.tipAnchor)
        [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(showPillTip:) object:self.tipAnchor];
    self.tipAnchor = nil;
    if (self.pillTip.parentWindow) {
        self.lastTipTime = CACurrentMediaTime();
        [self.pill removeChildWindow:self.pillTip];
        [self.pillTip orderOut:nil];
    }
}
- (void)showPillTip:(NSButton *)button {
    if (button != self.tipAnchor || !self.pill.visible || !NSApp.active || self.toolPopover.shown) return;
    NSDictionary *keys = @{@"back:": @"⌘[", @"forward:": @"⌘]", @"refresh:": @"⌘R", @"settings:": @"⌘,", @"toggleFocus:": @"⇧⌘F",
        @"centreWindow:": @"⌥⌘C", @"togglePill:": @"⌘\\"};
    NSView *body = [NSView new];
    body.wantsLayer = YES;
    body.layer.backgroundColor = [NSColor colorWithWhite:.13 alpha:.97].CGColor;
    body.layer.cornerRadius = 8;
    body.layer.cornerCurve = kCACornerCurveContinuous;
    CGFloat x = 10;
    for (NSString *value in @[button.accessibilityLabel, keys[NSStringFromSelector(button.action)] ?: @""]) {
        if (!value.length) continue;
        NSTextField *label = [NSTextField labelWithString:value];
        BOOL title = x == 10;
        label.font = [NSFont systemFontOfSize:title ? 12 : 11 weight:NSFontWeightMedium];
        label.textColor = title ? NSColor.whiteColor : [NSColor colorWithWhite:.76 alpha:1];
        [label sizeToFit];
        [label setFrameOrigin:NSMakePoint(x, floor((28 - NSHeight(label.frame))/2))];
        [body addSubview:label];
        x = NSMaxX(label.frame) + 10;
    }
    NSRect anchor = [self.pill convertRectToScreen:[button convertRect:button.bounds toView:nil]];
    NSRect usable = self.pill.screen.visibleFrame;
    CGFloat left = MIN(NSMaxX(usable)-x, MAX(NSMinX(usable), round(NSMidX(anchor)-x/2)));
    NSRect frame = NSMakeRect(left, NSMaxY(self.pill.frame)+5, x, 28);
    if (!self.pillTip) {
        self.pillTip = [[NSPanel alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskBorderless |
            NSWindowStyleMaskNonactivatingPanel backing:NSBackingStoreBuffered defer:NO];
        self.pillTip.opaque = NO;
        self.pillTip.backgroundColor = NSColor.clearColor;
        self.pillTip.ignoresMouseEvents = YES;
        self.pillTip.hasShadow = YES;
        self.pillTip.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary;
    }
    [self.pillTip setFrame:frame display:NO];
    self.pillTip.contentView = body;
    [self.pill addChildWindow:self.pillTip ordered:NSWindowAbove];
}
- (void)updatePillActions {
    for (NSView *view in self.pillControls.subviews) {
        if (![view isKindOfClass:NSButton.class]) continue;
        NSButton *button = (NSButton *)view;
        if (button.action == @selector(centreWindow:))
            button.enabled = !self.inFullScreen;
        if (button.action == @selector(toggleFocus:)) {
            button.enabled = !self.inFullScreen || self.focusMode;
            button.accessibilityLabel = self.focusMode ? @"Exit focus mode" : @"Focus mode";
        }
    }
}
- (void)attachPill {
    [self.window addChildWindow:self.pill ordered:NSWindowAbove];
    [self positionPill];
}
- (void)positionPill {
    if (!self.pill.parentWindow || !self.window.screen) return;
    for (SizeSlider *slider in self.quickFields.firstObject.superview.subviews)
        if ([slider isKindOfClass:SizeSlider.class] && slider.dragging) return;
    NSRect screen = self.inFullScreen ? self.window.screen.frame : self.window.screen.visibleFrame;
    NSRect pageRect = self.focusMode ? [self.window convertRectToScreen:[self.deck convertRect:self.deck.bounds toView:nil]]
                                     : self.window.frame;
    NSSize size = self.pill.frame.size;
    CGFloat x = MAX(NSMinX(screen), MIN(NSMaxX(screen) - size.width, NSMidX(pageRect) - size.width / 2));
    CGFloat y = NSMinY(pageRect) - 8 - size.height;
    if (y < NSMinY(screen)) y = MAX(NSMinY(screen), NSMinY(pageRect)) + 8;
    [self.pill setFrameOrigin:NSMakePoint(round(x), round(y))];
    if (!self.toolPopover.shown || !self.popoverAnchor || [self roomyEdge] == self.popoverEdge) return;
    self.popoverEdge = [self roomyEdge];
    [self.toolPopover showRelativeToRect:self.popoverAnchor.bounds ofView:self.popoverAnchor preferredEdge:self.popoverEdge];
}
- (void)togglePill:(id)sender {
    if (self.focusTransition) return;
    BOOL hide = self.pill.parentWindow != nil;
    [NSUserDefaults.standardUserDefaults setBool:hide forKey:@"toolbarHidden"];
    if (hide) {
        [self endPillEditing];
        [self hidePillTip];
        [self.toolPopover close];
        [self.window removeChildWindow:self.pill];
        [self.pill orderOut:nil];
    } else [self attachPill];
}
- (void)centreWindow:(id)sender {
    if (!self.inFullScreen) [self applyFields:nil centre:YES];
}
- (void)fillWindow:(id)sender {
    if (!self.inFullScreen) [self.window setFrame:self.window.screen.visibleFrame display:YES];
}
- (void)windowSettings:(id)sender {
    [self.toolPopover close];
    self.section = @"Window";
    [self settings:nil];
}
- (void)showToolView:(NSView *)view from:(NSButton *)button {
    NSViewController *controller = [NSViewController new];
    controller.view = view;
    self.toolPopover = [NSPopover new];
    self.popoverAnchor = button;
    self.toolPopover.behavior = NSPopoverBehaviorTransient;
    self.toolPopover.animates = !NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
    self.toolPopover.contentViewController = controller;
    self.toolPopover.contentSize = view.frame.size;
    self.toolPopover.delegate = self;
    [self.pill makeKeyWindow];
    self.popoverEdge = [self roomyEdge];
    [self.toolPopover showRelativeToRect:button.bounds ofView:button preferredEdge:self.popoverEdge];
}
- (NSRectEdge)roomyEdge {
    NSRect screen = self.inFullScreen ? self.pill.screen.frame : self.pill.screen.visibleFrame;
    BOOL fits = NSMinY(self.pill.frame) - NSMinY(screen) >= self.toolPopover.contentSize.height + 24;
    return fits ? NSRectEdgeMaxY : NSRectEdgeMinY;
}
- (void)popoverWillClose:(NSNotification *)note {
    NSResponder *responder = self.pill.firstResponder;
    if ([responder isKindOfClass:NSView.class] && ((NSView *)responder).window != self.pill)
        [self.pill makeFirstResponder:nil];
}
- (void)popoverDidClose:(NSNotification *)note {
    if (note.object == self.toolPopover && self.pill.isKeyWindow && (!self.pillEditor || self.pillEditor.hidden) && !self.quitting)
        [self.window makeKeyWindow];
}
- (BOOL)prepareToolFrom:(NSButton *)button {
    BOOL toggleOff = self.toolPopover.shown && self.popoverAnchor == button;
    [self.toolPopover close];
    self.quickFields = nil;
    return !toggleOff;
}
- (void)quickGeometry:(NSButton *)button {
    if (![self prepareToolFrom:button]) return;
    NSView *body = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 482, 242)];
    self.quickFields = [self drawGeometry:body];
    NSButton *settings = [NSButton buttonWithTitle:@"Window Settings…" target:self action:@selector(windowSettings:)];
    settings.frame = NSMakeRect(312, 10, 153, 30);
    [body addSubview:settings];
    [self showToolView:body from:button];
}
- (void)showShortcuts:(NSButton *)button {
    if (![self prepareToolFrom:button]) return;
    NSView *body = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 476, 354)];
    NSArray *groups = @[
        @[@"Tabs and navigation", @[@"Address and tabs", @"⌘L"], @[@"New tab", @"⌘T"],
          @[@"Close tab or dialog", @"⌘W"], @[@"Switch to tab", @"⌘1–9"], @[@"Back / forward", @"⌘[ / ⌘]"],
          @[@"Reload", @"⌘R"], @[@"Select a result", @"↑ ↓"], @[@"Open result", @"Return"], @[@"Dismiss", @"Esc"]],
        @[@"Window", @[@"Move window", @"⌥ drag"], @[@"Centre", @"⌥⌘C"], @[@"Fill", @"⌥⌘F"],
          @[@"Full screen", @"⌃⌘F"], @[@"Focus mode", @"⇧⌘F"], @[@"Minimize", @"⌘M"],
          @[@"Show / hide toolbar", @"⌘\\"], @[@"Settings", @"⌘,"], @[@"Quit Brow", @"⌘Q"]]
    ];
    for (NSUInteger col = 0; col < groups.count; col++) {
        NSArray *group = groups[col];
        CGFloat x = 18 + col * 233;
        caption(body, group[0], NSMakeRect(x, 315, 205, 21), YES);
        for (NSUInteger row = 1; row < group.count; row++) {
            CGFloat y = 318 - row * 25;
            caption(body, group[row][0], NSMakeRect(x, y, 140, 19), NO);
            NSTextField *key = caption(body, group[row][1], NSMakeRect(x + 134, y, 72, 19), NO);
            key.alignment = NSTextAlignmentRight;
            key.textColor = NSColor.secondaryLabelColor;
        }
    }
    caption(body, @"Editing", NSMakeRect(18, 49, 100, 20), YES);
    caption(body, @"Undo ⌘Z     Cut ⌘X     Copy ⌘C     Paste ⌘V     Select all ⌘A",
            NSMakeRect(18, 22, 445, 20), NO);
    [self showToolView:body from:button];
}
