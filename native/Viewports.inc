- (void)showViewports:(NSButton *)button {
    if (![self prepareToolFrom:button]) return;
    NSArray<NSArray *> *presets = viewportPresets();
    CGFloat height = 132 + presets.count * 28;
    NSView *body = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 288, height)];
    caption(body, @"Viewports", NSMakeRect(18, height - 39, 252, 21), YES);
    for (NSUInteger i = 0; i < presets.count; i++) {
        CGFloat y = height - 72 - i * 28;
        NSButton *box = [NSButton checkboxWithTitle:presets[i][0] target:self action:@selector(toggleViewport:)];
        box.frame = NSMakeRect(18, y, 160, 22);
        box.tag = i;
        box.state = [self.viewports containsObject:@(i)];
        [body addSubview:box];
        NSString *size = [NSString stringWithFormat:@"%@ × %@", presets[i][1], presets[i][2]];
        NSTextField *detail = caption(body, size, NSMakeRect(178, y + 1, 92, 19), NO);
        detail.alignment = NSTextAlignmentRight;
        detail.textColor = NSColor.secondaryLabelColor;
    }
    NSBox *line = [[NSBox alloc] initWithFrame:NSMakeRect(18, 88, 252, 1)];
    line.boxType = NSBoxSeparator;
    [body addSubview:line];
    NSButton *sync = [NSButton checkboxWithTitle:@"Follow links in all viewports" target:self
                                          action:@selector(toggleViewportSync:)];
    sync.frame = NSMakeRect(18, 54, 252, 22);
    sync.state = ![NSUserDefaults.standardUserDefaults boolForKey:@"isolateViewports"];
    [body addSubview:sync];
    NSTextField *hint = caption(body, @"Each viewport keeps its size and scales to fit the window frame.",
                                NSMakeRect(18, 14, 252, 32), NO);
    hint.textColor = NSColor.secondaryLabelColor;
    hint.font = [NSFont systemFontOfSize:12];
    hint.maximumNumberOfLines = 2;
    [self showToolView:body from:button];
}
- (void)toggleViewport:(NSButton *)sender {
    NSMutableArray<NSNumber *> *chosen = [NSMutableArray arrayWithArray:self.viewports ?: @[]];
    if (sender.state) [chosen addObject:@(sender.tag)];
    else [chosen removeObject:@(sender.tag)];
    self.viewports = [chosen sortedArrayUsingSelector:@selector(compare:)];
    [self syncViewports];
}
- (void)toggleViewportSync:(NSButton *)sender {
    [NSUserDefaults.standardUserDefaults setBool:!sender.state forKey:@"isolateViewports"];
}
- (void)followNavigation {
    NSArray<Tile *> *tiles = surfaceOf(page()).subviews;
    Tile *leader = nil;
    for (Tile *tile in tiles)
        if ([tile led] && !leader) leader = tile;
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"isolateViewports"]) return;
    for (Tile *tile in tiles)
        if (leader && tile != leader && ![tile.page.address isEqual:leader.address]) [tile follow:leader.address];
}
- (void)syncViewports {
    NSView<BrowPage> *primary = page();
    Surface *surface = surfaceOf(primary);
    if (!surface || self.quitting) return;
    NSMutableArray<NSValue *> *sizes = [NSMutableArray array];
    NSMutableArray<NSString *> *titles = [NSMutableArray array];
    for (NSNumber *index in self.viewports) {
        NSArray *preset = viewportPresets()[index.unsignedIntegerValue];
        [sizes addObject:[NSValue valueWithSize:NSMakeSize([preset[1] doubleValue], [preset[2] doubleValue])]];
        [titles addObject:[NSString stringWithFormat:@"%@ · %@ × %@", preset[0], preset[1], preset[2]]];
    }
    [surface discard:MAX(1, sizes.count)];
    while (surface.subviews.count < sizes.count) {
        NSView<BrowPage> *extra = createPage(chromium());
        if (!extra) break;
        [surface add:extra];
        if (primary.address.length) [(Tile *)surface.subviews.lastObject follow:primary.address];
    }
    surface.viewports = sizes;
    surface.titles = titles;
    [self updateWindowCorners];
    if (self.focusMode) [self layoutFocus];
    [surface arrange];
}
