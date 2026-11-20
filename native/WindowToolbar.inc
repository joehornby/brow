// A child panel keeps the controls outside the page's layout and hit-test area.
- (void)buildPill {
    NSRect bounds = NSMakeRect(0, 0, 354, 42);
    self.pill = [[NSPanel alloc] initWithContentRect:bounds
        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
        backing:NSBackingStoreBuffered defer:NO];
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
        @[@"Settings", @"gearshape", @"settings:"],
        @[@"Window Settings", @"macwindow", @"windowSettings:"],
        @[@"Window size and position", @"slider.horizontal.3", @"quickGeometry:"],
        @[@"Focus mode", @"viewfinder", @"toggleFocus:"],
        @[@"Fill screen", @"rectangle.inset.filled", @"fillWindow:"],
        @[@"Full screen", @"arrow.up.left.and.arrow.down.right", @"toggleFullScreen:"],
        @[@"Centre on screen", @"scope", @"centreWindow:"],
        @[@"Shortcuts", @"keyboard", @"showShortcuts:"],
        @[@"Hide toolbar", @"eye.slash", @"togglePill:"]
    ];
    CGFloat x = 8;
    for (NSUInteger i = 0; i < commands.count; i++) {
        if (i == 3 || i == 7) {
            NSBox *divider = [[NSBox alloc] initWithFrame:NSMakeRect(x + 3, 13, 1, 16)];
            divider.boxType = NSBoxSeparator;
            [controls addSubview:divider];
            x += 8;
        }
        NSArray *command = commands[i];
        SEL action = NSSelectorFromString(command[2]);
        NSButton *button = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:command[1]
            accessibilityDescription:command[0]] target:action == @selector(toggleFullScreen:) ? self.window : self action:action];
        button.bezelStyle = NSBezelStyleAccessoryBarAction;
        button.showsBorderOnlyWhileMouseInside = YES;
        [button addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
            options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
            owner:self userInfo:@{@"button":[NSValue valueWithNonretainedObject:button]}]];
        button.accessibilityLabel = command[0];
        button.frame = NSMakeRect(x, 5, 32, 32);
        if (action == @selector(togglePill:)) {
            button.title = @"⌘\\";
            button.imagePosition = NSImageLeft;
            button.font = [NSFont systemFontOfSize:10 weight:NSFontWeightMedium];
            button.contentTintColor = NSColor.secondaryLabelColor;
            button.frame = NSMakeRect(x, 5, 66, 32);
        }
        [controls addSubview:button];
        x += NSWidth(button.frame);
    }
    if (![NSUserDefaults.standardUserDefaults boolForKey:@"toolbarHidden"]) [self attachPill];
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
    NSDictionary *keys = @{@"settings:": @"⌘,", @"toggleFocus:": @"⇧⌘F", @"fillWindow:": @"⌥⌘F",
        @"toggleFullScreen:": @"⌃⌘F", @"centreWindow:": @"⌥⌘C", @"togglePill:": @"⌘\\"};
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
        if (button.action == @selector(centreWindow:) || button.action == @selector(fillWindow:))
            button.enabled = !self.inFullScreen;
        if (button.action == @selector(toggleFocus:)) {
            button.enabled = !self.inFullScreen || self.focusMode;
            button.accessibilityLabel = self.focusMode ? @"Exit focus mode" : @"Focus mode";
        }
        if (button.action == @selector(toggleFullScreen:))
            button.accessibilityLabel = self.inFullScreen ? @"Exit full screen" : @"Full screen";
    }
}
- (void)attachPill {
    [self.window addChildWindow:self.pill ordered:NSWindowAbove];
    [self positionPill];
}
- (void)positionPill {
    if (!self.pill.parentWindow || !self.window.screen) return;
    NSRect screen = self.inFullScreen ? self.window.screen.frame : self.window.screen.visibleFrame;
    NSRect pageRect = self.focusMode ? [self.window convertRectToScreen:[self.deck convertRect:self.deck.bounds toView:nil]]
                                     : self.window.frame;
    NSSize size = self.pill.frame.size;
    CGFloat x = MAX(NSMinX(screen), MIN(NSMaxX(screen) - size.width, NSMidX(pageRect) - size.width / 2));
    CGFloat y = NSMinY(pageRect) - 8 - size.height;
    if (y < NSMinY(screen)) y = MAX(NSMinY(screen), NSMinY(pageRect)) + 8;
    [self.pill setFrameOrigin:NSMakePoint(round(x), round(y))];
}
- (void)togglePill:(id)sender {
    if (self.focusTransition) return;
    BOOL hide = self.pill.parentWindow != nil;
    [NSUserDefaults.standardUserDefaults setBool:hide forKey:@"toolbarHidden"];
    if (hide) {
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
    [self.toolPopover showRelativeToRect:button.bounds ofView:button preferredEdge:NSRectEdgeMaxY];
}
- (BOOL)prepareToolFrom:(NSButton *)button {
    BOOL toggleOff = self.toolPopover.shown && self.popoverAnchor == button;
    [self.toolPopover close];
    self.quickFields = nil;
    return !toggleOff;
}
- (void)quickGeometry:(NSButton *)button {
    if (![self prepareToolFrom:button]) return;
    NSView *body = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 482, 150)];
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
