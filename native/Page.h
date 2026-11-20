#import <AppKit/AppKit.h>

@protocol BrowPage
- (NSString *)address;
- (NSString *)pageTitle;
- (void)navigate:(NSString *)address;
- (void)goBack;
- (void)goForward;
- (void)refresh;
// A small image of the visible page for the focus-mode background.
- (void)samplePage:(void (^)(NSImage *image))completion;
- (BOOL)loading;
- (void)focus;
- (void)dispose;
@end
