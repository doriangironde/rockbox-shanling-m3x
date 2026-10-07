#include <assert.h>
#include <string.h>
#include "../../../rockbox/firmware/target/hosted/ibasso/m3x/audiohw-m3x.c"

struct mixer { int unused; };
struct mixer_ctl { const char *name; int value; const char *selection; };
static struct mixer fake_mixer;
static struct mixer_ctl controls[] = {
    {M3X_CTL_VOL_L, 0, NULL}, {M3X_CTL_VOL_R, 0, NULL},
    {M3X_CTL_MUTE, 0, NULL}, {M3X_CTL_FILTER, 0, NULL},
};
static int left, right, opens, closes;
struct mixer *mixer_open(unsigned int card) { assert(card == 0); ++opens; return &fake_mixer; }
void mixer_close(struct mixer *m) { assert(m == &fake_mixer); ++closes; }
const char *mixer_get_name(struct mixer *m) { (void)m; return "test"; }
unsigned int mixer_get_num_ctls(struct mixer *m) { (void)m; return 4; }
struct mixer_ctl *mixer_get_ctl_by_name(struct mixer *m, const char *name)
{
    assert(m == &fake_mixer);
    for (unsigned i = 0; i < 4; ++i)
        if (!strcmp(controls[i].name, name)) return &controls[i];
    return NULL;
}
int mixer_ctl_set_value(struct mixer_ctl *c, unsigned int id, int value)
{ assert(id == 0); c->value = value; return 0; }
int mixer_ctl_set_enum_by_string(struct mixer_ctl *c, const char *value)
{ c->selection = value; return 0; }
void pcm_set_master_volume(int l, int r) { left = l; right = r; }
int main(void)
{
    audiohw_m3x_init();
    assert(opens == 1 && controls[0].value == 50 && controls[1].value == 50);
    assert(!strcmp(controls[2].selection, "mute"));
    audiohw_m3x_mixer_open(); assert(opens == 1);
    audiohw_set_volume(-300, -600); assert(left == -300 && right == -600);
    audiohw_set_volume(-1280, 100); assert(left == PCM_MUTE_LEVEL && right == 0);
    audiohw_set_volume(-2000, -1280); assert(left == PCM_MUTE_LEVEL && right == PCM_MUTE_LEVEL);
    audiohw_m3x_unmute(); assert(!strcmp(controls[2].selection, "unmute"));
    for (int i = 0; i < 6; ++i) {
        audiohw_set_filter_roll_off(i);
        const char *expected[] = {"sharp", "slow", "delay-sharp", "delay-slow", "supp-slow", "delay-sslow"};
        assert(!strcmp(controls[3].selection, expected[i]));
    }
    audiohw_set_filter_roll_off(99); assert(!strcmp(controls[3].selection, "sharp"));
    audiohw_m3x_fallback_route(); assert(!strcmp(controls[2].selection, "mute"));
    audiohw_m3x_mixer_close(); audiohw_m3x_mixer_close(); assert(closes == 1);
    return 0;
}
