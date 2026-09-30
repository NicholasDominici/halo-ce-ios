/* tvOS stand-ins for the iOS touch controls, orientation lock and XISO importer.
   Apple TV has hardware controllers only and no Files app, so gamepads pass
   straight through and the maps ship inside the signed app bundle. */
#import <UIKit/UIKit.h>
#include <SDL3/SDL.h>
#include "ios_host.h"
#include "xiso.h"
#include <unistd.h>

void host_ios_touch_initialize(void) {}
void host_ios_touch_reset(void) {}

void host_ios_touch_attach(SDL_Window *window) {
    UIWindow *native=(__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER,NULL);
    /* Same launch-window retirement as the iOS host (host_touch.m). */
    for(UIWindow *candidate in native.windowScene.windows) {
        if(candidate!=native && [NSStringFromClass(candidate.rootViewController.class) hasPrefix:@"SDLLaunch"])
            candidate.hidden=YES;
    }
    [native makeKeyAndVisible];
    host_logf(HOST_LOG_INFO,"tvOS window attached, %.0fx%.0f",native.bounds.size.width,native.bounds.size.height);
}

int host_ios_gamepads(uint32_t *out,int capacity) {
    int count=0,used=0;SDL_JoystickID *ids=SDL_GetGamepads(&count);
    for(int i=0;i<count && used<capacity;i++) {
        if(!SDL_GetGamepadFromID(ids[i]))SDL_OpenGamepad(ids[i]);
        out[used++]=ids[i];
    }
    SDL_free(ids);return used;
}
int host_ios_gamepad_type(SDL_Gamepad *pad) {return SDL_GetGamepadType(pad);}
int host_ios_gamepad_axis(SDL_Gamepad *pad,int axis) {return SDL_GetGamepadAxis(pad,axis);}
int host_ios_gamepad_button(SDL_Gamepad *pad,int button) {return SDL_GetGamepadButton(pad,button);}

/* root is a writable Caches folder. Point root/maps at the bundle's maps; the
   bundle path changes on every install, so the link is rebuilt each launch. */
void host_ios_prepare_assets(const char *root) {
    NSString *bundled=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"maps"];
    NSString *maps=[[NSString stringWithUTF8String:root] stringByAppendingPathComponent:@"maps"];
    NSFileManager *files=NSFileManager.defaultManager;
    NSDictionary *existing=[files attributesOfItemAtPath:maps error:nil];
    if(existing && [existing.fileType isEqualToString:NSFileTypeSymbolicLink])[files removeItemAtPath:maps error:nil];
    if(![files fileExistsAtPath:maps] && symlink(bundled.fileSystemRepresentation,maps.fileSystemRepresentation))
        host_fatal("Could not link the game maps: %s",strerror(errno));
    char reason[1024]={0};
    if(!xiso_maps_ready(maps.fileSystemRepresentation,reason,sizeof(reason)))
        host_fatal("This build has no usable Halo maps (%s). Rebuild with --maps pointing at maps extracted from your XISO.",reason);
    host_logf(HOST_LOG_INFO,"maps: %s",bundled.fileSystemRepresentation);
}
