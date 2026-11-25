#import "Page.h"
#import "include/capi/cef_app_capi.h"
#import "include/cef_api_hash.h"
#import <crt_externs.h>

#define DROP(obj) (obj)->base.release(&(obj)->base)
static bool running;
static void (*openLink)(NSString *);
static int browsers;
static void (^onClosed)(void);
static void afterEvent(void (^work)(void)) {
    CFRunLoopPerformBlock(CFRunLoopGetMain(),
                          (__bridge CFArrayRef) @[ NSDefaultRunLoopMode, NSModalPanelRunLoopMode ], work);
    CFRunLoopWakeUp(CFRunLoopGetMain());
}
static cef_app_t application;
static cef_client_t client;
static cef_life_span_handler_t life;
static cef_browser_process_handler_t process;

static void hold(cef_base_ref_counted_t *s) {}
static int keep(cef_base_ref_counted_t *s) { return 0; }
static int alive(cef_base_ref_counted_t *s) { return 1; }
static cef_base_ref_counted_t permanent(size_t size) {
    return (cef_base_ref_counted_t){size, hold, keep, keep, alive};
}
static cef_browser_process_handler_t *get_process(cef_app_t *s) { return &process; }
static cef_life_span_handler_t *get_life(cef_client_t *s) { return &life; }
static cef_display_handler_t display;
static cef_load_handler_t load;
static cef_display_handler_t *get_display(cef_client_t *s) { return &display; }
static cef_load_handler_t *get_load(cef_client_t *s) { return &load; }
static void notifyNavigation(void) {
    afterEvent(^{ [NSNotificationCenter.defaultCenter postNotificationName:BrowNavigationChanged object:nil]; });
}
static void addressChanged(cef_display_handler_t *s, cef_browser_t *browser, cef_frame_t *frame,
                           const cef_string_t *url) {
    if (frame->is_main(frame)) notifyNavigation();
    DROP(frame);
    DROP(browser);
}
static void loadingChanged(cef_load_handler_t *s, cef_browser_t *browser, int loading, int back, int forward) {
    notifyNavigation();
    DROP(browser);
}
static int popup(cef_life_span_handler_t *s, cef_browser_t *browser, cef_frame_t *frame, int popupID,
                 const cef_string_t *url, const cef_string_t *name, cef_window_open_disposition_t disposition,
                 int gesture, const cef_popup_features_t *features, cef_window_info_t *window,
                 cef_client_t **clientOut, cef_browser_settings_t *settings, cef_dictionary_value_t **extra,
                 int *noJS) {
    if (gesture && url && url->length) {
        NSString *address = [[NSString alloc] initWithCharacters:(const unichar *)url->str
                                                          length:url->length];
        afterEvent(^{
          openLink(address);
        });
    }
    DROP(browser);
    DROP(frame);
    return 1;
}
static void created(cef_life_span_handler_t *s, cef_browser_t *b) {
    browsers++;
    DROP(b);
}
static void closed(cef_life_span_handler_t *s, cef_browser_t *b) {
    browsers--;
    DROP(b);
    if (browsers == 0 && onClosed) {
        afterEvent(onClosed);
        onClosed = nil;
    }
}

