// Checks the safety gate. The functions under test are lifted out of dllmain.cpp at build time
// by gate_test.sh, so this cannot drift from the shipping code.
#include <string>
#include <cstdio>
static std::string lower(std::string s){for(char&c:s)c=(char)tolower((unsigned char)c);return s;}
#include "gate_extracted.inc"

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
    ALLOW("a_c_shepherd.ymt");
    // the other collection-based animals
    ALLOW("a_c_chop/teef_000_u.ydd");
    ALLOW("a_c_husky/lowr_diff_000_a_whi.ytd");
    ALLOW("a_c_panther/uppr_000_r.ydd");
    // animals that are not collection-based: bare model, fragment and metadata
    ALLOW("a_c_pug.ydd"); ALLOW("a_c_pug.ytd"); ALLOW("a_c_pug.yft"); ALLOW("a_c_pug.ymt");
    ALLOW("a_c_westy.ydd"); ALLOW("a_c_cat_01.ytd"); ALLOW("a_c_rottweiler_02.ydd");
    // freemode, unchanged
    ALLOW("mp_m_freemode_01/teef_004_u.ydd");
    ALLOW("mp_f_freemode_01_mp_f_2023_02/uppr_012_r.ydd");
    ALLOW("mp_fm_skin_m_up_whi.ytd");
    ALLOW("mp_fm_faov_makeup_031.ytd");

    // still refused: everything that is not a ped
    DENY("adder/adder.ytd");                 // vehicle collection
    DENY("prop_bench_01a.ydd");              // prop model, no a_c_ name
    DENY("onx_sandy_01.ydr");                // map drawable
    DENY("s_m_y_cop_01/head_000_r.ydd");     // story ped collection
    DENY("player_zero/uppr_000_r.ydd");
    DENY("a_c_shepherd.rpf");                // not a streamable type
    DENY("a_c_shepherd");                    // no extension
    DENY("shepherd.ymt");                    // .ymt only for a_c_ names
    DENY("gameconfig.ymt");
    DENY("vehicles.yft");
    DENY("head_000_r.ydd");                  // loose drawable with no collection
    DENY("mp_m_freemode_01/a_c_pug.yft");    // .yft has no meaning inside a collection
    DENY("a_c_shepherd/a_c_shepherd.ymt");
    DENY(".ytd"); DENY("ytd"); DENY("");

    printf(fails ? "%d FAILURE(S)\n" : "gate: all cases pass\n", fails);
    return fails ? 1 : 0;
}
