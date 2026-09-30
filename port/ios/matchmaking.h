/* Commands/status cross the ILP32 boundary as fixed-width scalar values. */
#ifndef HALO_IOS_MATCHMAKING_H
#define HALO_IOS_MATCHMAKING_H
enum halo_match_command {
    HALO_MATCH_NONE, HALO_MATCH_HOST, HALO_MATCH_JOIN, HALO_MATCH_START,
    HALO_MATCH_CANCEL, HALO_MATCH_PRACTICE
};
enum halo_match_state {
    HALO_MATCH_IDLE, HALO_MATCH_PREPARING, HALO_MATCH_LOBBY,
    HALO_MATCH_LOADING, HALO_MATCH_PLAYING, HALO_MATCH_ERROR, HALO_MATCH_FINISHED
};
void halo_ios_matchmaking_frame(void);
void halo_ios_bot_actions(void);
void halo_ios_bots_reset(void);
void halo_ios_bot_machine_set(int machine, int enabled);
int halo_ios_bot_machine(int machine);
int halo_ios_server_fill_bots(void *server, int target);
int halo_ios_server_player_count(void *server);
void halo_ios_server_match_limit(void *server);
void halo_ios_client_counts(void *client, int *humans, int *bots);
#endif