static NSTimer *pumpTimer;
static void schedule(cef_browser_process_handler_t *s, int64_t ms) {
    CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, ^{
      if (!running)
          return;
      NSDate *next = [NSDate dateWithTimeIntervalSinceNow:MAX(0, MIN(ms, 40)) / 1000.0];
      if (!pumpTimer || [next compare:pumpTimer.fireDate] == NSOrderedAscending) {
          [pumpTimer invalidate];
          pumpTimer = [[NSTimer alloc] initWithFireDate:next
                                               interval:0
                                                repeats:NO
                                                  block:^(NSTimer *timer) {
                                                    pumpTimer = nil;
                                                    cef_do_message_loop_work();
                                                    if (running && browsers > 0)
                                                        schedule(NULL, 40);
                                                  }];
          [NSRunLoop.mainRunLoop addTimer:pumpTimer forMode:NSRunLoopCommonModes];
      }
    });
    CFRunLoopWakeUp(CFRunLoopGetMain());
}
static cef_string_t string(NSString *value) {
    cef_string_t out = {0};
    const char *utf8 = value.UTF8String;
    cef_string_utf8_to_utf16(utf8, strlen(utf8), &out);
    return out;
}
static NSString *take_string(cef_string_userfree_t s) {
    if (!s)
        return @"";
    NSString *out = [[NSString alloc] initWithCharacters:(const unichar *)s->str length:s->length];
    cef_string_userfree_free(s);
    return out;
}
static void commandLine(cef_app_t *s, const cef_string_t *type, cef_command_line_t *line) {
    if (!type || !type->length) {
        cef_string_t name = string(@"autoplay-policy"), value = string(@"no-user-gesture-required");
        line->append_switch_with_value(line, &name, &value);
        cef_string_clear(&name);
        cef_string_clear(&value);
    }
    DROP(line);
}
static bool start(void) {
    if (running)
        return true;
    if (strcmp(cef_api_hash(CEF_API_VERSION, 0), CEF_API_HASH_PLATFORM) != 0)
        return false;
    process.base = permanent(sizeof(process));
    process.on_schedule_message_pump_work = schedule;
    application.base = permanent(sizeof(application));
    application.get_browser_process_handler = get_process;
    application.on_before_command_line_processing = commandLine;
    life.base = permanent(sizeof(life));
    life.on_before_popup = popup;
    life.on_after_created = created;
    life.on_before_close = closed;
    client.base = permanent(sizeof(client));
    display.base = permanent(sizeof(display));
    display.on_address_change = addressChanged;
    load.base = permanent(sizeof(load));
    load.on_loading_state_change = loadingChanged;
    client.get_display_handler = get_display;
    client.get_load_handler = get_load;
    client.get_life_span_handler = get_life;
    cef_main_args_t args = {*_NSGetArgc(), *_NSGetArgv()};
    cef_settings_t settings = {0};
    settings.size = sizeof(settings);
    settings.external_message_pump = 1;
    NSString *support = [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory
                                                             inDomains:NSUserDomainMask]
                            .firstObject.path;
    NSString *profile =
        [support stringByAppendingPathComponent:[NSBundle.mainBundle.bundleIdentifier
                                                    stringByAppendingPathComponent:@"Chromium"]];
    settings.root_cache_path = string(profile);
    settings.cache_path = string([profile stringByAppendingPathComponent:@"Default"]);
    running = cef_initialize(&args, &settings, &application, NULL);
    cef_string_clear(&settings.root_cache_path);
    cef_string_clear(&settings.cache_path);
    return running;
}

@interface ChromiumPage : NSView <BrowPage>
@property cef_browser_t *browser;
@property cef_registration_t *captureRegistration;
@property int captureID;
@property(copy) void (^captureReply)(NSImage *);
@property NSSize viewport;
@property CGFloat viewportScale;
@end
static cef_dev_tools_message_observer_t captureObserver;
static NSMapTable<NSNumber *, ChromiumPage *> *capturingPages;
static void captureResult(cef_dev_tools_message_observer_t *observer, cef_browser_t *browser,
                          int messageID, int success, const void *result, size_t size) {
    ChromiumPage *page = [capturingPages objectForKey:@(browser->get_identifier(browser))];
    DROP(browser);
    if (!page.captureReply || page.captureID != messageID) return;
    NSImage *image = nil;
    if (success && size < 2*1024*1024) {
        NSDictionary *json = [NSJSONSerialization JSONObjectWithData:[NSData dataWithBytes:result length:size]
                                                             options:0 error:NULL];
        if ([json[@"data"] isKindOfClass:NSString.class]) {
            NSData *pixels = [[NSData alloc] initWithBase64EncodedString:json[@"data"] options:0];
            image = [[NSImage alloc] initWithData:pixels];
        }
    }
    void (^reply)(NSImage *) = page.captureReply;
    page.captureReply = nil;
    reply(image);
}

