#include "streaming/gate.h"
#include "core/utils.h"
#include "core/logger.h"
#include <cctype>

const char* classifyCollection(const std::string& coll)
{
    std::string c = lower(coll);
    if (c == "mp_m_freemode_01") return "Freemode Male";
    if (c.rfind("mp_m_freemode_01", 0) == 0) return "Freemode Male DLC";
    if (c == "mp_f_freemode_01") return "Freemode Female";
    if (c.rfind("mp_f_freemode_01", 0) == 0) return "Freemode Female DLC";
    if (c.rfind("a_c_", 0) == 0) return "Animal Ped";
    // must come after the freemode tests: mp_m_freemode_01 also starts with the blocked mp_m_
    if (isBlockedCollection(c)) return "Story/Ambient Ped";
    return "Custom Server Ped";
}

// SAFETY: only human freemode-ped collections may be touched. Everything else — animal peds
// (canine…), story/ambient peds (a_*, ig_*, cs_*), vehicles, weapons, props, maps, scripts — is
// left strictly alone. This is what stops a "dog head replaced by a human head" mistake.
// SAFETY: only ped collections may be touched, and only two families of them.
//   mp_?_freemode_01*  the player's own ped
//   a_c_*              animal peds. The eight dogs and cats that are "streamed peds" (chop,
//                      husky, mtlion, panther, retriever, rottweiler, sharktiger, shepherd) are
//                      built exactly like a freemode ped: a collection folder of head_/uppr_/
//                      lowr_/accs_/teef_ drawables and their txds. Verified against
//                      x64e.rpf, models/cdimages/streamedpeds_a_c.rpf.
// Everything else (story and ambient peds, vehicles, weapons, props, maps, scripts) is left
// strictly alone.
bool isPedCollection(const std::string& coll)
{
    std::string c = lower(coll);
    return c.rfind("mp_m_freemode_01", 0) == 0 || c.rfind("mp_f_freemode_01", 0) == 0
        || c.rfind("a_c_", 0) == 0;
}

// Servers ship their own peds and name the collection whatever the author felt like: canine,
// caninesd, caninefd, blackcat, browncat on one real server, none of which any prefix rule could
// guess. So the folder name is no longer what decides. The FILE name is, and it is the better
// test anyway: ped parts are named from a closed vocabulary that Rockstar has used since launch,
// twelve component slots and eleven prop anchors, always followed by a three digit number. A
// vehicle txd, a prop drawable, a map file, none of them look like this, so they still cannot get
// in no matter what folder somebody puts them in. That is the guarantee the folder whitelist was
// really buying, kept, while server peds stop being collateral damage.
// The promise the README makes is that story and cutscene characters are never touched, and that
// survives the change above: these are the prefixes every collection-based non-freemode ped in
// b3751 uses (147 cs_, 69 ig_, plus the stragglers), read out of docs/ped_collections.tsv. They
// are refused by name whatever their files are called. Ambient peds (s_m_*, a_f_* and friends)
// need no entry: the scan found none of them are collection-based, so there is no slot to take.
bool isBlockedCollection(const std::string& coll)
{
    static const char* kBlocked[] = {
        "cs_", "csb_", "ig_", "hc_", "player_", "p_michael", "p_franklin", "mp_headtargets",
        "mp_m_", "mp_f_", "mp_s_",          // any mp_ ped that is not freemode_01, checked after it
        "s_m_", "s_f_", "a_m_", "a_f_", "u_m_", "u_f_", "g_m_", "g_f_",
    };
    std::string c = lower(coll);
    for (const char* b : kBlocked) if (c.rfind(b, 0) == 0) return true;
    return false;
}

