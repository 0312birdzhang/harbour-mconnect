#include <stdint.h>
typedef struct { int status, alive; int64_t position; } Player;
static Player player;
static int open_status = 2, use_after_destroy = 0;
static void check(Player* p) { if (!p->alive) use_after_destroy++; }
void* mc_audio_create(void) { player = (Player){0, 1, 0}; use_after_destroy = 0; return &player; }
void mc_audio_destroy(Player* p) { p->alive = 0; }
int mc_audio_open(Player* p, const char* uri) { check(p); p->status = open_status; return uri != 0; }
int mc_audio_state(Player* p, int state) { check(p); p->status = state == 2 ? 3 : state == 1 ? 2 : 0; return 1; }
int64_t mc_audio_position(Player* p) { check(p); return p->position; }
int64_t mc_audio_duration(Player* p) { check(p); return 180000000; }
int mc_audio_seek(Player* p, int64_t position) { check(p); p->position = position; if (p->status == 4) p->status = 2; return 1; }
int mc_audio_rate(Player* p, double rate) { check(p); return rate > 0; }
void mc_audio_volume(Player* p, double volume) { check(p); (void)volume; }
int mc_audio_equalizer(Player* p, const double* bands) { check(p); return bands != 0; }
int mc_audio_status(Player* p) { check(p); return p->status; }
const char* mc_audio_error(Player* p) { check(p); return "Test decoder failure"; }
void mc_test_status(int status) { player.status = status; }
void mc_test_open_status(int status) { open_status = status; }
int mc_test_use_after_destroy(void) { return use_after_destroy; }
