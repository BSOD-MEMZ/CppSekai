/* cpu_features.c - "this PC can run which build?" self check.
 *
 * Why: build.sh used to let zig pick the build machine's own CPU (native), so
 * the 2026-09-13 ~ 09-25 releases contained AVX-VNNI (VEX-encoded, Intel 12th
 * gen / Alder Lake 2021+, AMD Zen 5+ only). On any older CPU the first such
 * instruction raises 0xC000001D and the process dies silently. This tool says,
 * in plain words, whether a machine is one of those machines - so a crash can
 * be cross-checked on hardware that is not the reporter's.
 *
 * It must itself build for the x86-64 baseline, or it would die on exactly the
 * machines it is meant to diagnose:
 *     zig cc -std=c11 -O2 -mcpu=baseline -o build/cpuinfo.exe \
 *         .workbuddy/tools/cpu_features.c
 *
 * Usage:  cpuinfo.exe [optional-exe-to-scan ...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cpuid(unsigned leaf, unsigned sub, unsigned out[4])
{
    __asm__ __volatile__("cpuid"
        : "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
        : "a"(leaf), "c"(sub));
}

static unsigned long long xgetbv0(void)
{
    unsigned lo = 0;
    unsigned hi = 0;
    __asm__ __volatile__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((unsigned long long)hi << 32) | lo;
}

static int bit(unsigned value, int index) { return (value >> index) & 1u; }

/* ---- CPUID ---------------------------------------------------------------- */
static char gBrand[64];
static int gSse2, gSse42, gAvx, gFma, gAvx2, gBmi2, gAvx512f, gAvx512vnni, gAvxvnni;
static int gOsxsave, gXcr0;
static unsigned gFamily, gModel, gStepping;

static void detectCpu(void)
{
    unsigned r[4];
    cpuid(0, 0, r);
    const unsigned maxBasic = r[0];
    const unsigned maxExt = (cpuid(0x80000000u, 0, r), r[0]);

    cpuid(1, 0, r);
    const unsigned f1c = r[2];
    const unsigned f1d = r[3];
    gStepping = r[0] & 0xFu;
    gModel = (r[0] >> 4) & 0xFu;
    gFamily = (r[0] >> 8) & 0xFu;
    if (gFamily == 0xFu) gFamily += (r[0] >> 20) & 0xFFu;      /* extended family */
    if (gFamily == 6 || gFamily == 0xF) gModel += ((r[0] >> 16) & 0xFu) << 4;

    gSse2 = bit(f1d, 26);
    gSse42 = bit(f1c, 20);
    gAvx = bit(f1c, 28);
    gFma = bit(f1c, 12);
    gOsxsave = bit(f1c, 27);

    unsigned f7a[4] = {0, 0, 0, 0};
    unsigned f7c[4] = {0, 0, 0, 0};
    if (maxBasic >= 7) {
        cpuid(7, 0, f7a);
        gAvx2 = bit(f7a[1], 5);
        gBmi2 = bit(f7a[1], 8);
        gAvx512f = bit(f7a[1], 16);
        gAvx512vnni = bit(f7a[2], 11);
        cpuid(7, 1, f7c);
        gAvxvnni = bit(f7c[0], 4);
    }
    if (gOsxsave) {
        const unsigned long long xcr0 = xgetbv0();
        gXcr0 = (int)xcr0;
    }

    gBrand[0] = '\0';
    if (maxExt >= 0x80000004u) {
        char* p = gBrand;
        for (unsigned leaf = 0x80000002u; leaf <= 0x80000004u; ++leaf) {
            cpuid(leaf, 0, r);
            memcpy(p, r, 16);
            p += 16;
        }
        *p = '\0';
        /* trim leading spaces */
        char* s = gBrand;
        while (*s == ' ') ++s;
        if (s != gBrand) memmove(gBrand, s, strlen(s) + 1);
    }
    if (gBrand[0] == '\0') snprintf(gBrand, sizeof(gBrand), "(unknown)");
}

/* ---- "can this machine run that build?" ---------------------------------- */
static void printVerdict(const char* label, int ok, const char* needs)
{
    printf("  %-42s %s\n", label, ok ? "能跑" : "跑不了");
    printf("      %s需要：%s\n", ok ? "" : "  -> ", needs);
}

/* ---- optional: scan an exe for VEX / AVX-VNNI byte patterns ---------------- */
static unsigned long long gVex;
static unsigned long long gVnni;
static unsigned long long gCode;

