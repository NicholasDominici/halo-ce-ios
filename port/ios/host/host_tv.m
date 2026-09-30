/* tvOS stand-ins for the iOS touch controls, orientation lock and XISO importer.
   Apple TV has no touch screen or Files app: the Siri Remote stands in for the
   touch pad, and the XISO arrives from a browser (host_tv_import.m). */
#import <UIKit/UIKit.h>
#include <SDL3/SDL.h>
#include "ios_host.h"
#include "xiso.h"
#include <stdlib.h>

/* The Siri Remote isn't a joystick (host_main.m), so SDL delivers its presses
   and swipes as keys. They drive a virtual "Halo Remote" gamepad that, like the
   iOS touch pad, owns player one (its buttons are only ever read through
   remote_read) and merges in the first hardware controller:
   either can work the menus, and Back backs out (B). The remote's keys are
   consumed here: the game's keyboard mapping (xinput_sdl.c) would otherwise
   also read Back, which SDL reports as Escape, as Start. */
static SDL_Joystick *remote_joystick;
static SDL_JoystickID remote_id, primary_hardware;
/* Remote presses latch until the game reads them: a swipe's key down and up
   arrive together, and a slow frame could otherwise miss a short press. Key
   events are pumped by the game's own poll, so all of this is on one thread. */
static bool remote_down[SDL_GAMEPAD_BUTTON_COUNT], remote_latched[SDL_GAMEPAD_BUTTON_COUNT];

static int remote_button(SDL_Scancode key) {
    switch(key) {
    case SDL_SCANCODE_UP:return SDL_GAMEPAD_BUTTON_DPAD_UP;
    case SDL_SCANCODE_DOWN:return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    case SDL_SCANCODE_LEFT:return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    case SDL_SCANCODE_RIGHT:return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    case SDL_SCANCODE_RETURN:return SDL_GAMEPAD_BUTTON_SOUTH;
    case SDL_SCANCODE_ESCAPE:return SDL_GAMEPAD_BUTTON_EAST;
    case SDL_SCANCODE_PAUSE:return SDL_GAMEPAD_BUTTON_START;
    default:return -1;
    }
}
static bool SDLCALL remote_event(void *userdata,SDL_Event *event) {
    (void)userdata;
    if(event->type!=SDL_EVENT_KEY_DOWN && event->type!=SDL_EVENT_KEY_UP)return true;
    int button=remote_button(event->key.scancode);
    if(button<0)return true;
    if(!event->key.repeat) {
        remote_down[button]=event->key.down;
        if(event->key.down)remote_latched[button]=true;
    }
    return false;
}
static bool remote_read(int button) {
    if(button<0 || button>=SDL_GAMEPAD_BUTTON_COUNT)return false;
    bool pressed=remote_down[button] || remote_latched[button];
    remote_latched[button]=false;
    return pressed;
}

void host_ios_touch_initialize(void) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.name="Halo Remote";
    remote_id=SDL_AttachVirtualJoystick(&desc);
    remote_joystick=SDL_OpenJoystick(remote_id);
    if(!remote_joystick)host_fatal("Could not initialize remote controls: %s",SDL_GetError());
    SDL_SetJoystickVirtualAxis(remote_joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);
    SDL_SetJoystickVirtualAxis(remote_joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);
    SDL_SetEventFilter(remote_event,NULL);
}
void host_ios_touch_reset(void) {
    for(int i=0;i<SDL_GAMEPAD_BUTTON_COUNT;i++)remote_down[i]=remote_latched[i]=false;
}

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

/* Port assignment and merging mirror host_touch.m, with the remote in place of touch. */
int host_ios_gamepads(uint32_t *out,int capacity) {
    int count=0,used=0;SDL_JoystickID *ids=SDL_GetGamepads(&count);
    primary_hardware=0;
    if(capacity>0 && remote_id)out[used++]=remote_id;
    for(int i=0;i<count;i++) {
        if(ids[i]==remote_id)continue;
        if(!primary_hardware) {
            primary_hardware=ids[i];
            if(!SDL_GetGamepadFromID(ids[i]))SDL_OpenGamepad(ids[i]);
        } else if(used<capacity) out[used++]=ids[i];
    }
    SDL_free(ids);return used;
}
int host_ios_gamepad_type(SDL_Gamepad *pad) {
    SDL_Gamepad *physical=primary_hardware?SDL_GetGamepadFromID(primary_hardware):NULL;
    if(SDL_GetGamepadID(pad)!=remote_id)return SDL_GetGamepadType(pad);
    return physical?SDL_GetGamepadType(physical):SDL_GAMEPAD_TYPE_XBOX360;
}
int host_ios_gamepad_axis(SDL_Gamepad *pad,int axis) {
    int value=SDL_GetGamepadAxis(pad,axis);
    if(SDL_GetGamepadID(pad)==remote_id && primary_hardware) {
        int physical=SDL_GetGamepadAxis(SDL_GetGamepadFromID(primary_hardware),axis);
        if(abs(physical)>abs(value))value=physical;
    }
    return value;
}
int host_ios_gamepad_button(SDL_Gamepad *pad,int button) {
    if(SDL_GetGamepadID(pad)!=remote_id)return SDL_GetGamepadButton(pad,button);
    return remote_read(button) ||
        (primary_hardware && SDL_GetGamepadButton(SDL_GetGamepadFromID(primary_hardware),button));
}

/* The player's XISO is imported into root/maps over the local network
   (host_tv_import.m). A personal build can instead bundle the maps, which
   host_main.m hands the game as HALO_DISC_ROOT; tvOS forbids symlinking them
   into Caches. A complete root/maps takes precedence, matching
   platform_translate_path in port/linux/src/xbox_files.c. */
void host_ios_prepare_assets(const char *root) {
    NSString *imported=[[NSString stringWithUTF8String:root] stringByAppendingPathComponent:@"maps"];
    NSString *bundled=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"maps"];
    char reason[1024]={0};
    for(NSString *maps in @[imported,bundled]) {
        if(xiso_maps_ready(maps.fileSystemRepresentation,reason,sizeof(reason))) {
            host_logf(HOST_LOG_INFO,"maps: %s",maps.fileSystemRepresentation);
            return;
        }
    }
    /* Caches is purgeable, so this also runs again if tvOS evicted the maps. */
    host_tv_import(root);
    if(!xiso_maps_ready(imported.fileSystemRepresentation,reason,sizeof(reason)))
        host_fatal("The imported maps are not usable (%s).",reason);
    host_logf(HOST_LOG_INFO,"maps: %s",imported.fileSystemRepresentation);
}
