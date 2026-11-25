static NSString *viewportHint(void) {
    return separatePages() ? @"Type in the bar under a viewport to change its page."
                           : @"Each viewport keeps its size and scales to fit the window frame.";
}
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
    NSButton *sync = [NSButton checkboxWithTitle:@"Same page in all viewports" target:self
                                          action:@selector(toggleViewportSync:)];
    sync.frame = NSMakeRect(18, 54, 252, 22);
    sync.state = !separatePages();
    [body addSubview:sync];
    NSTextField *hint = caption(body, viewportHint(), NSMakeRect(18, 14, 252, 32), NO);
    hint.identifier = @"viewportHint";
    hint.textColor = NSColor.secondaryLabelColor;
    hint.font = [NSFont systemFontOfSize:12];
    hint.maximumNumberOfLines = 2;
    [self showToolView:body from:button];
}
- (void)toggleViewport:(NSButton *)sender {
    NSMutableArray<NSNumber *> *chosen = [NSMutableArray arrayWithArray:self.viewports ?: @[]];
    if (sender.state) [chosen addObject:@(sender.tag)];
    else [chosen removeObject:@(sender.tag)];
    self.viewports = chosen;
    [self syncViewports];
}
- (void)moveViewport:(NSUInteger)from to:(NSUInteger)to {
    NSMutableArray<NSNumber *> *chosen = [NSMutableArray arrayWithArray:self.viewports ?: @[]];
    if (from < chosen.count && to < chosen.count) {
        NSNumber *preset = chosen[from];
        [chosen removeObjectAtIndex:from];
        [chosen insertObject:preset atIndex:to];
        self.viewports = chosen;
    }
    [self syncViewports];
}
- (void)toggleViewportSync:(NSButton *)sender {
    [NSUserDefaults.standardUserDefaults setBool:!sender.state forKey:@"isolateViewports"];
    NSString *address = page().address;
    if (sender.state && address.length)
        for (Tile *tile in surfaceOf(page()).subviews)
            if (![tile.page.address isEqual:address]) [tile follow:address];
    for (NSTextField *hint in sender.superview.subviews)
        if ([hint.identifier isEqual:@"viewportHint"]) hint.stringValue = viewportHint();
    [self syncViewports];
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
    Surface *surface = surfaceOf(page());
    if (!surface || self.quitting) return;
    BOOL useChromium = chromium();
    NSArray<NSNumber *> *wanted = self.viewports ?: @[];
    NSArray<Tile *> *tiles = surface.subviews.copy;
    Tile *kept = nil;
    for (Tile *tile in tiles)
        if (!kept && [wanted containsObject:@(tile.preset)]) kept = tile;
    if (!kept) {
        kept = tiles.firstObject;
        kept.preset = wanted.count ? wanted.firstObject.integerValue : -1;
    }
    for (Tile *tile in tiles)
        if (tile != kept && ![wanted containsObject:@(tile.preset)]) [surface drop:tile];
    if (!wanted.count) kept.preset = -1;
    NSView<BrowPage> *active = page();
    BOOL activeKept = active.superview.superview == surface;
    NSString *address = (activeKept ? active : kept.page).address;
    for (NSNumber *index in wanted) {
        if ([[surface.subviews valueForKey:@"preset"] containsObject:index]) continue;
        NSView<BrowPage> *extra = createPage(useChromium);
        if (!extra) break;
        [surface add:extra];
        Tile *tile = surface.subviews.lastObject;
        tile.preset = index.integerValue;
        if (address.length) [tile follow:address];
    }
    [surface sortSubviewsUsingFunction:presetOrder context:(__bridge void *)wanted];
    BOOL separate = separatePages() && surface.subviews.count > 1;
    surface.bars = separate && ![NSUserDefaults.standardUserDefaults boolForKey:@"toolbarHidden"];
    if (!surface.bars) [self endViewportEditing];
    Tile *selected = separate && activeKept ? (Tile *)active.superview : surface.subviews.firstObject;
    self.pages[@(brow_current())] = selected.page;
    [surface select:separate ? selected : nil];
    NSMutableArray<NSValue *> *sizes = [NSMutableArray array];
    NSMutableArray<NSString *> *titles = [NSMutableArray array];
    for (Tile *tile in surface.subviews) {
        if (tile.preset < 0) continue;
        NSArray *preset = viewportPresets()[tile.preset];
        [sizes addObject:[NSValue valueWithSize:NSMakeSize([preset[1] doubleValue], [preset[2] doubleValue])]];
        [titles addObject:[NSString stringWithFormat:@"%@ · %@ × %@", preset[0], preset[1], preset[2]]];
    }
    surface.viewports = sizes;
    surface.titles = titles;
    [self updateWindowCorners];
    if (self.focusMode) [self layoutFocus];
    [surface arrange];
    [self syncPillNavigation:nil];
}
- (void)editViewportAddress:(Tile *)tile {
    [self dismiss];
    if (!self.barEditor) {
        NSTextField *editor = self.barEditor = [NSTextField new];
        editor.placeholderString = @"Search or enter URL";
        editor.accessibilityLabel = @"Viewport address";
        editor.bezelStyle = NSTextFieldRoundedBezel;
        editor.font = [NSFont systemFontOfSize:12];
        editor.usesSingleLineMode = YES;
        editor.maximumNumberOfLines = 1;
        editor.lineBreakMode = NSLineBreakByClipping;
        editor.cell.wraps = NO;
        editor.cell.scrollable = YES;
        editor.delegate = self;
    }
    self.editedTile = tile;
    NSRect rect = [tile.superview convertRect:NSRectFromCGRect(tile.bar.frame) toView:self.window.contentView];
    self.barEditor.frame = NSInsetRect(rect, 0, -1);
    [self.window.contentView addSubview:self.barEditor positioned:NSWindowAbove relativeTo:nil];
    self.barEditor.stringValue = tile.page.address ?: @"";
    [self.window makeKeyAndOrderFront:nil];
    [self.barEditor selectText:nil];
    NSText *field = self.barEditor.currentEditor;
    [field scrollRangeToVisible:NSMakeRange(field.string.length, 0)];
}
- (void)endViewportEditing {
    self.editedTile = nil;
    [self.barEditor removeFromSuperview];
}