static void scanExe(const char* path)
{
    /* per-file accumulators: a stale value silently reports the wrong exe, and
       the wrong number still looks plausible (09-24 + 09-25 summed to exactly
       6015488, which is how I wasted ten minutes on a phantom section size).
       NB: do not put this comment after the first assignment - it swallowed the
       other two once already. */
    gVex = 0;
    gVnni = 0;
    gCode = 0;
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        printf("\n[%s] 打不开\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* file = (unsigned char*)malloc((size_t)size);
    if (file == NULL) {
        fclose(f);
        printf("\n[%s] 内存不足\n", path);
        return;
    }
    if (fread(file, 1, (size_t)size, f) != (size_t)size) {
        printf("\n[%s] 读失败\n", path);
        free(file);
        fclose(f);
        return;
    }
    fclose(f);

    if (size < 0x40 || file[0] != 'M' || file[1] != 'Z') {
        printf("\n[%s] 不是 PE 文件\n", path);
        free(file);
        return;
    }
    const unsigned pe = *(unsigned*)(file + 0x3C);
    const unsigned nsec = *(unsigned short*)(file + pe + 6);
    const unsigned optSize = *(unsigned short*)(file + pe + 20);
    const unsigned secBase = pe + 24 + optSize;
    for (unsigned i = 0; i < nsec; ++i) {
        const unsigned char* s = file + secBase + i * 40u;
        const unsigned chars = *(unsigned*)(s + 36);
        if ((chars & 0x20000000u) == 0) continue; /* not executable */
        const unsigned rawSize = *(unsigned*)(s + 16);
        const unsigned rawPtr = *(unsigned*)(s + 20);
        if (rawPtr + rawSize > (unsigned)size) continue;
        const unsigned char* code = file + rawPtr;
        for (unsigned k = 0; k + 4 < rawSize; ++k) {
            if (code[k] == 0xC4 || code[k] == 0xC5) ++gVex;
            /* C4 P0 P1 op : VEX3, map 0F38, 66 prefix, VNNI dot-product opcode */
            if (code[k] == 0xC4 && (code[k + 1] & 0x1F) == 2 && (code[k + 2] & 3) == 1
                && (code[k + 3] == 0x52 || code[k + 3] == 0xB4 || code[k + 3] == 0xB5)) {
                ++gVnni;
            }
        }
        gCode += rawSize;
    }
    free(file);

    const double ratio = gCode ? (double)gVex / (double)gCode : 0.0;
    printf("\n[%s]\n", path);
    printf("  可执行节 %llu 字节，VEX 字节 %llu（%.2f%%），AVX-VNNI 指令 %llu 条\n",
        gCode, gVex, ratio * 100.0, gVnni);
    if (gVnni > 5) {
        fputs("  -> 含 AVX-VNNI（按 12 代以后的 CPU 编译）：", stdout);
        fputs(gAvxvnni ? "本机能跑。\n"
                       : "**本机跑不了 —— 会在启动画面后 0xC000001D 静默崩溃。**\n", stdout);
    } else if (ratio > 0.01) {
        fputs("  -> 含大量 AVX2（按 native CPU 编译）：", stdout);
        fputs((gAvx2 && gFma && gBmi2) ? "本机能跑。\n"
                                       : "**本机跑不了 —— 缺 AVX2/FMA/BMI2。**\n", stdout);
    } else {
        fputs("  -> x86-64 基线版本（-mcpu=baseline），本机能跑。\n", stdout);
    }
}

int main(int argc, char** argv)
{
    detectCpu();
    const int avxUsable = gOsxsave && (gXcr0 & 0x6) == 0x6;   /* OS saves XMM+YMM */

    printf("==== CppSekai CPU self check ====\n");
    printf("CPU            : %s\n", gBrand);
    printf("Family/Model   : Family %u Model %u Stepping %u\n", gFamily, gModel, gStepping);
    printf("系统启用 AVX   : %s (XCR0=0x%X)\n", avxUsable ? "是" : "否", gXcr0);
    printf("SSE2 / SSE4.2  : %s / %s\n", gSse2 ? "有" : "无", gSse42 ? "有" : "无");
    printf("AVX2 / FMA     : %s / %s\n", gAvx2 ? "有" : "无", gFma ? "有" : "无");
    printf("BMI2           : %s\n", gBmi2 ? "有" : "无");
    printf("AVX512-VNNI    : %s   (10/11 代、Zen4 有，但不是我们用的那种)\n", gAvx512vnni ? "有" : "无");
    printf("AVX-VNNI       : %s   <== 2026-09-25 之前的 exe 需要它\n", gAvxvnni ? "有" : "无");

    printf("\n---- 结论 ----\n");
    printVerdict("旧包（09-13 ~ 09-25 的 release）",
        gAvx2 && gFma && gBmi2 && gAvxvnni && avxUsable,
        "AVX2 + FMA + BMI2 + AVX-VNNI（Intel 12 代 2021 / AMD Zen5 2024 起）");
    printVerdict("新包（2026-09-25 修复版）",
        gSse2,
        "x86-64 基线（SSE2，2003 年后任何 64 位 CPU）");
    if (!gAvxvnni) {
        printf("\n  ** 本机没有 AVX-VNNI：和报错用户是同一类机器，可以在这台机器上复现那个崩溃。\n");
    } else {
        printf("\n  本机支持 AVX-VNNI，复现不了那个崩溃（旧包在本机也不会崩）。\n");
    }

    for (int i = 1; i < argc; ++i) scanExe(argv[i]);
    return 0;
}
