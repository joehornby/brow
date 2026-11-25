#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

@interface SizeSlider : NSView
@property(nonatomic) double minimum, maximum, value;
@property(nonatomic) BOOL enabled;
@property NSInteger index;
@property(readonly) BOOL dragging;
@property(copy) void (^changed)(double value);
@property(copy) void (^released)(void);
- (instancetype)initWithFrame:(NSRect)frame title:(NSString *)title;
@end

@implementation SizeSlider {
    CALayer *_track, *_fill, *_ticks, *_handle;
    NSTextField *_label, *_number;
    BOOL _hover, _pressed, _moved;
    CGFloat _stretch, _start;
}
- (instancetype)initWithFrame:(NSRect)frame title:(NSString *)title {
    self = [super initWithFrame:frame];
    self.wantsLayer = YES;
    _enabled = YES;
    _maximum = 1;
    _track = [CALayer layer];
    _track.cornerRadius = 10;
    _track.cornerCurve = kCACornerCurveContinuous;
    _track.masksToBounds = YES;
    _fill = [CALayer layer];
    _fill.cornerRadius = 10;
    _fill.cornerCurve = kCACornerCurveContinuous;
    _ticks = [CALayer layer];
    _ticks.opacity = 0;
    for (int i = 1; i < 10; i++) {
        CALayer *tick = [CALayer layer];
        tick.cornerRadius = .5;
        [_ticks addSublayer:tick];
    }
    _handle = [CALayer layer];
    _handle.bounds = CGRectMake(0, 0, 3, 20);
    _handle.cornerRadius = 1.5;
    _handle.opacity = 0;
    [self.layer addSublayer:_track];
    for (CALayer *layer in @[_fill, _ticks, _handle]) [_track addSublayer:layer];
    _label = [NSTextField labelWithString:title];
    _label.font = [NSFont systemFontOfSize:13];
    _label.textColor = NSColor.secondaryLabelColor;
    _number = [NSTextField labelWithString:@""];
    _number.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightMedium];
    _number.alignment = NSTextAlignmentRight;
    [self addSubview:_label];
    [self addSubview:_number];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect owner:self userInfo:nil]];
    return self;
}
- (BOOL)wantsUpdateLayer { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (BOOL)dragging { return _pressed; }
- (NSView *)hitTest:(NSPoint)point { return NSPointInRect(point, self.frame) ? self : nil; }
- (void)updateLayer {
    BOOL active = (_hover || _pressed) && self.enabled;
    NSColor *ink = NSColor.labelColor;
    _track.backgroundColor = [ink colorWithAlphaComponent:active ? .09 : .06].CGColor;
    _fill.backgroundColor = [ink colorWithAlphaComponent:active ? .13 : .10].CGColor;
    _handle.backgroundColor = [ink colorWithAlphaComponent:.5].CGColor;
    for (CALayer *tick in _ticks.sublayers) tick.backgroundColor = [ink colorWithAlphaComponent:.16].CGColor;
}
- (void)layout {
    [super layout];
    CGFloat w = NSWidth(self.bounds), h = NSHeight(self.bounds);
    [_label sizeToFit];
    [_label setFrameOrigin:NSMakePoint(12, round((h - NSHeight(_label.frame)) / 2))];
    _number.frame = NSMakeRect(w - 76, round((h - 16) / 2), 64, 16);
    [self place:0];
}
- (void)place:(int)motion {
    CGFloat w = NSWidth(self.bounds), h = NSHeight(self.bounds), span = self.maximum - self.minimum;
    if (w <= 0) return;
    CGFloat fraction = span > 0 ? (self.value - self.minimum) / span : 0, grow = fabs(_stretch);
    CGFloat left = _stretch < 0 ? grow : 0, fill = fraction * w + MAX(0, _stretch);
    CGFloat x = MIN(w + grow - 8, MAX(8, fill - 9)), seen = x - left;
    CGFloat numberLeft = NSMaxX(_number.frame) - _number.intrinsicContentSize.width;
    BOOL shown = (_hover || _pressed) && self.enabled;
    BOOL dodge = (seen > NSMinX(_label.frame) - 6 && seen < NSMaxX(_label.frame) + 6) || seen > numberLeft - 6;
    [CATransaction begin];
    if (motion) {
        [CATransaction setAnimationDuration:motion == 2 ? .45 : .18];
        [CATransaction setAnimationTimingFunction:motion == 2
            ? [CAMediaTimingFunction functionWithControlPoints:.3 :1.45 :.45 :1]
            : [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut]];
    } else [CATransaction setDisableActions:YES];
    _track.frame = CGRectMake(-left, 0, w + grow, h);
    _fill.frame = CGRectMake(0, 0, fill, h);
    _ticks.frame = CGRectMake(left, 0, w, h);
    for (NSUInteger i = 0; i < _ticks.sublayers.count; i++) {
        CGFloat at = round(w * (i + 1) / 10);
        _ticks.sublayers[i].frame = CGRectMake(at, (h - 8) / 2, 1, 8);
        _ticks.sublayers[i].hidden = at < NSMaxX(_label.frame) + 6 || at > numberLeft - 8;
    }
    _handle.position = CGPointMake(x, h / 2);
    [CATransaction commit];
    [CATransaction begin];
    [CATransaction setAnimationDuration:.18];
    [CATransaction setAnimationTimingFunction:[CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut]];
    _ticks.opacity = shown;
    _handle.opacity = !shown ? 0 : dodge ? .3 : 1;
    _handle.transform = CATransform3DMakeScale(shown ? 1 : .25, dodge ? .55 : 1, 1);
    [CATransaction commit];
}
- (void)show:(double)value motion:(int)motion {
    _value = MIN(self.maximum, MAX(self.minimum, value));
    _number.stringValue = [NSString stringWithFormat:@"%.0f", _value];
    [self place:motion];
}
- (void)setValue:(double)value {
    if (value != _value) [self show:value motion:0];
}
- (void)setMinimum:(double)minimum {
    _minimum = minimum;
    [self show:_value motion:0];
}
- (void)setMaximum:(double)maximum {
    _maximum = maximum;
    [self show:_value motion:0];
}
- (void)setEnabled:(BOOL)enabled {
    _enabled = enabled;
    self.alphaValue = enabled ? 1 : .45;
}
- (void)commit:(double)fraction motion:(int)motion {
    double value = self.minimum + fraction * (self.maximum - self.minimum);
    value = fraction <= 0 ? self.minimum : fraction >= 1 ? self.maximum : round(value / 10) * 10;
    double before = _value;
    [self show:value motion:motion];
    if (_value != before && self.changed) self.changed(_value);
}
- (void)touch {
    self.needsDisplay = YES;
    [self place:1];
}
- (void)mouseEntered:(NSEvent *)event {
    _hover = YES;
    [self touch];
}
- (void)mouseExited:(NSEvent *)event {
    _hover = NO;
    [self touch];
}
- (void)mouseDown:(NSEvent *)event {
    if (!self.enabled) return;
    _pressed = YES;
    _moved = NO;
    _start = [self convertPoint:event.locationInWindow fromView:nil].x;
    [self touch];
}
- (void)mouseDragged:(NSEvent *)event {
    if (!_pressed) return;
    CGFloat w = NSWidth(self.bounds), x = [self convertPoint:event.locationInWindow fromView:nil].x;
    if (!_moved && fabs(x - _start) < 3) return;
    _moved = YES;
    CGFloat over = x < 0 ? x : x > w ? x - w : 0, past = MAX(0, fabs(over) - 24);
    _stretch = copysign(10 * (1 - exp(-past / 50)), over);
    [self commit:x / w motion:0];
}
- (void)mouseUp:(NSEvent *)event {
    if (!_pressed) return;
    _pressed = NO;
    self.needsDisplay = YES;
    if (!_moved) {
        double fraction = MIN(1, MAX(0, _start / NSWidth(self.bounds))), nearest = round(fraction * 10) / 10;
        [self commit:fabs(fraction - nearest) <= .035 ? nearest : fraction motion:2];
    }
    BOOL stretched = _stretch != 0;
    _stretch = 0;
    [self place:stretched || !_moved ? 2 : 1];
    if (self.released) self.released();
}
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilitySliderRole; }
- (NSString *)accessibilityLabel { return _label.stringValue; }
- (id)accessibilityValue { return @(self.value); }
- (BOOL)step:(double)amount {
    double before = _value;
    [self show:_value + amount motion:1];
    if (_value != before && self.changed) self.changed(_value);
    return YES;
}
- (BOOL)accessibilityPerformIncrement { return [self step:10]; }
- (BOOL)accessibilityPerformDecrement { return [self step:-10]; }
@end
