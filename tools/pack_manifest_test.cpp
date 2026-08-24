#include "../dllmain.cpp"

static int failures = 0;

static void want(bool got, bool expected, const char* name)
{
    if (got != expected) {
        printf("FAIL %-42s got %d want %d\n", name, (int)got, (int)expected);
        ++failures;
    }
}

static bool writeFile(const std::string& path, const void* data, size_t size)
{
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "wb") || !file) return false;
    bool ok = fwrite(data, 1, size, file) == size;
    fclose(file);
    return ok;
}

static bool writeText(const std::string& path, const std::string& text)
{
    return writeFile(path, text.data(), text.size());
}

static void clearOverrides()
{
    for (auto& ov : g_ovs) {
        if (ov.gfile && ov.gfile != ov.file) free((void*)ov.gfile);
        free((void*)ov.slot);
        free((void*)ov.file);
    }
    g_ovs.clear();
    g_bySlot.clear();
    g_costVirt = g_costPhys = 0;
    g_costBig.clear();
}

int main()
{
    char temp[MAX_PATH], root[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, temp) || !GetTempFileNameA(temp, "txo", 0, root)) return 2;
    DeleteFileA(root);
    if (!CreateDirectoryA(root, nullptr)) return 2;

    std::string base = root;
    std::string coll = base + "\\mp_m_freemode_01";
    if (!CreateDirectoryA(coll.c_str(), nullptr)) return 2;
    std::string ydd = coll + "\\head_000_r.ydd";
    std::string ytd = coll + "\\head_diff_000_a_whi.ytd";
    std::string extra = coll + "\\lowr_000_r.ydd";
    const unsigned char bytes4[] = { 1, 2, 3, 4 };
    const unsigned char bytes3[] = { 5, 6, 7 };
    if (!writeFile(ydd, bytes4, sizeof bytes4) || !writeFile(ytd, bytes4, sizeof bytes4) ||
        !writeFile(extra, bytes3, sizeof bytes3)) return 2;

    std::vector<Cand> two = {
        { "mp_m_freemode_01/head_000_r.ydd", ydd, Cost() },
        { "mp_m_freemode_01/head_diff_000_a_whi.ytd", ytd, Cost() }
    };
    std::vector<Cand> three = two;
    three.push_back({ "mp_m_freemode_01/lowr_000_r.ydd", extra, Cost() });
    std::string manifest = base + "\\_gtaw_pack_manifest.tsv";
    const std::string header = "row_key\ttarget_relative_path\tsize\n";
    const std::string rows =
        "head\tmp_m_freemode_01/head_000_r.ydd\t4\n"
        "head\tmp_m_freemode_01/head_diff_000_a_whi.ytd\t4\n";

    std::unordered_map<std::string, PackExpected> expected;
    size_t groups = 0; std::string problem;
    writeText(manifest, header + rows);
    want(readPackManifest(manifest.c_str(), two, expected, groups, problem), true, "plain complete manifest");
    want(groups == 1 && expected.size() == 2, true, "group and file counts");

    const std::string quoted =
        "\"row_key\"\t\"target_relative_path\"\t\"size\"\t\"sha256\"\t\"virtual_bytes\"\t\"physical_bytes\"\r\n"
        "\"head\"\t\"mp_m_freemode_01/head_000_r.ydd\"\t\"4\"\t\"00\"\t\"0\"\t\"0\"\r\n"
        "\"head\"\t\"mp_m_freemode_01/head_diff_000_a_whi.ytd\"\t\"4\"\t\"00\"\t\"0\"\t\"0\"\r\n";
    writeText(manifest, quoted);
    want(readPackManifest(manifest.c_str(), two, expected, groups, problem), true, "quoted PowerShell TSV");
    want(expected.begin()->second.hasCost, true, "optional RSC costs parsed");

    writeText(manifest, header + rows);
    want(readPackManifest(manifest.c_str(), three, expected, groups, problem), false, "unmanifested resource");
    writeText(manifest, header + rows + "lowr\tmp_m_freemode_01/lowr_000_r.ydd\t3\n");
    want(readPackManifest(manifest.c_str(), two, expected, groups, problem), false, "missing resource");
    writeText(manifest, header +
        "head\tmp_m_freemode_01/head_000_r.ydd\t5\n"
        "head\tmp_m_freemode_01/head_diff_000_a_whi.ytd\t4\n");
    want(readPackManifest(manifest.c_str(), two, expected, groups, problem), false, "size mismatch");
    writeText(manifest, header + rows + "head\tmp_m_freemode_01/head_000_r.ydd\t4\n");
    want(readPackManifest(manifest.c_str(), two, expected, groups, problem), false, "duplicate path");
    writeText(manifest, header +
        "head\t../head_000_r.ydd\t4\n"
        "head\tmp_m_freemode_01/head_diff_000_a_whi.ytd\t4\n");
    want(readPackManifest(manifest.c_str(), two, expected, groups, problem), false, "traversal path");
    writeText(manifest, "row_key\ttarget_relative_path\tsize\tvirtual_bytes\n" +
        std::string("head\tmp_m_freemode_01/head_000_r.ydd\t4\t0\n"));
    want(readPackManifest(manifest.c_str(), two, expected, groups, problem), false, "half RSC cost schema");
    writeText(manifest, "row_key\trow_key\ttarget_relative_path\tsize\n");
    want(readPackManifest(manifest.c_str(), two, expected, groups, problem), false, "duplicate header");
    writeText(manifest, header + "\"head\tmp_m_freemode_01/head_000_r.ydd\t4\n");
    want(readPackManifest(manifest.c_str(), two, expected, groups, problem), false, "unterminated quote");

    // Inventory can pass while a same-sized non-RSC file is present. The second phase must still
    // refuse the whole pack before publishing any override to the registration loop.
    writeText(manifest, header + rows);
    want(readPackManifest(manifest.c_str(), two, expected, groups, problem), true, "invalid-RSC inventory phase");
    InitializeCriticalSection(&g_cs);
    g_packManifestActive = g_packManifestReady = true;
    g_packExpected = expected;
    g_cands = two;
    scanFinish();
    want(g_packManifestReady, false, "invalid RSC refuses managed pack");
    want(g_ovs.empty(), true, "invalid RSC publishes no overrides");
    clearOverrides();
    DeleteCriticalSection(&g_cs);

    DeleteFileA(manifest.c_str());
    DeleteFileA(ydd.c_str());
    DeleteFileA(ytd.c_str());
    DeleteFileA(extra.c_str());
    RemoveDirectoryA(coll.c_str());
    RemoveDirectoryA(base.c_str());
    printf(failures ? "%d FAILURE(S)\n" : "managed-pack manifest: all cases pass\n", failures);
    return failures ? 1 : 0;
}
