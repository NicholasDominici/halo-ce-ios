#import <UIKit/UIKit.h>
#include "../rtc_transport.h"
#include "../posix_bridge.h"
#include "posix.h"
#include <arpa/inet.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The same posix_socket_* entry points that Halo calls through its guest ABI. */
static struct sockaddr_in game_address(uint32_t ip, uint16_t port) {
    struct sockaddr_in address = {0};
    uint16_t family = AF_INET;
    memcpy(&address, &family, 2);
    address.sin_addr.s_addr = htonl(ip);
    address.sin_port = htons(port);
    return address;
}
static int game_bind(int fd, uint16_t port) {
    struct sockaddr_in a = game_address(0, port);
    return posix_socket_bind(fd, &a, sizeof(a));
}
static int game_connect(int fd, uint32_t ip, uint16_t port) {
    struct sockaddr_in a = game_address(ip, port);
    return posix_socket_connect(fd, &a, sizeof(a));
}
@interface WebProbe : UIViewController {
    struct hw_rtc *_rtc;
    _Atomic int _stop;
}
@property(nonatomic, strong) UILabel *status;
@property(nonatomic, strong) NSString *base;
@property(nonatomic, strong) dispatch_queue_t queue;
@property(nonatomic, strong) dispatch_group_t jobs;
@property(nonatomic, strong) NSURLSession *session;
- (void)send:(NSDictionary *)event;
- (void)poll;
- (void)echo;
- (void)stop;
@end
static void signal_event(void *context, const char *event, const char *value, const char *detail) {
    WebProbe *probe = (__bridge WebProbe *)context;
    NSDictionary *data = @{@"event" : @(event), @"value" : @(value), @"detail" : @(detail)};
    [probe send:data];
    if (!strcmp(event, "ready") || !strcmp(event, "error"))
        dispatch_async(dispatch_get_main_queue(), ^{
          probe.status.text = [NSString stringWithFormat:@"Native iOS Winsock → WebRTC: %@ %@",
                                                         data[@"event"], data[@"value"]];
        });
}
@implementation WebProbe
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:.02 green:.06 blue:.08 alpha:1];
    self.status = [[UILabel alloc] initWithFrame:self.view.bounds];
    self.status.autoresizingMask =
        UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.status.textColor = UIColor.whiteColor;
    self.status.textAlignment = NSTextAlignmentCenter;
    self.status.numberOfLines = 0;
    self.status.font = [UIFont systemFontOfSize:24 weight:UIFontWeightMedium];
    self.status.text = @"Native iOS WebRTC transport probe\nConnecting to local fixture…";
    [self.view addSubview:self.status];
    UIButton *stop = [UIButton buttonWithType:UIButtonTypeSystem];
    stop.frame = CGRectMake(40, 40, 120, 44);
    [stop setTitle:@"Stop probe" forState:UIControlStateNormal];
    [stop addTarget:self action:@selector(stop) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:stop];
    const char *url = getenv("HALO_WEB_SIGNAL_URL");
    self.base = url ? @(url) : @"http://127.0.0.1:9032";
    NSURL *base = [NSURL URLWithString:self.base];
    if (![base.scheme isEqualToString:@"http"] || ![base.host isEqualToString:@"127.0.0.1"] ||
        base.user || base.password || base.query || base.fragment ||
        (base.path.length && ![base.path isEqualToString:@"/"])) {
        self.status.text = @"This probe accepts a loopback HTTP fixture only.";
        return;
    }
    if ([self.base hasSuffix:@"/"])
        self.base = [self.base substringToIndex:self.base.length - 1];
    self.queue = dispatch_queue_create("halo.web.transport.probe", DISPATCH_QUEUE_SERIAL);
    self.jobs = dispatch_group_create();
    self.session = [NSURLSession
        sessionWithConfiguration:NSURLSessionConfiguration.ephemeralSessionConfiguration];
    atomic_store(&_stop, 0);
    dispatch_async(self.queue, ^{
      [self poll];
    });
}
- (BOOL)prefersStatusBarHidden {
    return YES;
}
- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}
- (void)stop {
    if (!self.queue)
        return;
    dispatch_async(self.queue, ^{
      if (atomic_exchange(&self->_stop, 1))
          return;
      [self.session invalidateAndCancel];
      dispatch_group_notify(self.jobs, self.queue, ^{
        int result = halo_web_install_transport(NULL);
        if (!result) {
            hw_rtc_destroy(self->_rtc);
            self->_rtc = NULL;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
          self.status.text =
              result ? @"Probe cleanup failed: live virtual sockets remain."
                     : @"Probe stopped. All virtual sockets and WebRTC channels closed.";
        });
      });
    });
}
- (void)send:(NSDictionary *)event {
    if (atomic_load(&_stop))
        return;
    NSString *name = event[@"event"];
    if (![@[ @"description", @"candidate" ] containsObject:name])
        NSLog(@"halo-web-probe: %@", event);
    NSMutableURLRequest *request = [NSMutableURLRequest
        requestWithURL:[NSURL URLWithString:[self.base
                                                stringByAppendingString:@"/command?side=native"]]];
    request.HTTPMethod = @"POST";
    request.timeoutInterval = 5;
    [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
    request.HTTPBody = [NSJSONSerialization dataWithJSONObject:event options:0 error:nil];
    [[self.session dataTaskWithRequest:request
                     completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                       (void)data;
                       if (error || [(NSHTTPURLResponse *)response statusCode] != 200)
                           NSLog(@"halo-web-probe: signaling delivery failed");
                     }] resume];
}
- (void)poll {
    if (atomic_load(&_stop))
        return;
    NSURL *url = [NSURL URLWithString:[self.base stringByAppendingString:@"/signal?side=native"]];
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    request.timeoutInterval = 5;
    [[self.session
        dataTaskWithRequest:request
          completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            NSDictionary *body =
                data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
            dispatch_async(self.queue, ^{
              if (atomic_load(&self->_stop))
                  return;
              if (error || [(NSHTTPURLResponse *)response statusCode] != 200 ||
                  ![body isKindOfClass:NSDictionary.class] ||
                  ![body[@"protocol"] isEqual:@"halo-web-local-fixture-v1"] ||
                  ![body[@"externalNative"] boolValue]) {
                  dispatch_async(dispatch_get_main_queue(), ^{
                    self.status.text = @"Start the loopback fixture with --external-native.";
                  });
                  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), self.queue, ^{
                    [self poll];
                  });
                  return;
              }
              if (!self->_rtc) {
                  unsigned char local[6] = {2, 0, 0, 0, 0, 1}, remote[6] = {2, 0, 0, 0, 0, 2};
                  self->_rtc = hw_rtc_create(local, remote, [body[@"nativeOffer"] boolValue],
                                             signal_event, (__bridge void *)self);
                  if (!self->_rtc || halo_web_install_transport(hw_rtc_net(self->_rtc))) {
                      [self send:@{
                          @"event" : @"error",
                          @"value" : @"Could not install native transport"
                      }];
                      [self stop];
                      return;
                  }
                  dispatch_group_async(self.jobs,
                                       dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
                                         [self echo];
                                       });
              }
              if (![body[@"commands"] isKindOfClass:NSArray.class]) {
                  [self stop];
                  return;
              }
              for (NSDictionary *command in body[@"commands"]) {
                  if (![command isKindOfClass:NSDictionary.class])
                      continue;
                  NSString *type = command[@"type"];
                  int result = -1;
                  if ([type isEqual:@"description"] &&
                      [command[@"sdp"] isKindOfClass:NSString.class] &&
                      [command[@"descriptionType"] isKindOfClass:NSString.class])
                      result = hw_rtc_description(self->_rtc, [command[@"sdp"] UTF8String],
                                                  [command[@"descriptionType"] UTF8String]);
                  else if ([type isEqual:@"candidate"] &&
                           [command[@"candidate"] isKindOfClass:NSString.class] &&
                           [command[@"mid"] isKindOfClass:NSString.class])
                      result = hw_rtc_candidate(self->_rtc, [command[@"candidate"] UTF8String],
                                                [command[@"mid"] UTF8String]);
                  else if ([type isEqual:@"native-send"] &&
                           [command[@"payload"] isKindOfClass:NSString.class]) {
                      NSData *payload =
                          [command[@"payload"] dataUsingEncoding:NSUTF8StringEncoding];
                      result = 0;
                      dispatch_group_async(
                          self.jobs, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
                            int socket = posix_socket(AF_INET, SOCK_STREAM, 0), count = -1;
                            char bytes[4096];
                            if (payload.length <= sizeof(bytes) &&
                                !game_connect(socket, hw_rtc_peer(self->_rtc), 5150) &&
                                posix_socket_send(socket, payload.bytes, (int)payload.length, 0) ==
                                    (int)payload.length) {
                                for (int attempt = 0;
                                     count < 0 && attempt < 5000 && !atomic_load(&self->_stop);
                                     attempt++) {
                                    count = posix_socket_recv(socket, bytes, sizeof(bytes), 0);
                                    if (count < 0)
                                        usleep(1000);
                                }
                            }
                            NSString *reply =
                                count > 0 ? [[NSString alloc] initWithBytes:bytes
                                                                     length:(NSUInteger)count
                                                                   encoding:NSUTF8StringEncoding]
                                          : @"";
                            [self send:@{
                                @"event" : @"native-reply",
                                @"payload" : reply ?: @"",
                                @"count" : @(count)
                            }];
                            posix_socket_close(socket);
                          });
                  } else if ([type isEqual:@"quit"]) {
                      [self stop];
                      result = 0;
                  }
                  if (result < 0)
                      [self send:@{
                          @"event" : @"command-error",
                          @"type" : [type isKindOfClass:NSString.class] ? type : @"unknown"
                      }];
              }
              dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC), self.queue, ^{
                [self poll];
              });
            });
          }] resume];
}
- (void)echo {
    int server = posix_socket(AF_INET, SOCK_STREAM, 0), udp = posix_socket(AF_INET, SOCK_DGRAM, 0),
        stream = -1;
    if (server < 0 || udp < 0 || game_bind(server, 5150) || posix_socket_listen(server, 8) ||
        game_bind(udp, 5151)) {
        [self send:@{@"event" : @"error", @"value" : @"Could not bind probe sockets"}];
        if (server >= 0)
            posix_socket_close(server);
        if (udp >= 0)
            posix_socket_close(udp);
        return;
    }
    while (!atomic_load(&_stop)) {
        if (hw_net_peer_ready(hw_rtc_net(_rtc), hw_rtc_peer(_rtc))) {
            if (stream < 0)
                stream = posix_socket_accept(server, NULL, NULL);
            unsigned char bytes[16384];
            struct sockaddr_in from;
            int length = sizeof(from);
            int count = posix_socket_recvfrom(udp, bytes, sizeof(bytes), 0, &from, &length);
            if (count >= 0) {
                posix_socket_sendto(udp, bytes, count, 0, &from, length);
                [self send:@{@"event" : @"datagram", @"bytes" : @(count)}];
            }
            if (stream >= 0) {
                count = posix_socket_recv(stream, bytes, sizeof(bytes), 0);
                if (count > 0) {
                    int sent = 0;
                    for (int tries = 0; sent < count && tries < 5000 && !atomic_load(&_stop);
                         tries++) {
                        int result = posix_socket_send(stream, bytes + sent, count - sent, 0);
                        if (result > 0)
                            sent += result;
                        else
                            usleep(1000);
                    }
                    [self send:@{@"event" : @"stream", @"bytes" : @(count), @"sent" : @(sent)}];
                } else if (count == 0) {
                    posix_socket_close(stream);
                    stream = -1;
                    [self send:@{@"event" : @"eof"}];
                }
            }
        }
        usleep(1000);
    }
    posix_socket_close(server);
    posix_socket_close(udp);
    if (stream >= 0)
        posix_socket_close(stream);
}
@end
@interface ProbeDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation ProbeDelegate
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application;
    (void)options;
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[WebProbe alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
- (void)applicationWillTerminate:(UIApplication *)application {
    (void)application;
    [(WebProbe *)self.window.rootViewController stop];
}
@end
int main(int argc, char **argv) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(ProbeDelegate.class));
    }
}
