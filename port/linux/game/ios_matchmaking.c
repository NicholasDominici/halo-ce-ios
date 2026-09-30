/* Host-owned practice opponents feed the existing authoritative action queues.
   Clients receive ordinary Halo player actions; bots have no fake connections. */
#ifdef HALO_IOS
#include "cseries/cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "game/player_queues_new.h"
#include "interface/player_ui.h"
#include "interface/ui_widget.h"
#include "main/main.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager_internal.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "units/units.h"
#include "../../ios/matchmaking.h"
#include "../../runtime/guest/runtime/guest_host.h"
#include "../src/p2p.h"
#include <math.h>
#include <string.h>

extern struct game_variant *build_game_variant_slayer(struct game_variant *);
extern boolean main_menu_is_active(void);
static unsigned char bot_machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
static int match_state, human_count, bot_count, pending_command, start_requested;
static unsigned long preparation_started;
static unsigned char join_identifier[6];
static char failure_detail[256];
extern boolean halo_ios_client_join_address(struct network_game_client *,unsigned long);

void halo_ios_bot_machine_set(int machine, int enabled) {
    if(machine>0 && machine<HALO_PORT_MAXIMUM_NETWORK_MACHINES)bot_machines[machine]=enabled!=0;
}
int halo_ios_bot_machine(int machine) {
    return machine>0 && machine<HALO_PORT_MAXIMUM_NETWORK_MACHINES && bot_machines[machine];
}
void halo_ios_bots_reset(void) {memset(bot_machines,0,sizeof(bot_machines));bot_count=0;}

void halo_ios_bot_actions(void) {
    struct data_iterator iterator;
    struct player_datum *player;
    if(!bot_count || game_connection()!=_game_connection_network_server)return;
    data_iterator_new(&iterator,player_data);
    while((player=data_iterator_next(&iterator))!=NULL) {
        struct player_action actions[MAXIMUM_LOCAL_PLAYERS];
        real_point3d origin, target, closest_target;
        struct collision_result collision;
        struct data_iterator others;
        struct player_datum *other;
        float closest=1e30f,dx=0,dy=0,dz=0,distance;
        int machine=player->network_player_data.machine_index;
        long tick=game_time_get();
        if(!halo_ios_bot_machine(machine))continue;
        memset(actions,0,sizeof(actions));
        actions[0].desired_weapon_index=NONE;
        actions[0].desired_grenade_index=NONE;
        actions[0].desired_zoom_level=NONE;
        if(player->unit_index!=NONE && object_get(player->unit_index)->object.body_vitality>0) {
            object_get_origin(player->unit_index,&origin);origin.z+=0.6f;
            data_iterator_new(&others,player_data);
            while((other=data_iterator_next(&others))!=NULL) {
                float x,y,z,d;
                if(other==player || other->unit_index==NONE || other->quit_out_of_game || object_get(other->unit_index)->object.body_vitality<=0)continue;
                object_get_origin(other->unit_index,&target);target.z+=0.6f;
                x=target.x-origin.x;y=target.y-origin.y;z=target.z-origin.z;
                d=x*x+y*y+z*z;
                if(d<closest){closest=d;dx=x;dy=y;dz=z;closest_target=target;}
            }
            if(closest<1e29f) {
                distance=sqrtf(dx*dx+dy*dy);
                actions[0].desired_facing.yaw=atan2f(dy,dx);
                actions[0].desired_facing.pitch=atan2f(dz,distance+0.001f);
                /* Basic practice AI, using normal Halo movement and weapons. */
                actions[0].throttle.i=distance>7?0.8f:(distance<3?-0.5f:0);
                actions[0].throttle.j=((tick/45+machine)%2)?0.5f:-0.5f;
                if(distance<35 && tick%120<90 && !collision_test_line(_collision_test_for_line_of_sight_flags,&origin,&closest_target,player->unit_index,&collision)) {
                    actions[0].control_flags|=FLAG(_unit_control_weapon_primary_trigger_bit);
                    actions[0].primary_trigger=1.0f;
                }
                if(tick%150==0)actions[0].control_flags|=FLAG(_unit_control_weapon_reload_bit);
                if((tick+machine*7)%105==0)actions[0].control_flags|=FLAG(_unit_control_jump_bit);
            }
        }
        update_server_handle_client_update(machine,actions);
    }
}