@implementation ChromiumPage
- (BOOL)loading { return self.browser && self.browser->is_loading(self.browser); }
- (BOOL)emulateViewport:(NSSize)size {
    CGFloat scale = size.width > 0 ? NSWidth(self.bounds) / size.width : 0;
    if (!self.browser || (NSEqualSizes(size, self.viewport) && scale == self.viewportScale)) return YES;
    self.viewport = size;
    self.viewportScale = scale;
    NSDictionary *command = scale
        ? @{@"id":@0, @"method":@"Emulation.setDeviceMetricsOverride",
            @"params":@{@"width":@(size.width), @"height":@(size.height), @"deviceScaleFactor":@0,
                        @"mobile":@NO, @"scale":@(scale)}}
        : @{@"id":@0, @"method":@"Emulation.clearDeviceMetricsOverride"};
    NSData *data = [NSJSONSerialization dataWithJSONObject:command options:0 error:NULL];
    cef_browser_host_t *host = self.browser->get_host(self.browser);
    host->send_dev_tools_message(host, data.bytes, data.length);
    DROP(host);
    return YES;
}
- (void)samplePage:(void (^)(NSImage *))completion {
    if (!self.browser || self.captureReply || NSIsEmptyRect(self.bounds)) { completion(nil); return; }
    if (!capturingPages) {
        capturingPages = [NSMapTable strongToWeakObjectsMapTable];
        captureObserver.base = permanent(sizeof(captureObserver));
        captureObserver.on_dev_tools_method_result = captureResult;
    }
    cef_browser_host_t *host = self.browser->get_host(self.browser);
    if (!self.captureRegistration) {
        self.captureRegistration = host->add_dev_tools_message_observer(host,&captureObserver);
        [capturingPages setObject:self forKey:@(self.browser->get_identifier(self.browser))];
    }
    self.captureID++;
    NSDictionary *command=@{@"id":@(self.captureID),@"method":@"Page.captureScreenshot",
                            @"params":@{@"format":@"jpeg",@"quality":@30,@"captureBeyondViewport":@NO}};
    NSData *data=[NSJSONSerialization dataWithJSONObject:command options:0 error:NULL];
    self.captureReply=completion;
    BOOL sent=host->send_dev_tools_message(host,data.bytes,data.length);
    DROP(host);
    if (!sent) { self.captureReply=nil; completion(nil); }
}

- (NSString *)address {
    cef_frame_t *f = self.browser->get_main_frame(self.browser);
    if (!f)
        return @"";
    NSString *url = take_string(f->get_url(f));
    DROP(f);
    return [url isEqual:@"about:blank"] ? @"" : url;
}
- (NSString *)pageTitle {
    cef_browser_host_t *h = self.browser->get_host(self.browser);
    cef_navigation_entry_t *e = h->get_visible_navigation_entry(h);
    DROP(h);
    if (!e)
        return @"";
    NSString *title = take_string(e->get_title(e));
    DROP(e);
    return title;
}
- (void)navigate:(NSString *)url {
    cef_frame_t *frame = self.browser->get_main_frame(self.browser);
    if (!frame)
        return;
    cef_string_t address = string(url);
    frame->load_url(frame, &address);
    cef_string_clear(&address);
    DROP(frame);
}
- (BOOL)canGoBack { return self.browser && self.browser->can_go_back(self.browser); }
- (BOOL)canGoForward { return self.browser && self.browser->can_go_forward(self.browser); }
- (void)goBack {
    self.browser->go_back(self.browser);
}
- (void)goForward {
    self.browser->go_forward(self.browser);
}
- (void)refresh {
    self.browser->reload(self.browser);
}
- (void)focus {
    cef_browser_host_t *h = self.browser->get_host(self.browser);
    h->set_focus(h, 1);
    DROP(h);
}
- (void)dispose {
    if (!self.browser)
        return;
    cef_browser_host_t *h = self.browser->get_host(self.browser);
    for (NSView *child in self.subviews.copy)
        [child removeFromSuperview];
    [capturingPages removeObjectForKey:@(self.browser->get_identifier(self.browser))];
    if (self.captureRegistration) { DROP(self.captureRegistration); self.captureRegistration=NULL; }
    self.captureReply=nil;
    h->close_browser(h, 1);
    DROP(h);
    DROP(self.browser);
    self.browser = NULL;
}
@end

NSView<BrowPage> *brow_chromium_page(NSView *deck, void (*open)(NSString *)) {
    openLink = open;
    if (!start())
        return nil;
    ChromiumPage *page = [[ChromiumPage alloc] initWithFrame:deck.bounds];
    [deck addSubview:page];
    cef_window_info_t info = {0};
    info.size = sizeof(info);
    info.parent_view = (__bridge void *)page;
    info.bounds = (cef_rect_t){0, 0, (int)NSWidth(page.bounds), (int)NSHeight(page.bounds)};
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    cef_browser_settings_t settings = {0};
    settings.size = sizeof(settings);
    page.browser = cef_browser_host_create_browser_sync(&info, &client, NULL, &settings, NULL, NULL);
    if (!page.browser) {
        [page removeFromSuperview];
        return nil;
    }
    for (NSView *v in page.subviews)
        v.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    return page;
}
void brow_chromium_shutdown(void (^done)(void)) {
    void (^finish)(void) = ^{
      if (running) {
          running = false;
          [pumpTimer invalidate];
          pumpTimer = nil;
          cef_shutdown();
      }
      done();
    };
    if (browsers)
        onClosed = [finish copy];
    else
        afterEvent(finish);
}
