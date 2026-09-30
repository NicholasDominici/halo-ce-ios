#import <UIKit/UIKit.h>
#import "host_orientation.h"
#include "ios_host.h"
#include "../matchmaking.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <TargetConditionals.h>
#include <CommonCrypto/CommonDigest.h>

static _Atomic int next_command;
static char command_invite[256];
static int current_state, current_humans, current_bots;
static NSString *current_detail=@"";
static __weak UIWindow *game_window;
#if TARGET_OS_SIMULATOR
static void simulator_automation(void);
#endif

int host_matchmaking_command(char *invite,unsigned int size) {
#if TARGET_OS_SIMULATOR
    simulator_automation();
#endif
    if(invite && size)snprintf(invite,size,"%s",command_invite);
    return atomic_exchange(&next_command,0);
}
static void issue_command(int command,NSString *invite) {
    snprintf(command_invite,sizeof(command_invite),"%s",invite.UTF8String ?: "");
    atomic_store(&next_command,command);
}

@interface HaloMatchmakingController : HaloLandscapeController
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) UITextField *service;
@property(nonatomic,strong) UIButton *find;
@property(nonatomic,strong) UIButton *practice;
@property(nonatomic,strong) NSTimer *timer;
@property(nonatomic,copy) NSString *ticket;
@property(nonatomic,copy) NSString *token;
@property(nonatomic,copy) NSString *role;
@property(nonatomic,copy) NSString *base;
@property(nonatomic,copy) NSString *lastInvite;
@property(nonatomic) BOOL inFlight;
@property(nonatomic) BOOL commandIssued;
@property(nonatomic) BOOL startIssued;
@property(nonatomic) BOOL searching;
@property(nonatomic) NSUInteger generation;
@property(nonatomic,strong) NSDate *deadline;
@end

static HaloMatchmakingController *match_controller;

static NSString *map_compatibility(void) {
    FILE *file=fopen("maps/bloodgulch.map","rb");
    if(!file)return nil;
    CC_SHA256_CTX hash;CC_SHA256_Init(&hash);
    unsigned char bytes[65536],digest[CC_SHA256_DIGEST_LENGTH];size_t count;
    while((count=fread(bytes,1,sizeof(bytes),file)))CC_SHA256_Update(&hash,bytes,(CC_LONG)count);
    BOOL failed=ferror(file);fclose(file);if(failed)return nil;
    CC_SHA256_Final(digest,&hash);
    NSMutableString *result=[NSMutableString stringWithString:@"halo-native-mm-v1:"];
    for(size_t i=0;i<sizeof(digest);i++)[result appendFormat:@"%02x",digest[i]];
    return result;
}
void host_matchmaking_status(int state,int humans,int bots,const char *detail) {
    if(current_state!=state || current_humans!=humans || current_bots!=bots)
        host_logf(HOST_LOG_INFO,"matchmaking state=%d humans=%d bots=%d",state,humans,bots);
    current_state=state;current_humans=humans;current_bots=bots;
    current_detail=[NSString stringWithUTF8String:detail ?: ""] ?: @"";
#if TARGET_OS_SIMULATOR
    static CFAbsoluteTime last_telemetry;
    if(getenv("HALO_MATCHMAKING_AUTOSTART") && state==HALO_MATCH_PLAYING && CFAbsoluteTimeGetCurrent()-last_telemetry>10) {
        last_telemetry=CFAbsoluteTimeGetCurrent();
        host_logf(HOST_LOG_INFO,"matchmaking integration: %s",detail ?: "");
    }
#endif
}

