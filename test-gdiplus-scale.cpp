// Probe: does the load -> halve -> scale pipeline actually produce pixels?
//
// Reports the mean colour and alpha of the result, so a blank (transparent) or
// white result can be told apart from a correct one. Run against a real image.
//
//   gdiplus-probe.exe <image path>
#include <windows.h>
#include <stdio.h>
#include <math.h>

typedef struct GpGraphics GpGraphics;
typedef struct GpBrush GpBrush;
typedef struct GpImage GpImage;
typedef struct GpBitmap GpBitmap;

typedef struct GdiplusStartupInput {
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;
typedef struct GdiplusStartupOutput { void *NotificationHook; void *NotificationUnhook; } GdiplusStartupOutput;

static const int kPixelFormat32bppPARGB = 0x000E200B;
static const int kInterpolationHighQualityBicubic = 7;
static const int kSmoothingAntiAlias = 4;

static int(WINAPI *pStartup)(ULONG_PTR *, const GdiplusStartupInput *, GdiplusStartupOutput *);
static void(WINAPI *pShutdown)(ULONG_PTR);
static int(WINAPI *pCreateBitmapFromScan0)(INT, INT, INT, INT, BYTE *, GpBitmap **);
static int(WINAPI *pLoadImageFromFile)(const WCHAR *, GpImage **);
static int(WINAPI *pGetImageWidth)(GpImage *, UINT *);
static int(WINAPI *pGetImageHeight)(GpImage *, UINT *);
static int(WINAPI *pGetImageGraphicsContext)(GpImage *, GpGraphics **);
static int(WINAPI *pDrawImageRect)(GpGraphics *, GpImage *, float, float, float, float);
static int(WINAPI *pSetInterpolationMode)(GpGraphics *, int);
static int(WINAPI *pSetSmoothingMode)(GpGraphics *, int);
static int(WINAPI *pBitmapGetPixel)(GpBitmap *, INT, INT, DWORD *);
static int(WINAPI *pDisposeImage)(GpImage *);

// Mirrors PlanDownscale from the mod.
static int PlanDownscale(int srcW, int srcH, int target, int *outW, int *outH, int cap,
                         int *stepCount) {
    int count = 0;
    int w = srcW, h = srcH;
    int guard = 0;
    while (guard++ < 12) {
        if (w <= target * 2 && h <= target * 2) break;
        int nw = w / 2, nh = h / 2;
        if (nw < target) {
            double f = (double)target / (double)nw;
            nw = target;
            nh = (int)((double)nh * f + 0.5);
        }
        if (nh < target) {
            double f = (double)target / (double)nh;
            nh = target;
            nw = (int)((double)nw * f + 0.5);
        }
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
        if (nw > w) nw = w;
        if (nh > h) nh = h;
        w = nw; h = nh;
        if (count < cap) { outW[count] = w; outH[count] = h; count++; }
    }
    *stepCount = count;
    float fit = (float)target / (float)(w > h ? w : h);
    int fw = (int)((float)w * fit + 0.5f);
    int fh = (int)((float)h * fit + 0.5f);
    if (fw < 1) fw = 1;
    if (fh < 1) fh = 1;
    if (count < cap) { outW[count] = fw; outH[count] = fh; count++; }
    return count;
}

static GpImage *Downscale(GpImage *source, int target, int strideMode) {
    UINT w = 0, h = 0;
    pGetImageWidth(source, &w);
    pGetImageHeight(source, &h);
    int ws[16], hs[16], steps = 0;
    int n = PlanDownscale((int)w, (int)h, target, ws, hs, 16, &steps);

    GpImage *current = source;
    BOOL owns = FALSE;
    for (int i = 0; i < n; i++) {
        int nw = ws[i], nh = hs[i];
        int stride = (strideMode == 0) ? 0 : nw * 4;
        GpBitmap *next = NULL;
        if (pCreateBitmapFromScan0(nw, nh, stride, kPixelFormat32bppPARGB, NULL, &next) != 0 || !next) {
            printf("    create %dx%d stride=%d FAILED\n", nw, nh, stride);
            break;
        }
        GpGraphics *g = NULL;
        BOOL ok = FALSE;
        if (pGetImageGraphicsContext((GpImage *)next, &g) == 0 && g) {
            pSetInterpolationMode(g, kInterpolationHighQualityBicubic);
            pSetSmoothingMode(g, kSmoothingAntiAlias);
            ok = pDrawImageRect(g, current, 0.0f, 0.0f, (float)nw, (float)nh) == 0;
        }
        if (!ok) { pDisposeImage((GpImage *)next); break; }
        if (owns && current) pDisposeImage(current);
        current = (GpImage *)next;
        owns = TRUE;
    }
    if (!owns) return NULL;
    return current;
}

static void Report(const char *label, GpBitmap *bmp, int w, int h) {
    double sr = 0, sg = 0, sb = 0, sa = 0;
    int n = 0, transparent = 0, white = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            DWORD px = 0;
            if (pBitmapGetPixel(bmp, x, y, &px) != 0) continue;
            int a = (px >> 24) & 0xFF;
            int r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, bl = px & 0xFF;
            sr += r; sg += g; sb += bl; sa += a; n++;
            if (a == 0) transparent++;
            if (a == 255 && r > 240 && g > 240 && bl > 240) white++;
        }
    }
    if (!n) { printf("  %-22s no pixels read\n", label); return; }
    printf("  %-22s mean RGBA = %5.1f %5.1f %5.1f %5.1f   transparent=%d/%d  opaque-white=%d/%d\n",
           label, sr / n, sg / n, sb / n, sa / n, transparent, n, white, n);
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: gdiplus-probe.exe <image>\n"); return 2; }

    HMODULE m = LoadLibraryW(L"gdiplus.dll");
    if (!m) { printf("gdiplus.dll load failed: %lu\n", GetLastError()); return 1; }
    // GdiplusStartup / GdiplusShutdown do NOT carry the Gdip prefix; everything
    // else does.
    *(FARPROC *)&pStartup = GetProcAddress(m, "GdiplusStartup");
    *(FARPROC *)&pShutdown = GetProcAddress(m, "GdiplusShutdown");
