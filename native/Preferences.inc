static NSTextField *caption(NSView *parent, NSString *text, NSRect frame, BOOL headline) {
    NSTextField *label = [NSTextField labelWithString:text];
    label.frame = frame;
    label.font =
        headline ? [NSFont systemFontOfSize:14 weight:NSFontWeightSemibold] : [NSFont systemFontOfSize:13];
    [parent addSubview:label];
    return label;
}

static NSBox *groupBox(NSView *parent, NSRect rect) {
    NSBox *box = [[NSBox alloc] initWithFrame:rect];
    box.boxType = NSBoxCustom;
    box.titlePosition = NSNoTitle;
    box.cornerRadius = 12;
    box.borderWidth = 1;
    box.borderColor = NSColor.separatorColor;
    box.fillColor = NSColor.controlBackgroundColor;
    box.contentViewMargins = NSZeroSize;
    [parent addSubview:box];
    return box;
}

static NSColor *solidColor(void) {
    NSArray *rgb = [NSUserDefaults.standardUserDefaults arrayForKey:@"focusColor"];
    if (rgb.count != 3)
        return [NSColor colorWithSRGBRed:.12 green:.13 blue:.16 alpha:1];
    return [NSColor colorWithSRGBRed:[rgb[0] doubleValue]
                               green:[rgb[1] doubleValue]
                                blue:[rgb[2] doubleValue]
                               alpha:1];
}

