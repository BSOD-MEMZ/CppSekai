"""One-shot patch: teach chartdl to fetch 2D MV (.usm) from the exmeaning mirror.

The MV files are 10~40 MB each and only 186 songs have them, so the checkbox is
off by default and a 404 is a normal answer (not a downloader failure).
"""
import io

PATH = r'D:/Dev/CppSekai/downloader/chartdl.cpp'


def sub(s, a, b, count=1):
    assert s.count(a) >= 1, 'anchor missing: ' + a[:80]
    return s.replace(a, b, count)


def main():
    s = io.open(PATH, encoding='utf-8').read()

    # ---- 1. url + path ---------------------------------------------------
    s = sub(s, '''std::string jacketUrl(const Song& song)''',
            '''// 2D MV（.usm）。**不在 unipjsk**：那边 404，只有 exmeaning 镜像有
// （见 MV2D-SCORE.md 1.7）。总共只有 186 首带 2D MV，别的歌下下来是 404 ——
// 那是正常的，不代表下载器坏了。
std::string mvUrl(const Song& song)
{
    return "https://storage.exmeaning.com/sekai-jp-assets/live/2dmode/original_mv/"
        + id4(song.id) + "/" + id4(song.id) + ".usm";
}

std::string jacketUrl(const Song& song)''')

    s = sub(s, '''fs::path jacketPath(const fs::path& dir, const Song& song)
{
    return dir / (id4(song.id) + ".png");
}''',
            '''fs::path jacketPath(const fs::path& dir, const Song& song)
{
    return dir / (id4(song.id) + ".png");
}

// MV 统一放 <charts>/mv/<id4>.usm，演奏那边就按这个路径找。
fs::path mvPath(const fs::path& dir, const Song& song)
{
    return dir / "mv" / (id4(song.id) + ".usm");
}''')

    # ---- 2. request flag -------------------------------------------------
    s = sub(s, '''    bool jacket = true;
    bool sidecar = true;
};''',
            '''    bool jacket = true;
    bool sidecar = true;
    // 2D MV：默认关。单首 10~40 MB，而且只有 186 首有。
    bool mv = false;
};''')

    # ---- 3. queue the job ------------------------------------------------
    s = sub(s, '''    if (request.jacket) {
        add(jacketUrl(song), jacketPath(dir, song));
    }''',
            '''    if (request.jacket) {
        add(jacketUrl(song), jacketPath(dir, song));
    }
    if (request.mv) {
        fs::create_directories(dir / "mv", ec);
        add(mvUrl(song), mvPath(dir, song));
    }''')

    # ---- 4. control id + handle -----------------------------------------
    s = sub(s, '''    constexpr int kIdSettings = 1015;''',
            '''    constexpr int kIdSettings = 1015;
    constexpr int kIdMv = 1021;''')

    s = sub(s, '''    HWND gJacketCheck = nullptr;''',
            '''    HWND gJacketCheck = nullptr;
    HWND gMvCheck = nullptr;''')

    # ---- 5. create the checkbox -----------------------------------------
    s = sub(s, '''                SendMessageW(gJacketCheck, BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(gSidecarCheck, BM_SETCHECK, BST_CHECKED, 0);''',
            '''                gMvCheck = createInBox(L"BUTTON", L"2D MV (.usm)", BS_AUTOCHECKBOX, kIdMv);
                SendMessageW(gJacketCheck, BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(gSidecarCheck, BM_SETCHECK, BST_CHECKED, 0);
                // 默认不勾：单首 10~40 MB，而且很多歌根本没有 2D MV。
                SendMessageW(gMvCheck, BM_SETCHECK, BST_UNCHECKED, 0);''')

    # ---- 6. one more pinned row slot ------------------------------------
    s = sub(s, '''        const int jacketY = sidecarY - rowHeight;''',
            '''        const int jacketY = sidecarY - rowHeight;
        const int mvY = jacketY - rowHeight;''')

    # ---- 7. no-song branch ----------------------------------------------
    s = sub(s, '''            gDetailWantedHeight = bodyTop + rowHeight + footerH;''',
            '''            gDetailWantedHeight = bodyTop + rowHeight * 2 + footerH;''')

    s = sub(s, '''            SetWindowTextW(gSidecarCheck, L"元数据 (sidecar json)");
            EnableWindow(gSidecarCheck, FALSE);
            placeRow(gSidecarCheck, sidecarY);
            return;''',
            '''            SetWindowTextW(gSidecarCheck, L"元数据 (sidecar json)");
            EnableWindow(gSidecarCheck, FALSE);
            placeRow(gSidecarCheck, sidecarY);
            SetWindowTextW(gMvCheck, L"2D MV (.usm)");
            EnableWindow(gMvCheck, FALSE);
            placeRow(gMvCheck, mvY);
            return;''')

    # ---- 8. song branch: height + placement -----------------------------
    s = sub(s, '''        gDetailWantedHeight += bodyTop;''',
            '''        gDetailWantedHeight += bodyTop;
        // 多出来的一行 pinned 是 2D MV（曲绘 / 元数据之上）。
        gDetailWantedHeight += rowHeight;''')

    s = sub(s, '''        placeRow(gSidecarCheck, sidecarY);
        // (The wanted height was already published above the footer, where the
        // unscrolled body bottom is still known.)''',
            '''        placeRow(gSidecarCheck, sidecarY);

        SetWindowTextW(gMvCheck, L"2D MV (.usm)");
        EnableWindow(gMvCheck, TRUE);
        placeRow(gMvCheck, mvY);
        // (The wanted height was already published above the footer, where the
        // unscrolled body bottom is still known.)''')

    # ---- 9. read it in ---------------------------------------------------
    s = sub(s, '''                    request.jacket = SendMessageW(gJacketCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;''',
            '''                    request.jacket = SendMessageW(gJacketCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    request.mv = SendMessageW(gMvCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;''')

    s = sub(s, '''                    if (!anyDiff && request.vocalIndexes.empty() && !request.jacket && !request.sidecar) {''',
            '''                    if (!anyDiff && request.vocalIndexes.empty() && !request.jacket && !request.sidecar
                        && !request.mv) {''')

    io.open(PATH, 'w', encoding='utf-8').write(s)
    print('patched chartdl')


if __name__ == '__main__':
    main()