void halo_ios_matchmaking_frame(void) {
    char invite[256]={0};
    int command=host_matchmaking_command(invite,sizeof(invite));
    struct network_game_server *server=global_network_game_server_get();
    if(command==HALO_MATCH_CANCEL) {
        p2p_matchmaking_begin(0);
        halo_ios_bots_reset();pending_command=0;start_requested=0;match_state=HALO_MATCH_IDLE;
        failure_detail[0]=0;human_count=0;
        if(network_game_is_active())network_game_abort();
        host_matchmaking_status(match_state,0,0,"");return;
    }
    if(command==HALO_MATCH_HOST || command==HALO_MATCH_PRACTICE || command==HALO_MATCH_JOIN) {
        if(network_game_is_active() || !main_menu_is_active()) {
            match_state=HALO_MATCH_ERROR;strcpy(failure_detail,"Return to the main menu before finding a match.");
            host_matchmaking_status(match_state,0,0,failure_detail);return;
        }
        pending_command=command;start_requested=0;match_state=HALO_MATCH_PREPARING;
        failure_detail[0]=0;human_count=0;
        halo_ios_bots_reset();
        p2p_matchmaking_begin(command!=HALO_MATCH_PRACTICE);
        player_ui_clear_multiplayer_joins();
        player_ui_local_player_joined_multiplayer_game(0);
        if(command==HALO_MATCH_JOIN) {
            int i,value;
            for(i=0;i<6;i++){if(sscanf(invite+12+i*2,"%2x",&value)!=1)break;join_identifier[i]=(unsigned char)value;}
            ui_widgets_close_all();
            ui_widget_load_by_name_or_tag("ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen",NONE,NULL,NONE,NONE,NONE,NONE);
            if(i!=6 || !p2p_join_invite(invite)){match_state=HALO_MATCH_ERROR;strcpy(failure_detail,"The service returned an invalid game invite.");}
            if(!create_global_network_game_client()){match_state=HALO_MATCH_ERROR;strcpy(failure_detail,"Could not open Halo’s client sockets. Cancel and check your network settings.");}
            else game_connection_set(_game_connection_network_client);
        } else {
            struct game_variant variant;
            player_ui_fast_setup_network_server();
            server=global_network_game_server_get();
            if(server) {
                halo_ios_server_match_limit(server);
                if(command==HALO_MATCH_PRACTICE)network_game_accept_remote_connections(FALSE);
                build_game_variant_slayer(&variant);
                network_game_server_pause_countdown(server,TRUE);
                network_game_server_change_game_variant(server,&variant);
                network_game_server_change_map_name(server,"levels\\test\\bloodgulch\\bloodgulch");
            } else match_state=HALO_MATCH_ERROR;
        }
        preparation_started=system_milliseconds();
    }
    server=global_network_game_server_get();
    if(pending_command==HALO_MATCH_JOIN && match_state!=HALO_MATCH_ERROR) {
        struct network_game_client *client=global_network_game_client_get();
        if(client) {
            int state=network_game_client_get_state(client,NULL);
            unsigned long address;
            if(state==0 && p2p_peer_address(join_identifier,&address))halo_ios_client_join_address(client,address);
            else if(state==2)match_state=HALO_MATCH_LOBBY;
            else if(state==3)match_state=HALO_MATCH_PLAYING;
            else if(state==4)match_state=HALO_MATCH_FINISHED;
            halo_ios_client_counts(client,&human_count,&bot_count);
            if(network_game_client_get_error(client)){match_state=HALO_MATCH_ERROR;strcpy(failure_detail,"Halo could not join the host. Cancel and try again.");}
        }
    }
    if(server && pending_command && match_state!=HALO_MATCH_ERROR) {
        int state=network_game_server_get_state(server,NULL);
        human_count=halo_ios_server_player_count(server)-bot_count;
        if(state==0 && human_count>0 && !start_requested) {
            match_state=HALO_MATCH_LOBBY;
            if(command==HALO_MATCH_START || (pending_command==HALO_MATCH_PRACTICE && system_milliseconds()-preparation_started>1500)) {
                start_requested=1;bot_count=halo_ios_server_fill_bots(server,8);
                human_count=halo_ios_server_player_count(server)-bot_count;
                network_game_client_request_immediate_start();
                network_game_server_pause_countdown(server,FALSE);
                match_state=HALO_MATCH_LOADING;
            }
        } else if(state==1)match_state=HALO_MATCH_PLAYING;
        else if(state==2)match_state=HALO_MATCH_FINISHED;
        p2p_copy_invite(invite,sizeof(invite));
    }
    if(pending_command && match_state!=HALO_MATCH_PREPARING && match_state!=HALO_MATCH_ERROR && !network_game_is_active()) {
        p2p_matchmaking_begin(0);halo_ios_bots_reset();pending_command=0;start_requested=0;match_state=HALO_MATCH_IDLE;human_count=0;
    }
    if(match_state==HALO_MATCH_PLAYING && game_in_progress()) {
        struct data_iterator iterator;
        struct player_datum *player;
        int alive=0,deaths=0,kills=0;
        data_iterator_new(&iterator,player_data);
        while((player=data_iterator_next(&iterator))!=NULL) {
            int i;
            if(player->unit_index!=NONE && object_get(player->unit_index)->object.body_vitality>0)alive++;
            deaths+=player->statistics.deaths;
            for(i=0;i<4;i++)kills+=player->statistics.kills[i];
        }
        snprintf(invite,sizeof(invite),"tick=%ld alive=%d kills=%d deaths=%d",game_time_get(),alive,kills,deaths);
    }
    if(match_state==HALO_MATCH_PREPARING && system_milliseconds()-preparation_started>30000) {
        match_state=HALO_MATCH_ERROR;
        strcpy(failure_detail,"The game did not connect. Try Practice or check the network.");
    }
    host_matchmaking_status(match_state,human_count,bot_count,match_state==HALO_MATCH_ERROR?failure_detail:invite);
}
#endif