static NSArray *siteGradient(NSImage *image) {
    CGImageRef source = [image CGImageForProposedRect:NULL context:nil hints:nil];
    if (!source)
        return nil;
    unsigned char pixels[16 * 16 * 4];
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx =
        CGBitmapContextCreate(pixels, 16, 16, 8, 64, space, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    if (!ctx)
        return nil;
    CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
    CGContextFillRect(ctx, CGRectMake(0, 0, 16, 16));
    CGContextSetInterpolationQuality(ctx, kCGInterpolationMedium);
    CGContextDrawImage(ctx, CGRectMake(0, 0, 16, 16), source);
    CGContextRelease(ctx);
    unsigned sums[64][3] = {0}, counts[64] = {0};
    for (int i = 0; i < 256; i++) {
        unsigned char *p = pixels + 4 * i;
        int bin = (p[0] >> 6) * 16 + (p[1] >> 6) * 4 + (p[2] >> 6);
        counts[bin]++;
        for (int c = 0; c < 3; c++)
            sums[bin][c] += p[c];
    }
    double best = -1;
    int accent = 0, base = 0;
    for (int i = 0; i < 64; i++) {
        if (!counts[i])
            continue;
        if (counts[i] > counts[base])
            base = i;
        unsigned *c = sums[i];
        double chroma = (MAX(c[0], MAX(c[1], c[2])) - MIN(c[0], MIN(c[1], c[2]))) / (255. * counts[i]);
        double score = sqrt(counts[i]) * (.2 + chroma * 4);
        if (score > best) {
            best = score;
            accent = i;
        }
    }
    FocusLCH tones[2];
    int bins[] = {accent, base};
    for (int n = 0; n < 2; n++) {
        int i = bins[n];
        double d = 255. * counts[i];
        tones[n] = focusLCH(sums[i][0] / d, sums[i][1] / d, sums[i][2] / d);
    }
    if (tones[1].C < .035 && tones[0].C > .035) {
        tones[1].h = tones[0].h + .19;
        tones[1].C = tones[0].C * .64;
    }
    tones[0].L = .29 + .35 * tones[0].L;
    tones[0].C = MIN(.16, tones[0].C);
    tones[1].L = .18 + .16 * tones[1].L;
    tones[1].C = MIN(.10, tones[1].C);
    return focusStops(tones[0], tones[1]);
}

- (void)setBackdropColors:(NSArray *)target duration:(CFTimeInterval)duration {
    if ([self.backdrop.colors isEqual:target])
        return;
    NSString *key = @"siteColors";
    NSArray *from = ((CAGradientLayer *)self.backdrop.presentationLayer).colors ?: self.backdrop.colors;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.backdrop.colors = target;
    [self.backdrop removeAnimationForKey:key];
    if (duration > 0 && from.count == target.count &&
        !NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion) {
        NSMutableArray *frames = [NSMutableArray arrayWithObject:from];
        FocusLCH start[FocusStopCount], end[FocusStopCount];
        for (int i = 0; i < FocusStopCount; i++) {
            start[i] = focusLCHFromStop(from[i]);
            end[i] = focusLCHFromStop(target[i]);
        }
        for (int step = 1; step < 8; step++) {
            NSMutableArray *stops = [NSMutableArray arrayWithCapacity:FocusStopCount];
            for (int i = 0; i < FocusStopCount; i++)
                [stops addObject:(id)focusColor(mixLCH(start[i], end[i], step / 8.)).CGColor];
            [frames addObject:stops];
        }
        [frames addObject:target];
        CAKeyframeAnimation *blend = [CAKeyframeAnimation animationWithKeyPath:@"colors"];
        blend.values = frames;
        blend.duration = duration;
        blend.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
        [self.backdrop addAnimation:blend forKey:key];
    }
    [CATransaction commit];
}

- (void)refreshBackdrop {
    if (!self.focusMode)
        return;
    BOOL gradient = ![NSUserDefaults.standardUserDefaults boolForKey:@"solidBackdrop"];
    NSView<BrowPage> *p = page();
    NSArray *key = @[ @(brow_current()), p.address ?: @"", p.pageTitle ?: @"", @(gradient) ];
    BOOL same = [key isEqual:self.lastSampleKey];
    if (same && (!gradient || CACurrentMediaTime() < self.nextPaletteSample))
        return;
    self.lastSampleKey = key;
    NSUInteger version = ++self.sampleVersion;
    if (!gradient || !self.backdrop.colors.count) {
        NSMutableArray *flat = [NSMutableArray arrayWithCapacity:FocusStopCount];
        NSColor *solid = solidColor();
        for (int i = 0; i < FocusStopCount; i++)
            [flat addObject:(id)solid.CGColor];
        [self setBackdropColors:flat duration:0];
        self.hasSitePalette = NO;
    }
    if (!gradient || !p || self.focusTransition)
        return;
    if (p.loading) {
        self.lastSampleKey = nil;
        return;
    }
    self.nextPaletteSample = CACurrentMediaTime() + 18;
    [p samplePage:^(NSImage *image) {
      if (!self.focusMode || self.focusTransition || self.sampleVersion != version || !image)
          return;
      NSArray *colors = siteGradient(image);
      if (!colors)
          return;
      [self setBackdropColors:colors duration:self.hasSitePalette ? 14 : 6];
      self.hasSitePalette = YES;
    }];
}

- (void)layoutFocus {
    NSRect bounds = self.window.contentView.bounds;
    CGFloat w = MIN(NSWidth(self.preferredFrame), NSWidth(bounds)),
            h = MIN(NSHeight(self.preferredFrame), NSHeight(bounds));
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.backdrop.frame = bounds;
    self.deck.frame = NSMakeRect(round((NSWidth(bounds) - w) / 2), round((NSHeight(bounds) - h) / 2), w, h);
    self.deck.layer.cornerRadius = NSEqualSizes(self.deck.frame.size, bounds.size) || self.viewports.count ? 0 : 24;
    self.deck.layer.cornerCurve = kCACornerCurveContinuous;
    self.deck.layer.masksToBounds = YES;
    [CATransaction commit];
    [self positionPill];
}
- (void)toggleFocus:(id)sender {
    if (self.focusTransition) return;
    [self.preferences close];
    [self.window makeKeyAndOrderFront:nil];
    if (self.focusMode || self.inFullScreen) {
        [self.window toggleFullScreen:nil];
        return;
    }
    self.preferredFrame = self.window.frame;
    self.focusMode = YES;
    [self beginFocusTransition];
    self.deck.autoresizingMask = NSViewNotSizable;
    self.backdrop.hidden = NO;
    [self layoutFocus];
    [self refreshBackdrop];
    self.sampleTimer = [NSTimer scheduledTimerWithTimeInterval:1.5
                                                        target:self
                                                      selector:@selector(checkFocusPage:)
                                                      userInfo:nil
                                                       repeats:YES];
    self.sampleTimer.tolerance = .5;
    [self.window toggleFullScreen:nil];
}
- (void)checkFocusPage:(NSTimer *)timer {
    if (NSApp.active && self.window.visible)
        [self refreshBackdrop];
}
- (void)finishFocus {
    if (!self.focusMode)
        return;
    self.focusMode = NO;
    self.sampleVersion++;
    self.lastSampleKey = nil;
    [self.sampleTimer invalidate];
    self.sampleTimer = nil;
    self.backdrop.hidden = YES;
    [self.backdrop removeAnimationForKey:@"siteColors"];
    self.backdrop.colors = nil;
    self.hasSitePalette = NO;
    self.deck.layer.cornerRadius = 0;
    self.deck.layer.masksToBounds = NO;
    self.deck.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    if (!NSEqualRects(self.window.frame, self.preferredFrame))
        [self.window setFrame:self.preferredFrame display:YES];
    self.deck.frame = self.window.contentView.bounds;
}

- (NSArray *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar {
    return @[ @"Browser", @"Window", @"Icon" ];
}
- (NSArray *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {
    return [self toolbarAllowedItemIdentifiers:toolbar];
}
- (NSArray *)toolbarSelectableItemIdentifiers:(NSToolbar *)toolbar {
    return [self toolbarAllowedItemIdentifiers:toolbar];
}
- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar
        itemForItemIdentifier:(NSString *)identifier
    willBeInsertedIntoToolbar:(BOOL)inserting {
    NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
    NSDictionary *symbols = @{@"Browser" : @"globe", @"Window" : @"macwindow", @"Icon" : @"app.dashed"};
    item.label = identifier;
    item.image = [NSImage imageWithSystemSymbolName:symbols[identifier] accessibilityDescription:identifier];
    item.target = self;
    item.action = @selector(chooseSection:);
    return item;
}
- (void)chooseSection:(NSToolbarItem *)sender {
    self.section = sender.itemIdentifier;
    [self drawPreferences];
}
- (void)settings:(id)sender {
    [self dismiss];
    if (!self.preferences) {
        self.preferences =
            [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 540, 338)
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        self.preferences.releasedWhenClosed = NO;
        self.preferences.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorMoveToActiveSpace;
        self.preferences.toolbarStyle = NSWindowToolbarStylePreference;
        NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"BrowPreferences"];
        toolbar.delegate = self;
        toolbar.displayMode = NSToolbarDisplayModeIconAndLabel;
        self.preferences.toolbar = toolbar;
        [self.preferences center];
        if (!self.section) self.section = @"Icon";
    }
    [self drawPreferences];
    [self.preferences makeKeyAndOrderFront:nil];
}
- (void)drawPreferences {
    self.preferences.title = self.section;
    self.preferences.toolbar.selectedItemIdentifier = self.section;
    NSRect old = self.preferences.frame;
    NSRect sized = [self.preferences frameRectForContentRect:NSMakeRect(0, 0, 540, [self.section isEqual:@"Window"] ? 430 : 338)];
    sized.origin = NSMakePoint(NSMinX(old), NSMaxY(old) - NSHeight(sized));
    [self.preferences setFrame:sized display:YES];
    NSView *body = self.preferences.contentView;
    for (NSView *v in body.subviews.copy)
        [v removeFromSuperview];
    self.geometryFields = nil;
    if ([self.section isEqual:@"Icon"])
        [self drawIcons:body];
    else if ([self.section isEqual:@"Window"])
        [self drawWindowOptions:body];
    else
        [self drawEngines:body];
}
- (void)drawIcons:(NSView *)body {
    NSArray *names = iconNames();
    NSView *list = groupBox(body, NSMakeRect(36, 24, 468, 290)).contentView;
    for (NSUInteger i = 0; i < names.count; i++) {
        NSString *name = names[i];
        CGFloat y = 217 - i * 72;
        BOOL active = [name isEqual:selectedIcon()];
        NSButton *radio = [NSButton radioButtonWithTitle:@"" target:self action:@selector(pickIcon:)];
        radio.frame = NSMakeRect(15, y + 24, 20, 24);
        radio.state = active;
        radio.tag = i;
        radio.accessibilityLabel = name;
        [list addSubview:radio];
        NSImageView *art = [NSImageView imageViewWithImage:iconImage(name)];
        art.frame = NSMakeRect(45, y + 8, 56, 56);
        [list addSubview:art];
        caption(list, name, NSMakeRect(116, y + (active ? 35 : 24), 260, 21), YES);
        if (active) {
            NSTextField *marker = caption(list, @"Current icon", NSMakeRect(116, y + 14, 260, 18), NO);
            marker.textColor = NSColor.controlAccentColor;
        }
        NSButton *row = [NSButton buttonWithTitle:@"" target:self action:@selector(pickIcon:)];
        row.transparent = YES;
        row.frame = NSMakeRect(40, y, 422, 72);
        row.tag = i;
        row.accessibilityLabel = name;
        row.refusesFirstResponder = YES;
        [list addSubview:row];
        if (i + 1 < names.count) {
            NSBox *line = [[NSBox alloc] initWithFrame:NSMakeRect(15, y, 436, 1)];
            line.boxType = NSBoxSeparator;
            [list addSubview:line];
        }
    }
}
- (void)pickIcon:(NSButton *)sender {
    NSString *name = iconNames()[sender.tag];
    NSImage *image = iconImage(name);
    if (!image) {
        NSBeep();
        return;
    }
    NSApp.applicationIconImage = image;
    [NSUserDefaults.standardUserDefaults setObject:name forKey:@"appIcon"];
    [self drawPreferences];
}
- (void)drawEngines:(NSView *)body {
    NSView *card = groupBox(body, NSMakeRect(36, 154, 468, 160)).contentView;
    for (NSUInteger i = 0; i < 2; i++) {
        CGFloat y = 111 - i * 44;
        NSString *label = i ? @"Current tab" : @"New tabs";
        caption(card, label, NSMakeRect(18, y, 160, 22), YES);
        NSPopUpButton *picker = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(262, y - 2, 184, 26)];
        [picker addItemsWithTitles:@[ @"WebKit", @"Chromium" ]];
        picker.autoenablesItems = NO;
        [picker itemAtIndex:1].enabled = [NSFileManager.defaultManager fileExistsAtPath:pluginPath()];
        [picker selectItemAtIndex:i ? chromium() : defaultChromium()];
        picker.tag = i;
        picker.target = self;
        picker.action = @selector(changeEngine:);
        picker.accessibilityLabel = label;
        [card addSubview:picker];
    }
    NSTextField *hint = caption(card, @"Changing engines reopens the page with separate cookies and history.",
                                NSMakeRect(18, 17, 432, 32), NO);
    hint.textColor = NSColor.secondaryLabelColor;
    hint.font = [NSFont systemFontOfSize:12];
    hint.maximumNumberOfLines = 2;
}
- (void)changeEngine:(NSPopUpButton *)sender {
    BOOL useChromium = sender.indexOfSelectedItem == 1;
    if (!sender.tag)
        [NSUserDefaults.standardUserDefaults setBool:useChromium forKey:@"chromium"];
    else if (useChromium != chromium()) {
        NSString *url = page().address;
        if (brow_new(useChromium, true)) {
            if (url.length)
                navigate(url);
        } else
            [sender selectItemAtIndex:chromium()];
    }
}

- (NSArray<NSTextField *> *)drawGeometry:(NSView *)geometry {
    NSRect frame = self.focusMode ? self.preferredFrame : self.window.frame;
    NSRect screen = self.window.screen.visibleFrame;
    NSMutableArray *fields = [NSMutableArray array];
    NSArray *names = @[ @"Width", @"Height", @"Left", @"Top" ];
    NSInteger values[] = {lround(NSWidth(frame)), lround(NSHeight(frame)),
                          lround(NSMinX(frame) - NSMinX(screen)), lround(NSMaxY(screen) - NSMaxY(frame))};
    caption(geometry, @"Window frame", NSMakeRect(15, 202, 150, 22), YES);
    BOOL editable = !self.inFullScreen || self.focusMode;
    for (NSUInteger i = 0; i < 2; i++) {
        SizeSlider *slider = [[SizeSlider alloc] initWithFrame:NSMakeRect(16, 158 - i * 44, 450, 36) title:names[i]];
        slider.index = i;
        slider.minimum = i ? self.window.minSize.height : self.window.minSize.width;
        slider.maximum = i ? NSHeight(screen) : NSWidth(screen);
        slider.value = values[i];
        slider.enabled = editable;
        slider.changed = ^(double value) {
            NSTextField *field = fields[i];
            NSString *text = [NSString stringWithFormat:@"%.0f", value];
            if (field.currentEditor) field.currentEditor.string = text;
            else field.stringValue = text;
            [app applyFields:fields centre:NO];
        };
        slider.released = ^{ [app positionPill]; };
        [geometry addSubview:slider];
    }
    for (NSUInteger i = 0; i < 4; i++) {
        CGFloat x = 16 + i * 117;
        caption(geometry, names[i], NSMakeRect(x, 85, 104, 18), NO).textColor = NSColor.secondaryLabelColor;
        NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(x, 56, 101, 25)];
        field.integerValue = values[i];
        field.accessibilityLabel = names[i];
        field.delegate = self;
        [geometry addSubview:field];
        [fields addObject:field];
    }

    NSButton *centre = [NSButton buttonWithTitle:@"Centre on screen"
                                          target:self
                                          action:@selector(applyGeometry:)];
    centre.tag = 1;

    centre.frame = NSMakeRect(14, 10, 146, 30);

    [geometry addSubview:centre];
    for (NSTextField *field in fields)
        field.enabled = editable;
    centre.enabled = editable;
    for (NSUInteger i = 1; i < fields.count; i++)
        [fields[i - 1] setNextKeyView:fields[i]];
    [fields.lastObject setNextKeyView:centre];

    return fields;
}
- (void)drawWindowOptions:(NSView *)body {
    self.geometryFields = [self drawGeometry:groupBox(body, NSMakeRect(28, 167, 484, 238)).contentView];
    NSView *focus = groupBox(body, NSMakeRect(28, 23, 484, 130)).contentView;
    caption(focus, @"Focus full screen", NSMakeRect(15, 94, 180, 22), YES);
    NSPopUpButton *style = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(13, 52, 185, 28)];
    [style addItemsWithTitles:@[ @"Website gradient", @"Solid colour" ]];
    BOOL solid = [NSUserDefaults.standardUserDefaults boolForKey:@"solidBackdrop"];
    [style selectItemAtIndex:solid];
    style.target = self;
    style.action = @selector(changeBackdrop:);
    [focus addSubview:style];
    if (solid) {
        NSColorWell *well = [[NSColorWell alloc] initWithFrame:NSMakeRect(206, 53, 48, 27)];
        well.color = solidColor();
        well.target = self;
        well.action = @selector(changeSolid:);
        [focus addSubview:well];
    }
    NSButton *toggle = [NSButton buttonWithTitle:self.focusMode ? @"Exit focus" : @"Enter focus"
                                          target:self
                                          action:@selector(toggleFocus:)];
    toggle.frame = NSMakeRect(350, 51, 117, 30);
    toggle.enabled = !self.inFullScreen || self.focusMode;
    [focus addSubview:toggle];
    NSTextField *hint = caption(focus, @"Keeps the page centred at your window size.    ⇧⌘F",
                                NSMakeRect(16, 18, 447, 19), NO);
    hint.textColor = NSColor.secondaryLabelColor;
    hint.font = [NSFont systemFontOfSize:12];
}
- (void)syncGeometryFields {
    NSRect frame = self.focusMode ? self.preferredFrame : self.window.frame;
    NSRect screen = self.window.screen.visibleFrame;
    NSInteger values[] = {lround(NSWidth(frame)), lround(NSHeight(frame)),
                          lround(NSMinX(frame) - NSMinX(screen)), lround(NSMaxY(screen) - NSMaxY(frame))};
    for (NSArray *fields in @[self.geometryFields ?: @[], self.quickFields ?: @[]]) {
        for (NSUInteger i = 0; i < fields.count; i++) {
            NSTextField *field = fields[i];
            if (!field.currentEditor) field.integerValue = values[i];
        }
        for (SizeSlider *slider in [fields.firstObject superview].subviews) {
            if (![slider isKindOfClass:SizeSlider.class]) continue;
            slider.maximum = slider.index ? NSHeight(screen) : NSWidth(screen);
            if (!slider.dragging) slider.value = values[slider.index];
        }
    }
}
- (void)applyGeometry:(NSButton *)centre {
    NSArray *fields = centre.window && centre.window == self.quickFields.firstObject.window ? self.quickFields : self.geometryFields;
    [self applyFields:fields centre:centre != nil];
}
- (void)applyFields:(NSArray<NSTextField *> *)fields centre:(BOOL)centre {
    if (self.inFullScreen && !self.focusMode) return;
    NSRect frame = self.focusMode ? self.preferredFrame : self.window.frame;
    NSRect screen = self.window.screen.visibleFrame;
    CGFloat v[] = {NSWidth(frame), NSHeight(frame), NSMinX(frame) - NSMinX(screen),
                   NSMaxY(screen) - NSMaxY(frame)};
    NSUInteger used = MIN(fields.count, centre ? 2 : 4);
    NSInteger typed[4] = {0};
    BOOL parsed[4] = {NO};
    for (NSUInteger i = 0; i < used; i++) {
        NSTextField *field = fields[i];
        NSString *text = field.currentEditor ? field.currentEditor.string : field.stringValue;
        NSScanner *scanner = [NSScanner scannerWithString:text];
        if (![scanner scanInteger:&typed[i]] || !scanner.isAtEnd) continue;
        parsed[i] = YES;
        if (i == 0 && typed[i] < self.window.minSize.width) continue;
        if (i == 1 && typed[i] < self.window.minSize.height) continue;
        v[i] = i == 0 ? MIN(typed[i], NSWidth(screen)) : i == 1 ? MIN(typed[i], NSHeight(screen)) : typed[i];
    }
    CGFloat left =
        centre ? round((NSWidth(screen) - v[0]) / 2) : MIN(MAX(0, v[2]), MAX(0, NSWidth(screen) - v[0]));
    CGFloat top =
        centre ? round((NSHeight(screen) - v[1]) / 2) : MIN(MAX(0, v[3]), MAX(0, NSHeight(screen) - v[1]));
    frame = NSMakeRect(NSMinX(screen) + left, NSMaxY(screen) - top - v[1], v[0], v[1]);
    CGFloat applied[] = {v[0], v[1], left, top};
    for (NSUInteger i = 0; i < used; i++) {
        if (!parsed[i] || typed[i] <= applied[i]) continue;
        NSString *snapped = [@(lround(applied[i])) stringValue];
        if (fields[i].currentEditor) fields[i].currentEditor.string = snapped;
        else fields[i].stringValue = snapped;
    }
    if (centre)
        [fields.firstObject.window makeFirstResponder:nil];
    if (self.focusMode) {
        self.preferredFrame = frame;
        [self layoutFocus];
    } else if (!NSEqualRects(frame, self.window.frame))
        [self.window setFrame:frame display:YES animate:NO];
    [self syncGeometryFields];
}
- (void)changeBackdrop:(NSPopUpButton *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.indexOfSelectedItem == 1 forKey:@"solidBackdrop"];
    self.lastSampleKey = nil;
    [self refreshBackdrop];
    [self drawPreferences];
}
- (void)changeSolid:(NSColorWell *)sender {
    NSColor *color = [sender.color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (!color)
        return;
    [NSUserDefaults.standardUserDefaults
        setObject:@[ @(color.redComponent), @(color.greenComponent), @(color.blueComponent) ]
           forKey:@"focusColor"];
    self.lastSampleKey = nil;
    [self refreshBackdrop];
}