#define GET(x) *(FARPROC *)&p##x = GetProcAddress(m, "Gdip" #x)
    GET(CreateBitmapFromScan0); GET(LoadImageFromFile);
    GET(GetImageWidth); GET(GetImageHeight); GET(GetImageGraphicsContext);
    GET(DrawImageRect); GET(SetInterpolationMode); GET(SetSmoothingMode);
    GET(BitmapGetPixel); GET(DisposeImage);
#undef GET
    if (!pStartup) { printf("GdiplusStartup missing\n"); return 1; }

    ULONG_PTR token = 0;
    GdiplusStartupInput in = {};
    in.GdiplusVersion = 1;
    if (pStartup(&token, &in, NULL) != 0) { printf("GDI+ startup failed\n"); return 1; }

    WCHAR path[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH);

    GpImage *src = NULL;
    int rc = pLoadImageFromFile(path, &src);
    printf("load \"%s\" rc=%d\n", argv[1], rc);
    if (rc != 0 || !src) { pShutdown(token); return 1; }

    UINT sw = 0, sh = 0;
    pGetImageWidth(src, &sw);
    pGetImageHeight(src, &sh);
    printf("source %ux%u\n", sw, sh);
    Report("source", (GpBitmap *)src, (int)sw, (int)sh);

    const int target = 23;
    printf("\ntarget (plate diameter) = %d\n", target);

    printf("\n-- stride = 0 (what the mod used) --\n");
    GpImage *a = Downscale(src, target, 0);
    if (!a) {
        printf("  Downscale returned NULL  <-- nothing to draw\n");
    } else {
        UINT aw = 0, ah = 0;
        pGetImageWidth(a, &aw); pGetImageHeight(a, &ah);
        printf("  result %ux%u\n", aw, ah);
        Report("stride=0 result", (GpBitmap *)a, (int)aw, (int)ah);
    }

    printf("\n-- stride = width*4 (explicit) --\n");
    GpImage *b = Downscale(src, target, 1);
    if (!b) {
        printf("  Downscale returned NULL\n");
    } else {
        UINT bw = 0, bh = 0;
        pGetImageWidth(b, &bw); pGetImageHeight(b, &bh);
        printf("  result %ux%u\n", bw, bh);
        Report("stride=W*4 result", (GpBitmap *)b, (int)bw, (int)bh);
    }

    printf("\n-- single-step squash straight to %d (for contrast) --\n", target);
    {
        GpBitmap *one = NULL;
        if (pCreateBitmapFromScan0(target, target, 0, kPixelFormat32bppPARGB, NULL, &one) == 0 && one) {
            GpGraphics *g = NULL;
            if (pGetImageGraphicsContext((GpImage *)one, &g) == 0 && g) {
                pSetInterpolationMode(g, kInterpolationHighQualityBicubic);
                pDrawImageRect(g, src, 0.0f, 0.0f, (float)target, (float)target);
            }
            Report("one-step result", one, target, target);
        } else {
            printf("  create failed\n");
        }
    }

    if (a) pDisposeImage(a);
    if (b) pDisposeImage(b);
    pDisposeImage(src);
    pShutdown(token);
    return 0;
}
