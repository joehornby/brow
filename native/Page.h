#import <AppKit/AppKit.h>

#define BrowNavigationChanged @"BrowNavigationChanged"

@protocol BrowPage
- (NSString *)address;
- (NSString *)pageTitle;
- (void)navigate:(NSString *)address;
- (BOOL)canGoBack;
- (BOOL)canGoForward;
- (void)goBack;
- (void)goForward;
- (void)refresh;
// A small image of the visible page for the focus-mode background.
- (void)samplePage:(void (^)(NSImage *image))completion;
- (BOOL)loading;
- (void)focus;
- (void)dispose;
@end