bool isPedComponentFile(const std::string& file)
{
    static const char* kComp[] = { "head", "berd", "hair", "uppr", "lowr", "hand",
                                   "feet", "teef", "accs", "task", "decl", "jbib" };
    static const char* kAnchor[] = { "head", "eyes", "ears", "mouth", "lhand", "rhand",
                                     "lwrist", "rwrist", "hip", "lfoot", "rfoot" };
    std::string f = lower(file);
    size_t at = std::string::npos;
    if (f.rfind("p_", 0) == 0) {                       // a prop: p_<anchor>_...
        for (const char* a : kAnchor) {
            std::string pre = std::string("p_") + a + "_";
            if (f.rfind(pre, 0) == 0) { at = pre.size(); break; }
        }
    } else {
        for (const char* c : kComp) {
            std::string pre = std::string(c) + "_";
            if (f.rfind(pre, 0) == 0) { at = pre.size(); break; }
        }
    }
    if (at == std::string::npos) return false;
    if (f.compare(at, 5, "diff_") == 0) at += 5;       // <comp>_diff_<nnn>_<letter>_<race>.ytd
    return f.size() >= at + 3 && isdigit((unsigned char)f[at])
        && isdigit((unsigned char)f[at+1]) && isdigit((unsigned char)f[at+2]);
}

std::string collectionOf(const std::string& key)   // "collection/file" -> "collection"
{
    size_t s = key.find('/');
    return (s == std::string::npos) ? key : key.substr(0, s);
}

// Every type the plugin will even consider. The policy gate is isAllowedKey; this exists so the
// folder walk can stop early on readmes, archives and loose textures.
bool isOverrideExt(const std::string& ln)
{
    return hasExt(ln, ".ytd") || hasExt(ln, ".ydd") || hasExt(ln, ".yft")
        || hasExt(ln, ".ymt") || hasExt(ln, ".ycd") || hasExt(ln, ".ydr");
}

// Animal peds are the exception. Most of them (pug, poodle, westy, cat, coyote, deer...) are not
// collection-based at all: model, fragment and variation metadata are bare a_c_<name>.ydd/.yft/
// .ymt files. The eight collection-based ones still keep their .yft and .ymt at the root beside
// the folder, and the .ymt (a CPedVariationInfo) is what makes any drawable or texture a mod ADDS
// on top of vanilla selectable at all. So those three types are allowed at the root, for a_c_
// names only. One player's .ymt took the game down at registration on 0.8.0; that is now the
// startup crash saver's job to contain rather than a reason to refuse the whole type.
// A .ymt registers cleanly when its name is one the game has never seen: a connected server does
// exactly that a dozen times a session for its own clothing packs, through this same call, with
// no trouble at all. Hand the call a .ymt name the game ALREADY owns and it faults three frames
// in and takes the game with it (ink-island-iowa, GTA5_b3751.exe+16765E0). The .yft beside it
// survives the same treatment because a fragment and a metadata file land in different stores,
// and only the metadata store dies this way.
//
// Every animal ships its own .ymt, checked against b3751 with CodeWalker (docs/ped_collections.tsv),
// so a user's animal .ymt always collides and can never win. Timing does not rescue it either:
// 0.8.4 handed them over a minute into the session instead and it died just the same. These eight
// names are refused. Every other .ymt name is allowed, which is what a clothing pack needs.
bool isVanillaAnimalYmt(const std::string& key)
{
    static const char* kVanilla[] = {
        "a_c_chop.ymt", "a_c_husky.ymt", "a_c_mtlion.ymt", "a_c_panther.ymt",
        "a_c_retriever.ymt", "a_c_rottweiler.ymt", "a_c_sharktiger.ymt", "a_c_shepherd.ymt",
    };
    std::string k = lower(key);
    for (const char* v : kVanilla) if (k == v) return true;
    return false;
}

