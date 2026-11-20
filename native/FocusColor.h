// OKLCH uses a polar form of Oklab. Conversion matrices assume D65 linear sRGB.
typedef struct {
    double L, C, h;
} FocusLCH;
typedef struct {
    double r, g, b;
} FocusRGB;

static double linearChannel(double c) { return c <= .04045 ? c / 12.92 : pow((c + .055) / 1.055, 2.4); }
static double encodedChannel(double c) {
    c = fmin(1, fmax(0, c));
    return c <= .0031308 ? 12.92 * c : 1.055 * pow(c, 1 / 2.4) - .055;
}
static FocusLCH focusLCH(double red, double green, double blue) {
    double r = linearChannel(red), g = linearChannel(green), b = linearChannel(blue);
    double l = cbrt(.4122214708 * r + .5363325363 * g + .0514459929 * b);
    double m = cbrt(.2119034982 * r + .6806995451 * g + .1073969566 * b);
    double s = cbrt(.0883024619 * r + .2817188376 * g + .6299787005 * b);
    double a = 1.9779984951 * l - 2.4285922050 * m + .4505937099 * s;
    double y = .0259040371 * l + .7827717662 * m - .8086757660 * s;
    return (FocusLCH){.2104542553 * l + .793617785 * m - .0040720468 * s, hypot(a, y), atan2(y, a)};
}
static FocusRGB focusLinearRGB(FocusLCH color) {
    double a = color.C * cos(color.h), b = color.C * sin(color.h);
    double l = color.L + .3963377774 * a + .2158037573 * b;
    double m = color.L - .1055613458 * a - .0638541728 * b;
    double s = color.L - .0894841775 * a - 1.291485548 * b;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    return (FocusRGB){4.0767416621 * l - 3.3077115913 * m + .2309699292 * s,
                      -1.2684380046 * l + 2.6097574011 * m - .3413193965 * s,
                      -.0041960863 * l - .7034186147 * m + 1.707614701 * s};
}
static NSColor *focusColor(FocusLCH color) {
    // Reduce only chroma for out-of-gamut stops, preserving hue and lightness.
    double low = 0, high = color.C;
    FocusRGB rgb = focusLinearRGB(color);
    for (int i = 0; i < 12; i++) {
        BOOL fits = rgb.r >= 0 && rgb.r <= 1 && rgb.g >= 0 && rgb.g <= 1 && rgb.b >= 0 && rgb.b <= 1;
        if (i == 0 && fits)
            break;
        if (fits)
            low = color.C;
        else
            high = color.C;
        color.C = (low + high) / 2;
        rgb = focusLinearRGB(color);
    }
    return [NSColor colorWithSRGBRed:encodedChannel(rgb.r)
                               green:encodedChannel(rgb.g)
                                blue:encodedChannel(rgb.b)
                               alpha:1];
}
static FocusLCH mixLCH(FocusLCH a, FocusLCH b, double t) {
    if (a.C < .006)
        a.h = b.h;
    if (b.C < .006)
        b.h = a.h;
    return (FocusLCH){a.L + (b.L - a.L) * t, a.C + (b.C - a.C) * t, a.h + remainder(b.h - a.h, 2 * M_PI) * t};
}
enum { FocusStopCount = 9 };
static NSArray *focusStops(FocusLCH a, FocusLCH b) {
    NSMutableArray *stops = [NSMutableArray arrayWithCapacity:FocusStopCount];
    for (int i = 0; i < FocusStopCount; i++)
        [stops addObject:(id)focusColor(mixLCH(a, b, (double)i / (FocusStopCount - 1))).CGColor];
    return stops;
}

static FocusLCH focusLCHFromStop(id stop) {
    NSColor *c = [[NSColor colorWithCGColor:(__bridge CGColorRef)stop]
        colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    return focusLCH(c.redComponent, c.greenComponent, c.blueComponent);
}