@implementation HaloMatchmakingController
- (UIButton *)button:(NSString *)title action:(SEL)action {
    UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    button.titleLabel.font=[UIFont systemFontOfSize:17 weight:UIFontWeightSemibold];
    button.backgroundColor=[UIColor colorWithRed:.08 green:.3 blue:.47 alpha:1];
    [button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    button.layer.cornerRadius=12;button.contentEdgeInsets=UIEdgeInsetsMake(14,24,14,24);
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];return button;
}
- (void)viewDidLoad {
    [super viewDidLoad];self.view.backgroundColor=[UIColor colorWithRed:.025 green:.06 blue:.08 alpha:1];
    UILabel *title=[[UILabel alloc]init];title.text=@"Multiplayer demo";title.textColor=UIColor.whiteColor;title.font=[UIFont systemFontOfSize:27 weight:UIFontWeightBold];
    UILabel *description=[[UILabel alloc]init];description.text=@"Blood Gulch · Slayer · 8 players\nEmpty seats fill with BOT players. Practice works offline.";description.numberOfLines=0;description.textColor=UIColor.lightGrayColor;
    self.status=[[UILabel alloc]init];self.status.numberOfLines=0;self.status.textColor=UIColor.whiteColor;self.status.text=@"Return to Halo’s main menu before starting a match.";
    self.service=[[UITextField alloc]init];self.service.placeholder=@"Matchmaking service URL (HTTPS)";self.service.text=[[NSUserDefaults standardUserDefaults]stringForKey:@"HaloMatchmakingService"] ?: @"";
    self.service.textColor=UIColor.whiteColor;self.service.backgroundColor=[UIColor colorWithWhite:1 alpha:.08];self.service.borderStyle=UITextBorderStyleRoundedRect;self.service.keyboardType=UIKeyboardTypeURL;self.service.autocorrectionType=UITextAutocorrectionTypeNo;self.service.autocapitalizationType=UITextAutocapitalizationTypeNone;
    self.find=[self button:@"Find match" action:@selector(findMatch)];self.practice=[self button:@"Practice with bots" action:@selector(practiceMatch)];
    UIButton *cancel=[self button:@"Cancel search / leave match" action:@selector(cancelMatch)];
    UIButton *close=[self button:@"Back to Halo" action:@selector(close)];
    UIStackView *buttons=[[UIStackView alloc]initWithArrangedSubviews:@[self.find,self.practice,close]];buttons.axis=UILayoutConstraintAxisHorizontal;buttons.spacing=12;buttons.distribution=UIStackViewDistributionFillEqually;
    UIStackView *stack=[[UIStackView alloc]initWithArrangedSubviews:@[title,description,self.service,self.status,buttons,cancel]];stack.axis=UILayoutConstraintAxisVertical;stack.spacing=14;stack.translatesAutoresizingMaskIntoConstraints=NO;
    UIScrollView *scroll=[[UIScrollView alloc]init];scroll.translatesAutoresizingMaskIntoConstraints=NO;[self.view addSubview:scroll];[scroll addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],[scroll.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor],[scroll.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:20],[stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-20],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:24],[stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-24],
        [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-48],[self.service.heightAnchor constraintEqualToConstant:42]
    ]];
    self.timer=[NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(tick) userInfo:nil repeats:YES];
}
- (void)close {
    if(self.searching && current_state!=HALO_MATCH_PLAYING){self.status.text=@"Cancel the search before closing, or wait for the match.";return;}
    [self dismissViewControllerAnimated:YES completion:nil];
}
- (void)request:(NSString *)method path:(NSString *)path body:(NSDictionary *)body completion:(void (^)(NSDictionary *,NSString *))completion {
    NSURL *url=[NSURL URLWithString:[self.base stringByAppendingString:path]];
    NSMutableURLRequest *request=[NSMutableURLRequest requestWithURL:url];request.HTTPMethod=method;request.timeoutInterval=10;
    [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
    if(self.token)[request setValue:[@"Bearer " stringByAppendingString:self.token] forHTTPHeaderField:@"Authorization"];
    if(body)request.HTTPBody=[NSJSONSerialization dataWithJSONObject:body options:0 error:nil];
    NSUInteger generation=self.generation;
    NSString *base=self.base;
    [[[NSURLSession sharedSession]dataTaskWithRequest:request completionHandler:^(NSData *data,NSURLResponse *response,NSError *error) {
        NSDictionary *result=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;
        NSInteger code=[response isKindOfClass:NSHTTPURLResponse.class]?((NSHTTPURLResponse *)response).statusCode:0;
        NSString *message=error.localizedDescription;
        if(!message && (code<200 || code>=300) && [result isKindOfClass:NSDictionary.class] && [result[@"error"]isKindOfClass:NSString.class])message=result[@"error"];
        if(!message && (code<200 || code>=300))message=@"The matchmaking service could not complete the request.";
        dispatch_async(dispatch_get_main_queue(),^{
            if(self.generation==generation)completion([result isKindOfClass:NSDictionary.class]?result:nil,message);
            else if([method isEqualToString:@"POST"] && [path isEqualToString:@"/v1/tickets"] && [result isKindOfClass:NSDictionary.class] && [result[@"id"]isKindOfClass:NSString.class] && [result[@"token"]isKindOfClass:NSString.class]) {
                /* Cancel may race ticket creation. Revoke the returned ticket. */
                NSMutableURLRequest *cleanup=[NSMutableURLRequest requestWithURL:[NSURL URLWithString:[base stringByAppendingFormat:@"/v1/tickets/%@",result[@"id"]]]];
                cleanup.HTTPMethod=@"DELETE";cleanup.timeoutInterval=10;
                [cleanup setValue:[@"Bearer " stringByAppendingString:result[@"token"]] forHTTPHeaderField:@"Authorization"];
                [[[NSURLSession sharedSession]dataTaskWithRequest:cleanup]resume];
            }
        });
    }]resume];
}
- (void)findMatch {
    if(self.searching || current_state!=HALO_MATCH_IDLE || atomic_load(&next_command)!=HALO_MATCH_NONE){self.status.text=@"Leave the current match and wait for Halo’s main menu before searching again.";return;}
    NSURL *url=[NSURL URLWithString:self.service.text];
    BOOL loopback=[@[@"localhost",@"127.0.0.1"]containsObject:url.host];
    if(!url.host.length || url.user.length || url.password.length || url.query.length || url.fragment.length || (![url.scheme isEqualToString:@"https"] && !(loopback && [url.scheme isEqualToString:@"http"]))) {
        self.status.text=@"Enter the HTTPS URL of your matchmaking service. The repository includes a service you can deploy.";return;
    }
    self.base=[self.service.text stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"/"]];
    [[NSUserDefaults standardUserDefaults]setObject:self.base forKey:@"HaloMatchmakingService"];
    self.searching=YES;self.find.enabled=self.practice.enabled=NO;self.status.text=@"Looking for players…";self.generation++;
    NSUInteger generation=self.generation;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
      NSString *compatibility=map_compatibility();
      dispatch_async(dispatch_get_main_queue(),^{
        if(self.generation!=generation)return;
        if(!compatibility){[self fail:@"Import Halo’s game files before finding a match."];return;}
        [self request:@"POST" path:@"/v1/tickets" body:@{@"playlist":@"bloodgulch-slayer-8",@"compatibility":compatibility,@"name":@"Spartan"} completion:^(NSDictionary *result,NSString *error) {
        if(error){[self fail:error];return;}
        if(![result[@"id"]isKindOfClass:NSString.class] || ![result[@"token"]isKindOfClass:NSString.class]){[self fail:@"The service returned an invalid ticket."];return;}
        self.ticket=result[@"id"];self.token=result[@"token"];
        if(!self.ticket.length || !self.token.length){[self fail:@"The service returned an invalid ticket."];return;}
        [self applyTicket:result];
        }];
      });
    });
}
- (void)fail:(NSString *)message {
    if(![self.status.text isEqualToString:message])host_logf(HOST_LOG_WARN,"matchmaking: %s",message.UTF8String);
    self.status.text=message;self.searching=NO;self.find.enabled=self.practice.enabled=!self.commandIssued;
}
- (void)practiceMatch {
    if(self.searching || current_state!=HALO_MATCH_IDLE || atomic_load(&next_command)!=HALO_MATCH_NONE){self.status.text=@"Leave the current match and wait for Halo’s main menu before starting practice.";return;}
    self.status.text=@"Preparing an offline match with 7 bots…";issue_command(HALO_MATCH_PRACTICE,nil);
}
- (void)cancelMatch {
    if(self.ticket.length)[self request:@"DELETE" path:[@"/v1/tickets/" stringByAppendingString:self.ticket] body:nil completion:^(NSDictionary *r,NSString *e){}];
    self.generation++;self.searching=NO;self.ticket=self.token=self.role=self.lastInvite=nil;self.commandIssued=self.startIssued=self.inFlight=NO;
    self.find.enabled=self.practice.enabled=YES;issue_command(HALO_MATCH_CANCEL,nil);self.status.text=@"Search cancelled. Returning to Halo.";
}
- (void)applyTicket:(NSDictionary *)ticket {
    if(![ticket[@"role"]isKindOfClass:NSString.class] || ![ticket[@"state"]isKindOfClass:NSString.class] || (ticket[@"startsAt"] && ![ticket[@"startsAt"]isKindOfClass:NSNumber.class])) {
        [self fail:@"The service returned an invalid match. Cancel and try again."];return;
    }
    self.role=ticket[@"role"];
    if([ticket[@"state"]isEqualToString:@"cancelled"] || [ticket[@"state"]isEqualToString:@"expired"]) {[self fail:@"The host left. Cancel this search and try again."];return;}
    if(![@[@"host",@"join"]containsObject:self.role] || !ticket[@"startsAt"]) {
        [self fail:@"The service returned an invalid match. Cancel and try again."];return;
    }
    self.deadline=[NSDate dateWithTimeIntervalSince1970:[ticket[@"startsAt"]doubleValue]/1000];
    if([self.role isEqualToString:@"host"] && !self.commandIssued){self.commandIssued=YES;issue_command(HALO_MATCH_HOST,nil);}
    if([self.role isEqualToString:@"join"] && [ticket[@"invite"]isKindOfClass:NSString.class] && !self.commandIssued) {
        self.commandIssued=YES;issue_command(HALO_MATCH_JOIN,ticket[@"invite"]);
    }
    if(current_state==HALO_MATCH_LOBBY && [self.role isEqualToString:@"host"] && self.deadline.timeIntervalSinceNow<=0 && !self.startIssued) {
        self.startIssued=YES;issue_command(HALO_MATCH_START,nil);
    }
}
- (void)tick {
    if(current_state==HALO_MATCH_ERROR){[self fail:current_detail.length?current_detail:@"Halo could not prepare this match."];return;}
    if(current_state==HALO_MATCH_FINISHED){self.status.text=@"Round finished. Leave the match to search again.";}
    if(current_state==HALO_MATCH_IDLE && self.commandIssued){[self cancelMatch];return;}
    if(current_state==HALO_MATCH_PLAYING){self.status.text=[NSString stringWithFormat:@"Match running · %d people · %d bots",current_humans,current_bots];self.startIssued=YES;}
    else if(current_state==HALO_MATCH_LOADING)self.status.text=@"Loading Blood Gulch…";
    else if(current_state==HALO_MATCH_LOBBY)self.status.text=[NSString stringWithFormat:@"Waiting for players · %d connected · empty seats fill with bots",current_humans];
    if(!self.searching || !self.ticket || self.inFlight)return;
    self.inFlight=YES;
    NSString *path=[@"/v1/tickets/" stringByAppendingString:self.ticket];
    NSDictionary *body=nil;NSString *method=@"GET";
    if([self.role isEqualToString:@"host"] && [current_detail hasPrefix:@"halo://join/"] && ![self.lastInvite isEqualToString:current_detail]) {
        body=@{@"invite":current_detail};method=@"PATCH";
    }
    [self request:method path:path body:body completion:^(NSDictionary *result,NSString *error) {
        self.inFlight=NO;
        if(error){[self fail:error];return;}
        if(body)self.lastInvite=body[@"invite"];
        [self applyTicket:result];
    }];
}
@end

