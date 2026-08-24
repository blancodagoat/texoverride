// Checks the safety gate against src/streaming/gate.h directly.
#include "../src/streaming/gate.h"
#include <string>
#include <cstdio>

static int fails = 0;
static void want(bool got, bool exp, const char* k)
{
    if (got != exp) { printf("FAIL %-46s got %d want %d\n", k, (int)got, (int)exp); ++fails; }
}
#define ALLOW(k) want(isAllowedKey(k), true,  k)
#define DENY(k)  want(isAllowedKey(k), false, k)

int main()
{
    // the Police K9 Shepherd mod, exactly as it ships
    ALLOW("a_c_shepherd/head_000_r.ydd");
    ALLOW("a_c_shepherd/head_diff_000_d_whi.ytd");
    ALLOW("a_c_shepherd/accs_diff_000_h_uni.ytd");
    ALLOW("a_c_shepherd/uppr_000_u.ydd");
    ALLOW("a_c_shepherd.yft");
    // the other collection-based animals
    ALLOW("a_c_chop/teef_000_u.ydd");
    ALLOW("a_c_husky/lowr_diff_000_a_whi.ytd");
    ALLOW("a_c_panther/uppr_000_r.ydd");
    // animals that are not collection-based: bare model, fragment and variation metadata
    ALLOW("a_c_pug.ydd"); ALLOW("a_c_pug.ytd"); ALLOW("a_c_pug.yft"); ALLOW("a_c_pug.ymt");   // no vanilla pug .ymt, so it is claimable
    ALLOW("a_c_westy.ydd"); ALLOW("a_c_cat_01.ytd"); ALLOW("a_c_rottweiler_02.ydd");
    // freemode, unchanged
    ALLOW("mp_m_freemode_01/teef_004_u.ydd");
    ALLOW("mp_f_freemode_01_mp_f_2023_02/uppr_012_r.ydd");
    ALLOW("mp_fm_skin_m_up_whi.ytd");
    ALLOW("mp_fm_faov_makeup_031.ytd");

    // anim dictionaries at the root: exact name, .ycd only
    ALLOW("mp_player_int_uppergang_sign_a.ycd");
    ALLOW("agangsign2@animation.ycd");
    ALLOW("amb@code_human_in_car_mp_actions@gang_sign_a@std@ds@base.ycd");
    DENY("mp_m_freemode_01/uppr_000_r.ycd");   // a .ycd never belongs to a collection folder

    // still refused: everything that is not a ped
    DENY("adder/adder.ytd");                 // vehicle collection
    DENY("prop_bench_01a.ydd");              // prop model, no a_c_ name
    DENY("onx_sandy_01.ydr");                // map drawable
    DENY("s_m_y_cop_01/head_000_r.ydd");     // story ped collection
    DENY("player_zero/uppr_000_r.ydd");
    DENY("cs_lamardavis/head_000_r.ydd");    // 147 cs_ collections in the game, all refused
    DENY("ig_lamardavis/uppr_000_u.ydd");
    DENY("hc_gunman/feet_000_r.ydd");
    DENY("mp_m_niko_01/head_000_r.ydd");     // an mp_ ped that is not freemode
    DENY("a_c_shepherd.rpf");                // not a streamable type
    DENY("a_c_shepherd");                    // no extension
    // the eight vanilla animal .ymt names: the game owns them and the replacing call crashes
    DENY("a_c_shepherd.ymt"); DENY("a_c_chop.ymt"); DENY("a_c_panther.ymt");
    DENY("A_C_Shepherd.YMT");                // case must not get past it
    // any other .ymt name is fair game, which is what a clothing pack ships
    ALLOW("mp_m_freemode_01_mypack.ymt"); ALLOW("mp_creaturemetadata_mypack.ymt");
    ALLOW("shepherd.ymt"); ALLOW("gameconfig.ymt");
    DENY("vehicles.yft");
    DENY("head_000_r.ydd");                  // loose drawable with no collection
    DENY("mp_m_freemode_01/a_c_pug.yft");    // .yft has no meaning inside a collection
    DENY("a_c_shepherd/a_c_shepherd.ymt");
    DENY(".ytd"); DENY("ytd"); DENY("");

    // server peds: the folder name is the author's choice, so the FILE name is what decides
    ALLOW("caninesd/head_000_r.ydd");   ALLOW("caninesd/head_diff_000_a_whi.ytd");
    ALLOW("canine/head_000_r.ydd");     ALLOW("caninefd/uppr_001_u.ydd");
    ALLOW("blackcat/head_diff_000_a_uni.ytd");
    ALLOW("anything/p_head_000.ydd");   ALLOW("anything/p_lwrist_diff_001_a.ytd");
    DENY("caninesd/head_000_r.yft");    // still only .ydd and .ytd inside a folder
    DENY("caninesd/malinois.ydd");      // not a ped part name
    DENY("caninesd/adder.ytd");
    DENY("myfolder/prop_bench_01a.ydd");
    DENY("onx/onx_sandy_01.ytd");
    DENY("x64/heada_000_r.ydd");        // prefix must be a whole slot name
    DENY("x64/head_abc_r.ydd");         // and the number must be a number

    printf(fails ? "%d FAILURE(S)\n" : "gate: all cases pass\n", fails);
    return fails ? 1 : 0;
}