// A key with a slash is a component slot ("collection/file") and its collection must be one of
// the two families above.
//
// A key without one is a bare-name asset. Overlay txds (skin, tattoos, facepaint, hair, beards,
// shirt decals...) live loose in the overlay rpfs and stream by filename alone, and a scan of
// every *overlay*.rpf found ~100 unrelated naming families plus arbitrarily-named server packs,
// so no name whitelist can work there. The gate is type + exactness instead: .ytd only, matching
// the dictionary whose exact name it bears.
bool isAllowedKey(const std::string& key)
{
    size_t s = key.find('/');
    if (s != std::string::npos) {
        if (!hasExt(key, ".ydd") && !hasExt(key, ".ytd")) return false;
        std::string coll = key.substr(0, s);
        // the two known families keep working exactly as they did, odd filenames included
        if (isPedCollection(coll)) return true;
        if (isBlockedCollection(coll)) return false;
        return isPedComponentFile(key.substr(s + 1));
    }
    if (hasExt(key, ".ytd")) return true;
    // .ycd: a clip dictionary, the file an animation lives in. Allowed at the root by
    // exact name, same rule as a .ytd. Clips inside are looked up by joaat hash, not by
    // the label CodeWalker prints, so a replacement dictionary only has to carry the
    // right hashes. Worth having because a server streams its own anim dictionaries
    // through this very call, and the re-assert loop is the only thing on the client
    // that can take a slot back off one.
    if (hasExt(key, ".ycd")) return true;
    // .ydr / .yft: a drawable or a fragment. Weapons (w_*), world props (prop_*, p_*, v_*, and
    // whatever a server calls its own) and vehicles all live here, and all of them stream by
    // exact file name, so the same rule as a .ytd applies: any name, matched exactly. 0.8.8 took
    // only w_* here; a pack of props landed in a folder and every file was refused (see the
    // README's prop section). Nothing is cross-wired by a bare exact name, because the name IS
    // the slot.
    if (hasExt(key, ".ydr") || hasExt(key, ".yft")) return true;
    if (hasExt(key, ".ymt")) return !isVanillaAnimalYmt(key);
    // .ydd at the root: only the animal models. A ped drawable belongs to a collection.
    return lower(key).rfind("a_c_", 0) == 0 && hasExt(key, ".ydd");
}

// Types we walk past on purpose. Announcing beats a silent skip: a .meta is a file mods really do
// ship, so a user who sees nothing assumes the plugin broke.
bool isIgnoredType(const std::string& ln, const std::string& rel, bool announce)
{
    if (rel.find('/') == std::string::npos && isVanillaAnimalYmt(ln)) {
        if (announce) LOG_INFO(LogCategory::Scan, "IGNORED %s - vanilla animal .ymt collides with game metadata; other mod files still load", rel.c_str());
        return true;
    }
    if (ln.size() > 5 && ln.compare(ln.size()-5, 5, ".meta") == 0) {
        if (announce) LOG_INFO(LogCategory::Scan, "IGNORED %s - .meta files hold shop/menu data, not looks; see README", rel.c_str());
        return true;
    }
    return false;
}

// A folder whose name starts with "disabled" is skipped whole, files and subfolders alike, so a
// pack is switched off by renaming it (Pack1 -> disabledPack1) and back on by renaming it again.
// No real collection is named that way, so nothing legitimate is lost to the rule.
bool isDisabledFolder(const std::string& name)
{
    return lower(name).rfind("disabled", 0) == 0;
}

// One rule for both the startup scan and live reload, so a nested folder means the same thing
// on both paths. Flat layouts produce exactly the keys they always did.
std::string slotKeyFor(const std::string& rel, const char** why)
{
    *why = nullptr;
    size_t cut = rel.rfind('/');
    std::string file = cut == std::string::npos ? rel : rel.substr(cut + 1);
    std::string parent;
    if (cut != std::string::npos) {
        size_t cut2 = rel.rfind('/', cut - 1);
        parent = cut2 == std::string::npos ? rel.substr(0, cut) : rel.substr(cut2 + 1, cut - cut2 - 1);
    }
    bool slotted = hasExt(file, ".ydd") || hasExt(file, ".ytd");
    if (slotted && !parent.empty()) {
        std::string k = parent + "/" + file;
        if (isAllowedKey(k)) return k;
        // a ped part in a folder that is not a ped collection: a story ped folder, or a clothing
        // file dropped one level too high. It has no bare-name meaning, so say what is wrong.
        if (isBlockedCollection(parent) || isPedComponentFile(file)) {
            *why = "folder contents must use GTA ped part naming (e.g. head_000_r.ydd, uppr_diff_001_a_uni.ytd), inside a folder named after the collection";
            return "";
        }
        // not a ped part: the folder is just a folder, treat the file as loose (tattoos/ etc.)
    }
    if (isAllowedKey(file)) return file;
    if (hasExt(file, ".ymt") && isVanillaAnimalYmt(file))
        *why = "vanilla animal .ymt collides with game metadata; other mod files still load";
    else if (hasExt(file, ".ydd"))
        *why = "a .ydd belongs in a folder named after its collection (see COLLECTIONS.md); only animal models (a_c_*) can sit loose";
    else
        *why = "not a file type the plugin loads";
    return "";
}