#if TARGET_OS_SIMULATOR
/* Opt-in integration harness exercises the same coordinator as the buttons.
   It is excluded from physical-device builds and never runs by default. */
static void simulator_automation(void) {
    static CFAbsoluteTime first_frame;
    static int stage;
    const char *mode=getenv("HALO_MATCHMAKING_AUTOSTART");
    if(!mode)return;
    /* A null-renderer fixture has no SDL window to pump UIKit's run loop. */
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,0,true);
    if(!first_frame)first_frame=CFAbsoluteTimeGetCurrent();
    CFAbsoluteTime elapsed=CFAbsoluteTimeGetCurrent()-first_frame;
    if(stage==0 && elapsed>12) {
        stage=1;
        host_logf(HOST_LOG_INFO,"matchmaking integration: starting %s",mode);
        if(!match_controller)match_controller=[[HaloMatchmakingController alloc]init];
        [match_controller loadViewIfNeeded];
        if(getenv("HALO_MATCHMAKING_TEST_PRESENT"))host_ios_matchmaking_present();
        if(!strcmp(mode,"queue")) {
            const char *service=getenv("HALO_MATCHMAKING_TEST_SERVICE");
            match_controller.service.text=service?[NSString stringWithUTF8String:service]:@"http://127.0.0.1:8787";
            [match_controller findMatch];
        } else if(!strcmp(mode,"practice")) [match_controller practiceMatch];
    }
    static CFAbsoluteTime playing_at;
    if(current_state==HALO_MATCH_PLAYING && !playing_at)playing_at=CFAbsoluteTimeGetCurrent();
    if(playing_at && CFAbsoluteTimeGetCurrent()-playing_at>8 && match_controller.presentingViewController)
        [match_controller close];
    const char *cancel=getenv("HALO_MATCHMAKING_CANCEL_AFTER");
    if(stage==1 && cancel && elapsed>strtod(cancel,NULL)) {
        stage=2;[match_controller cancelMatch];
    }
    if(stage==2 && elapsed>strtod(cancel,NULL)+8 && current_state==HALO_MATCH_IDLE) {
        stage=3;[match_controller practiceMatch];
    }
}
#endif

void host_ios_matchmaking_attach(UIWindow *window) {game_window=window;}
void host_ios_matchmaking_present(void) {
    UIViewController *root=game_window.rootViewController;
    if(!root || root.presentedViewController)return;
    host_ios_touch_reset();
    HaloMatchmakingController *controller=match_controller ?: [[HaloMatchmakingController alloc]init];
    controller.modalPresentationStyle=UIModalPresentationFullScreen;match_controller=controller;
    [root presentViewController:controller animated:YES completion:nil];
}
