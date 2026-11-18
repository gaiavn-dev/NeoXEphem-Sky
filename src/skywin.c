/* NeoXEphem Sky: native Win32 sky map built on XEphem's libastro.
 *
 * Shows the sky for any date (thousands of years into the past or future)
 * from any place on Earth. Pure Win32/GDI, no X11 or Motif.
 *
 * Mouse: drag to pan, wheel to zoom, hover for info, click to select.
 * Keys:  Z zenith view, N/E/S/W face a direction, +/- zoom,
 *        PgUp/PgDn step time, Space play/pause.
 */
#define WIN32_LEAN_AND_MEAN
#define FIXED WIN32_FIXED /* wingdi.h FIXED clashes with libastro enum */
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#undef FIXED

#include "skycore.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

/* ---------------------------------------------------------------- ids */
enum {
    ID_YEAR = 100, ID_MONTH, ID_DAY, ID_HOUR, ID_MIN, ID_TZ, ID_LMT, ID_NOW,
    ID_STEPUNIT, ID_BACK, ID_FWD, ID_PLAY, ID_SITE, ID_LAT, ID_LON, ID_APPLY,
    ID_LINES, ID_BOUNDS, ID_NAMES, ID_GRID, ID_DAYLIGHT, ID_MAGLIM, ID_FIND,
    ID_ECLIPTIC, ID_SUNBTN, ID_MOONBTN,
    ID_STATUS, ID_TIMER = 1
};

/* -------------------------------------------------------------- state */
static HINSTANCE g_inst;
static HWND g_wnd, g_status;
static HWND g_ctl[64];
static HFONT g_ui, g_small, g_label;
static int g_dpi = 96;
static int g_panel_h = 72;

static SkyData g_sd;
static SkyTime g_st;
static double g_tz = 0; /* hours added to UT for display */

/* view: stereographic centered at (g_az0, g_alt0), full angle g_fov */
static double g_az0 = PI, g_alt0 = PI / 2, g_fov = degrad(190);
static int g_lines = 1, g_bounds = 0, g_names = 1, g_grid = 0, g_daylight = 1;
static int g_ecliptic = 1;
static SkyRiseSet g_sunrs, g_moonrs; /* rise/transit/set for the local day */
static double g_maglim = 6.0;
static int g_playing = 0;

static int g_dragging = 0, g_dragx, g_dragy;
static double g_drag_az, g_drag_alt;
static int g_sel = -1;
static char g_hover[256];

/* hit list of drawn objects */
typedef struct { int x, y, r, idx; } Hit;
static Hit *g_hits;
static int g_nhits, g_hitcap;

/* projection basis, recomputed per paint */
static double fx, fy, fz, rx, ry, rz, ux, uy, uz, g_k;
static int g_cx, g_cy, g_w, g_h;

#define S(v) MulDiv((v), g_dpi, 96)

/* ------------------------------------------------------------ helpers */
static void get_text(int id, char *buf, int n) { GetWindowTextA(g_ctl[id - 100], buf, n); }
static double get_num(int id) { char b[64]; get_text(id, b, sizeof b); return atof(b); }
static void set_text(int id, const char *s) { SetWindowTextA(g_ctl[id - 100], s); }
static void set_num(int id, const char *fmt, double v)
{
    char b[64];
    snprintf(b, sizeof b, fmt, v);
    set_text(id, b);
}

static void exe_dir(char *out, int n)
{
    char *p;
    GetModuleFileNameA(NULL, out, n);
    p = strrchr(out, '\\');
    if (p) *p = '\0';
}

static void view_basis(void)
{
    double ca = cos(g_alt0), sa = sin(g_alt0), cz = cos(g_az0), sz = sin(g_az0);
    fx = ca * sz; fy = ca * cz; fz = sa;   /* forward  */
    rx = cz; ry = -sz; rz = 0;             /* right    */
    ux = ry * fz - rz * fy;                /* up = r x f */
    uy = rz * fx - rx * fz;
    uz = rx * fy - ry * fx;
    g_k = (g_w < g_h ? g_w : g_h) / 2.0 / tan(g_fov / 4.0);
}

/* alt/az (radians) -> screen. returns 0 if not projectable. */
static int project(double alt, double az, double *sx, double *sy)
{
    double ca = cos(alt);
    double px = ca * sin(az), py = ca * cos(az), pz = sin(alt);
    double d = px * fx + py * fy + pz * fz;
    double X, Y;
    if (d < -0.995) return 0;
    X = (px * rx + py * ry + pz * rz) / (1 + d);
    Y = (px * ux + py * uy + pz * uz) / (1 + d);
    *sx = g_cx + X * g_k;
    *sy = g_cy - Y * g_k;
    return fabs(*sx) < 1e6 && fabs(*sy) < 1e6;
}

static int onscreen(double x, double y, int m)
{
    return x >= -m && y >= -m && x <= g_w + m && y <= g_h + m;
}

static void add_hit(int x, int y, int r, int idx)
{
    if (g_nhits == g_hitcap) {
        g_hitcap = g_hitcap ? g_hitcap * 2 : 2048;
        g_hits = (Hit *)realloc(g_hits, g_hitcap * sizeof(Hit));
    }
    g_hits[g_nhits].x = x; g_hits[g_nhits].y = y;
    g_hits[g_nhits].r = r; g_hits[g_nhits].idx = idx;
    g_nhits++;
}

/* ---------------------------------------------------------- time I/O */
static void time_to_fields(void)
{
    int mn, yr, d, h, m;
    double dy, f;
    mjd_cal(g_st.ut_mjd + g_tz / 24.0, &mn, &dy, &yr);
    d = (int)floor(dy);
    f = (dy - d) * 1440.0 + 0.5; /* minutes */
    h = (int)(f / 60);
    m = (int)f % 60;
    if (h == 24) { h = 23; m = 59; }
    set_num(ID_YEAR, "%.0f", yr);
    set_num(ID_MONTH, "%.0f", mn);
    set_num(ID_DAY, "%.0f", d);
    set_num(ID_HOUR, "%.0f", h);
    set_num(ID_MIN, "%.0f", m);
    set_num(ID_TZ, "%+.2f", g_tz);
}

static double fields_to_mjd(void)
{
    double mj;
    int yr = (int)get_num(ID_YEAR);
    if (yr == 0) yr = -1; /* no year 0 in historical numbering */
    cal_mjd((int)get_num(ID_MONTH), get_num(ID_DAY), yr, &mj);
    mj += (get_num(ID_HOUR) + get_num(ID_MIN) / 60.0) / 24.0;
    g_tz = get_num(ID_TZ);
    return mj - g_tz / 24.0;
}

static void update_sunmoon(void)
{
    SkyObj *sun = sky_find_kind(&g_sd, SK_SUN), *moon = sky_find_kind(&g_sd, SK_MOON);
    if (sun) sky_riset(&g_st, sun, g_tz, &g_sunrs);
    if (moon) sky_riset(&g_st, moon, g_tz, &g_moonrs);
}

static void recompute(void)
{
    char t[128], b[256], lst[32];
    int nm;
    sky_compute(&g_sd, &g_st);
    update_sunmoon();
    sky_fmt_time(&g_st, t, sizeof t);
    SendMessageA(g_status, SB_SETTEXTA, 0, (LPARAM)t);
    fs_sexa(lst, g_st.lst, 2, 60);
    nm = snprintf(b, sizeof b, "LST %s   JD %.4f   Delta-T %.0f s", lst,
                  g_st.ut_mjd + MJD0, deltat(g_st.ut_mjd));
    (void)nm;
    SendMessageA(g_status, SB_SETTEXTA, 1, (LPARAM)b);
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void apply_inputs(void)
{
    g_st.lat_deg = get_num(ID_LAT);
    g_st.lng_deg = get_num(ID_LON);
    if (g_st.lat_deg > 90) g_st.lat_deg = 90;
    if (g_st.lat_deg < -90) g_st.lat_deg = -90;
    g_st.ut_mjd = fields_to_mjd();
    sky_set_mjd(&g_st, g_st.ut_mjd);
    g_maglim = get_num(ID_MAGLIM);
    recompute();
}

static void set_time_mjd(double mj)
{
    g_st.ut_mjd = mj;
    sky_set_mjd(&g_st, mj);
    time_to_fields();
    recompute();
}

static void step_time(int dir)
{
    int unit = (int)SendMessageA(g_ctl[ID_STEPUNIT - 100], CB_GETCURSEL, 0, 0);
    static const double mins[] = { 1, 10, 60, 1440, 1436.068 };
    if (unit <= 4) {
        set_time_mjd(g_st.ut_mjd + dir * mins[unit] / 1440.0);
    } else { /* calendar steps keep the clock time */
        int mn, yr;
        double dy, mj, frac;
        mjd_cal(g_st.ut_mjd + g_tz / 24.0, &mn, &dy, &yr);
        frac = dy - floor(dy);
        dy = floor(dy);
        if (unit == 5) {
            mn += dir;
            while (mn > 12) { mn -= 12; yr++; if (yr == 0) yr = 1; }
            while (mn < 1) { mn += 12; yr--; if (yr == 0) yr = -1; }
        } else {
            int n = unit == 6 ? 1 : unit == 7 ? 10 : 100;
            int y0 = yr;
            yr += dir * n;
            if (y0 > 0 && yr <= 0) yr--;      /* skip year 0 */
            else if (y0 < 0 && yr >= 0) yr++;
        }
        if (dy > 28) { int nd; double m2; cal_mjd(mn, 1, yr, &m2); mjd_dpm(m2, &nd); if (dy > nd) dy = nd; }
        cal_mjd(mn, dy + frac, yr, &mj);
        set_time_mjd(mj - g_tz / 24.0);
    }
}

/* ----------------------------------------------------------- painting */
static COLORREF lerp(COLORREF a, COLORREF b, double t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
               (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

static double sun_alt_deg(void)
{
    int i;
    for (i = g_sd.nobjs - 1; i >= 0; i--)
        if (g_sd.objs[i].kind == SK_SUN) return raddeg(g_sd.objs[i].alt);
    return -90;
}

static COLORREF star_color(const SkyObj *p)
{
    switch (p->o.f_spect[0]) {
    case 'O': case 'B': return RGB(170, 195, 255);
    case 'A': return RGB(225, 235, 255);
    case 'F': return RGB(255, 250, 230);
    case 'G': return RGB(255, 235, 180);
    case 'K': return RGB(255, 200, 140);
    case 'M': return RGB(255, 160, 120);
    default: return RGB(235, 235, 235);
    }
}

static void line_altaz(HDC dc, double ra0, double dec0, double ra1, double dec1, int steps)
{
    int i, pen_down = 0;
    double dra = ra1 - ra0;
    if (dra > PI) dra -= TWOPI;
    if (dra < -PI) dra += TWOPI;
    for (i = 0; i <= steps; i++) {
        double t = (double)i / steps, alt, az, x, y;
        sky_radec2altaz(&g_st, ra0 + dra * t, dec0 + (dec1 - dec0) * t, &alt, &az);
        if (!project(alt, az, &x, &y) || !onscreen(x, y, g_w)) { pen_down = 0; continue; }
        if (pen_down) LineTo(dc, (int)x, (int)y);
        else MoveToEx(dc, (int)x, (int)y, NULL);
        pen_down = 1;
    }
}

static void draw_label(HDC dc, int x, int y, const char *s, COLORREF c)
{
    SetTextColor(dc, c);
    TextOutA(dc, x, y, s, (int)strlen(s));
}

/* unit screen direction (dx right, dy down) toward alt/az, even off-view */
static void view_dir(double alt, double az, double *dx, double *dy)
{
    double ca = cos(alt), px = ca * sin(az), py = ca * cos(az), pz = sin(alt);
    double X = px * rx + py * ry + pz * rz, Y = px * ux + py * uy + pz * uz;
    double n = sqrt(X * X + Y * Y);
    if (n < 1e-9) { X = 0; Y = -1; n = 1; }
    *dx = X / n;
    *dy = -Y / n;
}

/* screen direction from object a toward object b along the sky */
static void sky_dir(const SkyObj *a, const SkyObj *b, double ax, double ay, double *dx, double *dy)
{
    double m[3], t[3], q[3], d, n, alt, az, qx, qy, e = 0.01;
    int i;
    m[0] = cos(a->alt) * sin(a->az); m[1] = cos(a->alt) * cos(a->az); m[2] = sin(a->alt);
    t[0] = cos(b->alt) * sin(b->az); t[1] = cos(b->alt) * cos(b->az); t[2] = sin(b->alt);
    d = m[0] * t[0] + m[1] * t[1] + m[2] * t[2];
    for (i = 0; i < 3; i++) t[i] -= d * m[i];
    n = sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    *dx = 1; *dy = 0;
    if (n < 1e-9) return;
    for (i = 0; i < 3; i++) q[i] = m[i] * cos(e) + t[i] / n * sin(e);
    alt = asin(q[2]);
    az = atan2(q[0], q[1]);
    if (!project(alt, az, &qx, &qy)) return;
    qx -= ax; qy -= ay;
    n = sqrt(qx * qx + qy * qy);
    if (n > 1e-9) { *dx = qx / n; *dy = qy / n; }
}

/* moon disk with the lit part facing (sx,sy); k = fraction illuminated */
static void draw_moon_disk(HDC dc, double x, double y, double r, double sx, double sy,
                           double k, int below)
{
    POINT pts[82];
    int i, n = 0;
    double vx = -sy, vy = sx, e = r * (1 - 2 * k);
    HBRUSH dark = CreateSolidBrush(below ? RGB(38, 38, 44) : RGB(58, 58, 66));
    HBRUSH lit = CreateSolidBrush(below ? RGB(120, 120, 108) : RGB(235, 235, 215));
    HPEN rim = CreatePen(PS_SOLID, 1, below ? RGB(110, 110, 100) : RGB(150, 150, 140));
    HGDIOBJ ob, op;

    ob = SelectObject(dc, dark);
    op = SelectObject(dc, rim);
    Ellipse(dc, (int)(x - r), (int)(y - r), (int)(x + r + 1), (int)(y + r + 1));
    for (i = 0; i <= 40; i++) {
        double th = -PI / 2 + PI * i / 40, a = r * cos(th), b = r * sin(th);
        pts[n].x = (int)(x + a * sx + b * vx + 0.5);
        pts[n].y = (int)(y + a * sy + b * vy + 0.5);
        n++;
    }
    for (i = 0; i <= 40; i++) {
        double th = PI / 2 - PI * i / 40, a = e * cos(th), b = r * sin(th);
        pts[n].x = (int)(x + a * sx + b * vx + 0.5);
        pts[n].y = (int)(y + a * sy + b * vy + 0.5);
        n++;
    }
    SelectObject(dc, lit);
    SelectObject(dc, GetStockObject(NULL_PEN));
    if (k > 0.005) Polygon(dc, pts, n);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(dark);
    DeleteObject(lit);
    DeleteObject(rim);
}

static void draw_sun_disk(HDC dc, double x, double y, double r, int below)
{
    HBRUSH halo, disk;
    HPEN pen;
    HGDIOBJ ob, op;
    if (below) {
        pen = CreatePen(PS_SOLID, S(2), RGB(200, 150, 60));
        disk = CreateSolidBrush(RGB(90, 70, 30));
        op = SelectObject(dc, pen);
        ob = SelectObject(dc, disk);
        Ellipse(dc, (int)(x - r), (int)(y - r), (int)(x + r + 1), (int)(y + r + 1));
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(pen); DeleteObject(disk);
        return;
    }
    halo = CreateSolidBrush(RGB(255, 245, 190));
    disk = CreateSolidBrush(RGB(255, 225, 80));
    op = SelectObject(dc, GetStockObject(NULL_PEN));
    ob = SelectObject(dc, halo);
    Ellipse(dc, (int)(x - r * 1.8), (int)(y - r * 1.8), (int)(x + r * 1.8 + 1), (int)(y + r * 1.8 + 1));
    SelectObject(dc, disk);
    Ellipse(dc, (int)(x - r), (int)(y - r), (int)(x + r + 1), (int)(y + r + 1));
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(halo); DeleteObject(disk);
}

/* arrow on the view edge pointing to an off-view body */
static void edge_marker(HDC dc, double alt, double az, const char *label, COLORREF c)
{
    double dx, dy, t = 1e9, m = S(30), ex, ey;
    POINT tri[3];
    HBRUSH br;
    HGDIOBJ ob, op;
    char buf[96];
    view_dir(alt, az, &dx, &dy);
    if (dx > 1e-6) t = min(t, (g_w - m - g_cx) / dx);
    if (dx < -1e-6) t = min(t, (m - g_cx) / dx);
    if (dy > 1e-6) t = min(t, (g_h - m - g_cy) / dy);
    if (dy < -1e-6) t = min(t, (m - g_cy) / dy);
    ex = g_cx + dx * t;
    ey = g_cy + dy * t;
    tri[0].x = (int)(ex + dx * S(16)); tri[0].y = (int)(ey + dy * S(16));
    tri[1].x = (int)(ex - dy * S(8));  tri[1].y = (int)(ey + dx * S(8));
    tri[2].x = (int)(ex + dy * S(8));  tri[2].y = (int)(ey - dx * S(8));
    br = CreateSolidBrush(c);
    ob = SelectObject(dc, br);
    op = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, tri, 3);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(br);
    snprintf(buf, sizeof buf, "%s  alt %+.0f\xb0 az %.0f\xb0", label, raddeg(alt), raddeg(az));
    SelectObject(dc, g_ui);
    SetTextAlign(dc, (dx > 0.3 ? TA_RIGHT : dx < -0.3 ? TA_LEFT : TA_CENTER) | (dy > 0.3 ? TA_BOTTOM : TA_TOP));
    draw_label(dc, (int)(ex - dx * S(14)), (int)(ey - dy * S(14)), buf, c);
    SetTextAlign(dc, TA_LEFT | TA_TOP);
}

static void draw_ecliptic(HDC dc)
{
    int i, down = 0, was_up = -1;
    HPEN up = CreatePen(PS_DOT, 1, RGB(200, 170, 70));
    HPEN dn = CreatePen(PS_DOT, 1, RGB(90, 80, 45));
    HGDIOBJ op = SelectObject(dc, up);
    for (i = 0; i <= 360; i += 2) {
        double ra, dec, alt, az, x, y;
        int isup;
        ecl_eq(g_st.ut_mjd, 0.0, degrad(i), &ra, &dec);
        sky_radec2altaz(&g_st, ra, dec, &alt, &az);
        isup = alt >= 0;
        if (!project(alt, az, &x, &y) || !onscreen(x, y, g_w)) { down = 0; continue; }
        if (isup != was_up) {
            SelectObject(dc, isup ? up : dn);
            was_up = isup;
        }
        if (down) LineTo(dc, (int)x, (int)y); else MoveToEx(dc, (int)x, (int)y, NULL);
        down = 1;
    }
    SelectObject(dc, op);
    DeleteObject(up);
    DeleteObject(dn);
}

static void fmt_event(char *out, int n, double mj, int flags, int noflag)
{
    double f;
    int h, m;
    if (flags & RS_ERROR) { snprintf(out, n, "?"); return; }
    if (flags & RS_CIRCUMPOLAR) { snprintf(out, n, "up all day"); return; }
    if (flags & RS_NEVERUP) { snprintf(out, n, "never up"); return; }
    if (flags & noflag) { snprintf(out, n, "--:--"); return; }
    f = mj + g_tz / 24.0 + 0.5;
    f = (f - floor(f)) * 1440.0 + 0.5;
    h = (int)(f / 60) % 24;
    m = (int)f % 60;
    snprintf(out, n, "%02d:%02d", h, m);
}

/* Sun + Moon: always placed (on screen, on the ground if set, or as an edge arrow) */
static void draw_sun_moon(HDC dc)
{
    SkyObj *sun = sky_find_kind(&g_sd, SK_SUN), *moon = sky_find_kind(&g_sd, SK_MOON);
    SkyObj *b[2];
    int j;
    char buf[128];
    b[0] = moon; b[1] = sun; /* sun last so it is on top */
    for (j = 0; j < 2; j++) {
        SkyObj *p = b[j];
        double x, y, r;
        int below, issun;
        COLORREF lc;
        if (!p) continue;
        issun = p->kind == SK_SUN;
        below = p->alt < 0;
        lc = issun ? RGB(255, 210, 90) : RGB(220, 220, 240);
        if (!project(p->alt, p->az, &x, &y) || !onscreen(x, y, -S(10))) {
            edge_marker(dc, p->alt, p->az, issun ? "Sun" : "Moon", lc);
            continue;
        }
        r = g_k * tan(arcsecrad(p->o.s_size) / 4.0);
        if (r < S(issun ? 9 : 11)) r = S(issun ? 9 : 11);
        if (issun) {
            draw_sun_disk(dc, x, y, r, below);
            snprintf(buf, sizeof buf, below ? "Sun (below horizon, %+.0f\xb0)" : "Sun", raddeg(p->alt));
        } else {
            double sx = 1, sy = 0;
            SkyMoonPhase mp;
            sky_moon_phase(p, &mp);
            if (sun) sky_dir(p, sun, x, y, &sx, &sy);
            draw_moon_disk(dc, x, y, r, sx, sy, mp.illum, below);
            if (below)
                snprintf(buf, sizeof buf, "Moon %.0f%% (below horizon, %+.0f\xb0)", mp.illum * 100, raddeg(p->alt));
            else
                snprintf(buf, sizeof buf, "Moon %.0f%%  %s", mp.illum * 100, mp.name);
        }
        SelectObject(dc, g_ui);
        draw_label(dc, (int)(x + r + S(4)), (int)(y - r - S(6)), buf, lc);
        add_hit((int)x, (int)y, (int)r + S(4), (int)(p - g_sd.objs));
    }
}

/* top-right panel: where the Sun and Moon are and when they rise/set */
static void draw_info_panel(HDC dc)
{
    SkyObj *sun = sky_find_kind(&g_sd, SK_SUN), *moon = sky_find_kind(&g_sd, SK_MOON);
    char ln[7][160], r1[16], t1[16], s1[16];
    int n = 0, i, lh = S(17), w = S(440), x0, y0;
    HBRUSH br;
    HPEN pen;
    HGDIOBJ ob, op;
    SkyMoonPhase mp;
    if (!sun || !moon) return;
    snprintf(ln[n++], 160, "SUN    alt %+6.1f\xb0   az %5.1f\xb0   in %s", raddeg(sun->alt),
             raddeg(sun->az), sky_constellation(&g_st, sun->ra, sun->dec) + 5);
    fmt_event(r1, 16, g_sunrs.rise, g_sunrs.flags, RS_NORISE);
    fmt_event(t1, 16, g_sunrs.transit, g_sunrs.flags, RS_NOTRANS);
    fmt_event(s1, 16, g_sunrs.set, g_sunrs.flags, RS_NOSET);
    snprintf(ln[n++], 160, "           rise %s   noon %s   set %s", r1, t1, s1);
    sky_moon_phase(moon, &mp);
    snprintf(ln[n++], 160, "MOON   alt %+6.1f\xb0   az %5.1f\xb0   in %s", raddeg(moon->alt),
             raddeg(moon->az), sky_constellation(&g_st, moon->ra, moon->dec) + 5);
    snprintf(ln[n++], 160, "           %s, %.0f%% lit, age %.1f d", mp.name, mp.illum * 100, mp.age);
    fmt_event(r1, 16, g_moonrs.rise, g_moonrs.flags, RS_NORISE);
    fmt_event(t1, 16, g_moonrs.transit, g_moonrs.flags, RS_NOTRANS);
    fmt_event(s1, 16, g_moonrs.set, g_moonrs.flags, RS_NOSET);
    snprintf(ln[n++], 160, "           rise %s   transit %s   set %s", r1, t1, s1);
    snprintf(ln[n++], 160, "times in UT%+.2fh (same clock as the date fields)", g_tz);

    x0 = g_w - w - S(8);
    if (x0 < S(8)) x0 = S(8);
    y0 = S(30);
    br = CreateSolidBrush(RGB(14, 18, 36));
    pen = CreatePen(PS_SOLID, 1, RGB(70, 80, 120));
    ob = SelectObject(dc, br);
    op = SelectObject(dc, pen);
    Rectangle(dc, x0, y0, x0 + w, y0 + n * lh + S(10));
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(br); DeleteObject(pen);
    SelectObject(dc, g_ui);
    for (i = 0; i < n; i++) {
        COLORREF c = i < 2 ? RGB(255, 215, 110) : i < 5 ? RGB(215, 220, 240) : RGB(140, 150, 170);
        draw_label(dc, x0 + S(8), y0 + S(5) + i * lh, ln[i], c);
    }
}

static void paint_sky(HDC dc)
{
    double sunalt = sun_alt_deg();
    COLORREF night = RGB(4, 7, 20), twi = RGB(30, 45, 95), day = RGB(85, 135, 200);
    COLORREF sky, ground;
    HBRUSH br;
    HPEN pen, old;
    RECT all = { 0, 0, g_w, g_h };
    POINT hz[361];
    int i, nhz = 0, zen_inside;
    double x, y, zx, zy, eff_lim;
    char buf[128];

    if (sunalt > 0) sky = lerp(twi, day, sunalt / 10.0);
    else if (sunalt > -18) sky = lerp(night, twi, (sunalt + 18) / 18.0);
    else sky = night;
    if (!g_daylight) sky = night;
    ground = lerp(RGB(12, 20, 12), RGB(55, 75, 45), (sunalt + 6) / 12.0);
    if (!g_daylight) ground = RGB(12, 20, 12);

    br = CreateSolidBrush(sky);
    FillRect(dc, &all, br);
    DeleteObject(br);

    view_basis();
    g_nhits = 0;
    SetBkMode(dc, TRANSPARENT);

    /* grid */
    if (g_grid) {
        int a, z;
        pen = CreatePen(PS_SOLID, 1, RGB(40, 70, 60));
        old = (HPEN)SelectObject(dc, pen);
        for (a = 0; a < 90; a += 15) {
            int down = 0;
            for (z = 0; z <= 360; z += 3)
                if (project(degrad(a), degrad(z), &x, &y)) {
                    if (down) LineTo(dc, (int)x, (int)y); else MoveToEx(dc, (int)x, (int)y, NULL);
                    down = 1;
                } else down = 0;
        }
        for (z = 0; z < 360; z += 15) {
            int down = 0;
            for (a = 0; a <= 90; a += 3)
                if (project(degrad(a), degrad(z), &x, &y)) {
                    if (down) LineTo(dc, (int)x, (int)y); else MoveToEx(dc, (int)x, (int)y, NULL);
                    down = 1;
                } else down = 0;
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }

    /* constellation boundaries and figures */
    if (g_bounds) {
        pen = CreatePen(PS_DOT, 1, RGB(110, 50, 50));
        old = (HPEN)SelectObject(dc, pen);
        for (i = 0; i < g_sd.nbounds; i++) {
            SkySeg *s = &g_sd.bounds[i];
            line_altaz(dc, s->ra0, s->dec0, s->ra1, s->dec1, 12);
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    if (g_lines) {
        double cx[NCNS], cy[NCNS], cz[NCNS];
        memset(cx, 0, sizeof cx); memset(cy, 0, sizeof cy); memset(cz, 0, sizeof cz);
        pen = CreatePen(PS_SOLID, 1, RGB(60, 100, 150));
        old = (HPEN)SelectObject(dc, pen);
        for (i = 0; i < g_sd.nfigs; i++) {
            SkySeg *s = &g_sd.figs[i];
            line_altaz(dc, s->ra0, s->dec0, s->ra1, s->dec1, 4);
            if (s->id >= 0 && s->id < NCNS) {
                cx[s->id] += cos(s->dec0) * cos(s->ra0);
                cy[s->id] += cos(s->dec0) * sin(s->ra0);
                cz[s->id] += sin(s->dec0);
            }
        }
        SelectObject(dc, old);
        DeleteObject(pen);
        if (g_names) {
            SelectObject(dc, g_label);
            for (i = 0; i < NCNS; i++) {
                double alt, az, ra, dec, r = sqrt(cx[i] * cx[i] + cy[i] * cy[i] + cz[i] * cz[i]);
                const char *nm;
                if (r == 0) continue;
                ra = atan2(cy[i], cx[i]);
                dec = asin(cz[i] / r);
                sky_radec2altaz(&g_st, ra, dec, &alt, &az);
                if (alt < 0 || !project(alt, az, &x, &y) || !onscreen(x, y, 0)) continue;
                nm = cns_name(i) + 5; /* skip "Abc: " */
                SetTextAlign(dc, TA_CENTER);
                draw_label(dc, (int)x, (int)y, nm, RGB(90, 130, 180));
                SetTextAlign(dc, TA_LEFT);
            }
        }
    }

    /* stars and deep sky */
    eff_lim = g_maglim;
    if (g_daylight && sunalt > -12) eff_lim = g_maglim - (sunalt + 12) * 0.55;
    if (g_fov < degrad(60)) eff_lim += 1.0; /* zoomed in: show a bit deeper */
    SelectObject(dc, g_small);
    for (i = 0; i < g_sd.nobjs; i++) {
        SkyObj *p = &g_sd.objs[i];
        double r;
        if (p->kind >= SK_PLANET || p->alt < 0) continue;
        if (p->kind == SK_STAR && p->mag > eff_lim) continue;
        if (!project(p->alt, p->az, &x, &y) || !onscreen(x, y, 4)) continue;
        if (p->kind == SK_DEEPSKY) {
            if (eff_lim < 4 || (g_fov > degrad(120) && p->mag > 7.5)) continue;
            pen = CreatePen(PS_SOLID, 1, RGB(90, 170, 110));
            old = (HPEN)SelectObject(dc, pen);
            SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, (int)x - S(4), (int)y - S(3), (int)x + S(4), (int)y + S(3));
            SelectObject(dc, old);
            DeleteObject(pen);
            if (g_names && g_fov < degrad(70))
                draw_label(dc, (int)x + S(5), (int)y - S(5), p->label, RGB(90, 170, 110));
            add_hit((int)x, (int)y, S(5), i);
            continue;
        }
        r = (eff_lim + 1.2 - p->mag) * 0.55 * g_dpi / 96.0;
        if (r < 0.6) r = 0.6;
        if (r > S(6)) r = S(6);
        if (r < 1.0) {
            SetPixel(dc, (int)x, (int)y, lerp(sky, star_color(p), r));
        } else {
            br = CreateSolidBrush(star_color(p));
            SelectObject(dc, br);
            SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, (int)(x - r), (int)(y - r), (int)(x + r + 1), (int)(y + r + 1));
            DeleteObject(br);
        }
        if (g_names && p->mag < (g_fov < degrad(90) ? 3.0 : 1.6) &&
            !(p->label[0] >= '0' && p->label[0] <= '9') && strncmp(p->label, "HD", 2))
            draw_label(dc, (int)(x + r + 2), (int)(y - r - 2), p->label, RGB(200, 200, 170));
        add_hit((int)x, (int)y, (int)r + S(3), i);
    }

    /* ground below the horizon */
    for (i = 0; i <= 360; i++)
        if (project(0, degrad(i), &x, &y)) {
            hz[nhz].x = (int)x;
            hz[nhz].y = (int)y;
            nhz++;
        }
    zen_inside = 0;
    if (nhz > 3) {
        HRGN poly = CreatePolygonRgn(hz, nhz, WINDING);
        if (project(PI / 2, 0, &zx, &zy))
            zen_inside = PtInRegion(poly, (int)zx, (int)zy);
        else
            zen_inside = 0;
        if (zen_inside) {
            HRGN rect = CreateRectRgn(-10, -10, g_w + 10, g_h + 10);
            CombineRgn(rect, rect, poly, RGN_DIFF);
            br = CreateSolidBrush(ground);
            FillRgn(dc, rect, br);
            DeleteObject(br);
            DeleteObject(rect);
        } else {
            br = CreateSolidBrush(ground);
            FillRgn(dc, poly, br);
            DeleteObject(br);
        }
        DeleteObject(poly);
        pen = CreatePen(PS_SOLID, S(2), RGB(70, 110, 70));
        old = (HPEN)SelectObject(dc, pen);
        Polyline(dc, hz, nhz);
        SelectObject(dc, old);
        DeleteObject(pen);
    }

    /* cardinal points */
    {
        static const char *card[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        SelectObject(dc, g_ui);
        SetTextAlign(dc, TA_CENTER | TA_BOTTOM);
        for (i = 0; i < 8; i++)
            if (project(degrad(1.0), degrad(i * 45), &x, &y) && onscreen(x, y, 0))
                draw_label(dc, (int)x, (int)y, card[i], i % 2 ? RGB(150, 170, 120) : RGB(240, 220, 120));
        SetTextAlign(dc, TA_LEFT | TA_TOP);
    }

    /* sun, moon, planets (drawn last, on top) */
    for (i = 0; i < g_sd.nobjs; i++) {
        SkyObj *p = &g_sd.objs[i];
        double r;
        COLORREF c;
        if (p->kind != SK_PLANET || p->alt < degrad(-1)) continue;
        if (!project(p->alt, p->az, &x, &y) || !onscreen(x, y, 30)) continue;
        r = g_k * tan(arcsecrad(p->o.s_size) / 4.0); /* angular radius on screen */
        switch (p->kind) {
        case SK_SUN: c = RGB(255, 230, 90); if (r < S(7)) r = S(7); break;
        case SK_MOON: c = RGB(225, 225, 210); if (r < S(6)) r = S(6); break;
        default:
            if (p->mag > eff_lim && p->o.pl_code >= URANUS) continue;
            c = p->o.pl_code == MARS ? RGB(255, 140, 90) :
                p->o.pl_code == JUPITER ? RGB(255, 225, 180) :
                p->o.pl_code == SATURN ? RGB(240, 215, 150) :
                p->o.pl_code == VENUS ? RGB(255, 255, 235) : RGB(210, 220, 255);
            if (r < S(3)) r = S(3);
        }
        br = CreateSolidBrush(c);
        SelectObject(dc, br);
        SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, (int)(x - r), (int)(y - r), (int)(x + r + 1), (int)(y + r + 1));
        DeleteObject(br);
        SelectObject(dc, g_ui);
        if (p->kind == SK_MOON)
            snprintf(buf, sizeof buf, "Moon %.0f%%", p->o.s_phase);
        else
            snprintf(buf, sizeof buf, "%s", p->label);
        draw_label(dc, (int)(x + r + 3), (int)(y - r - S(4)), buf, RGB(255, 210, 120));
        add_hit((int)x, (int)y, (int)r + S(4), i);
    }

    if (g_ecliptic) draw_ecliptic(dc);
    draw_sun_moon(dc);

    /* selection marker */
    if (g_sel >= 0) {
        SkyObj *p = &g_sd.objs[g_sel];
        if (project(p->alt, p->az, &x, &y)) {
            pen = CreatePen(PS_SOLID, S(2), RGB(255, 80, 80));
            old = (HPEN)SelectObject(dc, pen);
            SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, (int)x - S(10), (int)y - S(10), (int)x + S(10), (int)y + S(10));
            SelectObject(dc, old);
            DeleteObject(pen);
        }
    }

    /* overlay: where & when */
    {
        char t[128];
        sky_fmt_time(&g_st, t, sizeof t);
        snprintf(buf, sizeof buf, "%s   lat %+.3f\xb0 lon %+.3f\xb0   view az %.0f\xb0 alt %.0f\xb0 fov %.0f\xb0",
                 t, g_st.lat_deg, g_st.lng_deg, fmod(raddeg(g_az0) + 360, 360), raddeg(g_alt0), raddeg(g_fov));
        SelectObject(dc, g_ui);
        draw_label(dc, S(8), S(6), buf, RGB(230, 230, 230));
        if (g_hover[0]) draw_label(dc, S(8), S(24), g_hover, RGB(255, 220, 140));
    }
    draw_info_panel(dc);
}

/* headless render of the current view to a 24-bit BMP; returns exit code */
static int snapshot(const char *path, int w, int h)
{
    HDC sdc = GetDC(NULL), mem = CreateCompatibleDC(sdc);
    HBITMAP bmp = CreateCompatibleBitmap(sdc, w, h), oldb;
    BITMAPINFOHEADER bi;
    BITMAPFILEHEADER bf;
    int stride = ((w * 3 + 3) & ~3), ok = 0;
    BYTE *px = (BYTE *)malloc((size_t)stride * h);
    FILE *fp;

    sky_compute(&g_sd, &g_st);
    update_sunmoon();
    g_w = w; g_h = h; g_cx = w / 2; g_cy = h / 2;
    oldb = (HBITMAP)SelectObject(mem, bmp);
    paint_sky(mem);
    SelectObject(mem, oldb);

    ZeroMemory(&bi, sizeof bi);
    bi.biSize = sizeof bi; bi.biWidth = w; bi.biHeight = h;
    bi.biPlanes = 1; bi.biBitCount = 24; bi.biCompression = BI_RGB;
    GetDIBits(mem, bmp, 0, h, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    fp = fopen(path, "wb");
    if (fp) {
        ZeroMemory(&bf, sizeof bf);
        bf.bfType = 0x4D42;
        bf.bfOffBits = sizeof bf + sizeof bi;
        bf.bfSize = bf.bfOffBits + stride * h;
        fwrite(&bf, sizeof bf, 1, fp);
        fwrite(&bi, sizeof bi, 1, fp);
        fwrite(px, stride, h, fp);
        fclose(fp);
        ok = 1;
    }
    free(px);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, sdc);
    return ok ? 0 : 2;
}

static void on_paint(HWND hw)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC dc = BeginPaint(hw, &ps), mem;
    HBITMAP bmp, oldb;
    GetClientRect(hw, &rc);
    {
        RECT sr;
        GetWindowRect(g_status, &sr);
        rc.bottom -= sr.bottom - sr.top;
    }
    rc.top = g_panel_h;
    g_w = rc.right - rc.left;
    g_h = rc.bottom - rc.top;
    if (g_w > 0 && g_h > 0) {
        g_cx = g_w / 2;
        g_cy = g_h / 2;
        mem = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, g_w, g_h);
        oldb = (HBITMAP)SelectObject(mem, bmp);
        paint_sky(mem);
        BitBlt(dc, 0, rc.top, g_w, g_h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldb);
        DeleteObject(bmp);
        DeleteDC(mem);
    }
    EndPaint(hw, &ps);
}

/* ------------------------------------------------------------- mouse */
static int hit_test(int mx, int my)
{
    int i, best = -1;
    double bd = 1e9;
    for (i = 0; i < g_nhits; i++) {
        double dx = g_hits[i].x - mx, dy = g_hits[i].y - my, d = dx * dx + dy * dy;
        double rr = (double)g_hits[i].r * g_hits[i].r;
        if (d <= rr + S(25) && d < bd) { bd = d; best = g_hits[i].idx; }
    }
    return best;
}

static void describe(int idx, char *out, int n)
{
    SkyObj *p = &g_sd.objs[idx];
    char ra[32], dec[32];
    const char *cn = sky_constellation(&g_st, p->ra, p->dec);
    fs_sexa(ra, radhr(p->ra), 2, 3600);
    fs_sexa(dec, raddeg(p->dec), 3, 3600);
    snprintf(out, n, "%s  (%s)  mag %.2f   alt %.1f\xb0 az %.1f\xb0   RA %s Dec %s   in %s",
             p->label, p->o.o_name, p->mag, raddeg(p->alt), raddeg(p->az), ra, dec, cn + 5);
}

static void face(double az, double alt, double fov)
{
    g_az0 = az; g_alt0 = alt; g_fov = fov;
    InvalidateRect(g_wnd, NULL, FALSE);
}

/* point the view at the Sun or Moon; if it is below the horizon, look
 * toward it while keeping the horizon in view */
static void center_on(SkyKind k)
{
    SkyObj *p = sky_find_kind(&g_sd, k);
    double alt;
    if (!p) return;
    alt = p->alt >= 0 ? p->alt : p->alt / 2 + degrad(10);
    if (alt < degrad(-60)) alt = degrad(-60);
    g_sel = (int)(p - g_sd.objs);
    describe(g_sel, g_hover, sizeof g_hover);
    face(p->az, alt, degrad(120));
}

static void find_object(void)
{
    char q[64];
    int i, n;
    get_text(ID_FIND, q, sizeof q);
    n = (int)strlen(q);
    if (!n) return;
    for (i = g_sd.nobjs - 1; i >= 0; i--) {
        SkyObj *p = &g_sd.objs[i];
        if (!_strnicmp(p->label, q, n) || !_strnicmp(p->o.o_name, q, n)) {
            g_sel = i;
            describe(i, g_hover, sizeof g_hover);
            if (g_fov > degrad(100)) g_fov = degrad(90);
            face(p->az, p->alt < degrad(-80) ? degrad(-80) : p->alt, g_fov);
            if (p->alt < 0) strncat(g_hover, "   [BELOW HORIZON]", sizeof g_hover - strlen(g_hover) - 1);
            return;
        }
    }
    snprintf(g_hover, sizeof g_hover, "\"%s\" not found", q);
    InvalidateRect(g_wnd, NULL, FALSE);
}

/* ------------------------------------------------------------ layout */
static HWND mk(const char *cls, const char *txt, DWORD style, int id)
{
    HWND h = CreateWindowExA(!strcmp(cls, "EDIT") ? WS_EX_CLIENTEDGE : 0, cls, txt,
                             WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g_wnd,
                             (HMENU)(INT_PTR)id, g_inst, NULL);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_ui, TRUE);
    if (id >= 100) g_ctl[id - 100] = h;
    return h;
}

static HWND g_lbl[24];
static int g_nlbl;

static void create_controls(void)
{
    static const char *units[] = { "1 minute", "10 minutes", "1 hour", "1 day",
        "1 sidereal day", "1 month", "1 year", "10 years", "100 years" };
    int i;
#define LBL(t) (g_lbl[g_nlbl++] = mk("STATIC", t, SS_RIGHT | SS_CENTERIMAGE, -1))
    LBL("Year");  mk("EDIT", "", ES_AUTOHSCROLL, ID_YEAR);
    LBL("Mon");   mk("EDIT", "", ES_NUMBER, ID_MONTH);
    LBL("Day");   mk("EDIT", "", ES_NUMBER, ID_DAY);
    LBL("Time");  mk("EDIT", "", ES_NUMBER, ID_HOUR);
    LBL(":");     mk("EDIT", "", ES_NUMBER, ID_MIN);
    LBL("UT+h");  mk("EDIT", "", ES_AUTOHSCROLL, ID_TZ);
    mk("BUTTON", "Local mean time", BS_PUSHBUTTON, ID_LMT);
    mk("BUTTON", "Now", BS_PUSHBUTTON, ID_NOW);
    LBL("Step");
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, ID_STEPUNIT);
    for (i = 0; i < 9; i++) SendMessageA(g_ctl[ID_STEPUNIT - 100], CB_ADDSTRING, 0, (LPARAM)units[i]);
    SendMessageA(g_ctl[ID_STEPUNIT - 100], CB_SETCURSEL, 2, 0);
    mk("BUTTON", "\xab", BS_PUSHBUTTON, ID_BACK);
    mk("BUTTON", "\xbb", BS_PUSHBUTTON, ID_FWD);
    mk("BUTTON", "Play", BS_PUSHBUTTON, ID_PLAY);
    mk("BUTTON", "Sun", BS_PUSHBUTTON, ID_SUNBTN);
    mk("BUTTON", "Moon", BS_PUSHBUTTON, ID_MOONBTN);

    LBL("Site");
    mk("COMBOBOX", "", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | CBS_SORT, ID_SITE);
    for (i = 0; i < g_sd.nsites; i++) {
        int k = (int)SendMessageA(g_ctl[ID_SITE - 100], CB_ADDSTRING, 0, (LPARAM)g_sd.sites[i].name);
        SendMessageA(g_ctl[ID_SITE - 100], CB_SETITEMDATA, k, i);
    }
    LBL("Lat");   mk("EDIT", "", ES_AUTOHSCROLL, ID_LAT);
    LBL("Lon");   mk("EDIT", "", ES_AUTOHSCROLL, ID_LON);
    mk("BUTTON", "Apply", BS_DEFPUSHBUTTON, ID_APPLY);
    mk("BUTTON", "Lines", BS_AUTOCHECKBOX, ID_LINES);
    mk("BUTTON", "Bounds", BS_AUTOCHECKBOX, ID_BOUNDS);
    mk("BUTTON", "Names", BS_AUTOCHECKBOX, ID_NAMES);
    mk("BUTTON", "Grid", BS_AUTOCHECKBOX, ID_GRID);
    mk("BUTTON", "Daylight", BS_AUTOCHECKBOX, ID_DAYLIGHT);
    mk("BUTTON", "Ecliptic", BS_AUTOCHECKBOX, ID_ECLIPTIC);
    LBL("Mag");   mk("EDIT", "6.0", ES_AUTOHSCROLL, ID_MAGLIM);
    LBL("Find");  mk("EDIT", "", ES_AUTOHSCROLL, ID_FIND);
    CheckDlgButton(g_wnd, ID_LINES, g_lines);
    CheckDlgButton(g_wnd, ID_NAMES, g_names);
    CheckDlgButton(g_wnd, ID_DAYLIGHT, g_daylight);
    CheckDlgButton(g_wnd, ID_ECLIPTIC, g_ecliptic);
}

static void layout(void)
{
    int x, y, h = S(24), l = 0;
    HWND *c = g_ctl;
#define PUT(hw, w) do { MoveWindow((hw), x, y, S(w), h, TRUE); x += S(w) + S(4); } while (0)
#define PUTL(w) PUT(g_lbl[l++], w)
#define PUTC(id, w) PUT(c[(id) - 100], w)
    x = S(6); y = S(6);
    PUTL(32); PUTC(ID_YEAR, 56);
    PUTL(30); PUTC(ID_MONTH, 32);
    PUTL(28); PUTC(ID_DAY, 32);
    PUTL(34); PUTC(ID_HOUR, 32);
    PUTL(6);  PUTC(ID_MIN, 32);
    PUTL(38); PUTC(ID_TZ, 52);
    PUTC(ID_LMT, 112); PUTC(ID_NOW, 44);
    PUTL(32);
    MoveWindow(c[ID_STEPUNIT - 100], x, y, S(110), S(300), TRUE); x += S(114);
    PUTC(ID_BACK, 28); PUTC(ID_FWD, 28); PUTC(ID_PLAY, 48);
    x += S(10); PUTC(ID_SUNBTN, 48); PUTC(ID_MOONBTN, 52);

    x = S(6); y = S(6) + h + S(6);
    PUTL(32);
    MoveWindow(c[ID_SITE - 100], x, y, S(230), S(400), TRUE); x += S(234);
    PUTL(26); PUTC(ID_LAT, 70);
    PUTL(28); PUTC(ID_LON, 76);
    PUTC(ID_APPLY, 56);
    PUTC(ID_LINES, 54); PUTC(ID_BOUNDS, 64); PUTC(ID_NAMES, 62); PUTC(ID_GRID, 48);
    PUTC(ID_DAYLIGHT, 74); PUTC(ID_ECLIPTIC, 72);
    PUTL(32); PUTC(ID_MAGLIM, 36);
    PUTL(32); PUTC(ID_FIND, 110);
    g_panel_h = y + h + S(6);
}

/* ---------------------------------------------------------- wndproc */
static LRESULT CALLBACK wndproc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        int parts[3];
        g_wnd = hw;
        g_status = CreateWindowExA(0, STATUSCLASSNAMEA, "", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                   0, 0, 0, 0, hw, (HMENU)ID_STATUS, g_inst, NULL);
        SendMessageA(g_status, WM_SETFONT, (WPARAM)g_ui, TRUE);
        parts[0] = S(330); parts[1] = S(700); parts[2] = -1;
        SendMessageA(g_status, SB_SETPARTS, 3, (LPARAM)parts);
        SendMessageA(g_status, SB_SETTEXTA, 2,
                     (LPARAM)"Drag: pan  Wheel: zoom  Z: zenith  N/E/S/W: face  U: Sun  M: Moon  PgUp/PgDn: step  Space: play");
        create_controls();
        layout();
        return 0;
    }
    case WM_SIZE:
        SendMessageA(g_status, WM_SIZE, 0, 0);
        layout();
        InvalidateRect(hw, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_PAINT: {
        /* paint panel background, then sky */
        HDC dc = GetDC(hw);
        RECT r;
        GetClientRect(hw, &r);
        r.bottom = g_panel_h;
        FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
        ReleaseDC(hw, dc);
        on_paint(hw);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        switch (id) {
        case ID_APPLY: apply_inputs(); break;
        case IDOK: /* Enter in an edit */
            if (GetFocus() == g_ctl[ID_FIND - 100]) find_object(); else apply_inputs();
            break;
        case ID_NOW: set_time_mjd(sky_mjd_now()); break;
        case ID_LMT:
            g_tz = get_num(ID_LON) / 15.0;
            set_num(ID_TZ, "%+.2f", g_tz);
            time_to_fields();
            break;
        case ID_BACK: step_time(-1); break;
        case ID_FWD: step_time(+1); break;
        case ID_PLAY:
            g_playing = !g_playing;
            SetWindowTextA(g_ctl[ID_PLAY - 100], g_playing ? "Stop" : "Play");
            if (g_playing) SetTimer(hw, ID_TIMER, 80, NULL); else KillTimer(hw, ID_TIMER);
            break;
        case ID_SITE:
            if (code == CBN_SELCHANGE) {
                HWND cb = g_ctl[ID_SITE - 100];
                int k = (int)SendMessageA(cb, CB_GETCURSEL, 0, 0);
                if (k >= 0) {
                    SkySite *s = &g_sd.sites[SendMessageA(cb, CB_GETITEMDATA, k, 0)];
                    set_num(ID_LAT, "%.4f", s->lat_deg);
                    set_num(ID_LON, "%.4f", s->lng_deg);
                    g_st.elev_m = s->elev_m;
                    apply_inputs();
                }
            }
            break;
        case ID_LINES: g_lines = IsDlgButtonChecked(hw, id); break;
        case ID_BOUNDS: g_bounds = IsDlgButtonChecked(hw, id); break;
        case ID_NAMES: g_names = IsDlgButtonChecked(hw, id); break;
        case ID_GRID: g_grid = IsDlgButtonChecked(hw, id); break;
        case ID_DAYLIGHT: g_daylight = IsDlgButtonChecked(hw, id); break;
        case ID_ECLIPTIC: g_ecliptic = IsDlgButtonChecked(hw, id); InvalidateRect(hw, NULL, FALSE); SetFocus(hw); break;
        case ID_SUNBTN: center_on(SK_SUN); SetFocus(hw); break;
        case ID_MOONBTN: center_on(SK_MOON); SetFocus(hw); break;
        }
        if (id >= ID_LINES && id <= ID_DAYLIGHT) { InvalidateRect(hw, NULL, FALSE); SetFocus(hw); }
        if (id == ID_BACK || id == ID_FWD || id == ID_PLAY || id == ID_NOW) SetFocus(hw);
        return 0;
    }
    case WM_TIMER:
        step_time(+1);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hw);
        if (GET_Y_LPARAM(lp) < g_panel_h) return 0;
        g_dragging = 1;
        g_dragx = GET_X_LPARAM(lp);
        g_dragy = GET_Y_LPARAM(lp);
        g_drag_az = g_az0;
        g_drag_alt = g_alt0;
        SetCapture(hw);
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging) {
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            g_dragging = 0;
            ReleaseCapture();
            if (abs(mx - g_dragx) < 3 && abs(my - g_dragy) < 3) {
                g_sel = hit_test(mx, my - g_panel_h);
                if (g_sel >= 0) describe(g_sel, g_hover, sizeof g_hover);
                InvalidateRect(hw, NULL, FALSE);
            }
        }
        return 0;
    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        if (g_dragging) {
            double s = 2.0 / g_k;
            g_az0 = g_drag_az - (mx - g_dragx) * s;
            g_alt0 = g_drag_alt + (my - g_dragy) * s;
            if (g_alt0 > PI / 2) g_alt0 = PI / 2;
            if (g_alt0 < -PI / 2 + 0.01) g_alt0 = -PI / 2 + 0.01;
            InvalidateRect(hw, NULL, FALSE);
        } else if (my > g_panel_h) {
            int h = hit_test(mx, my - g_panel_h);
            char buf[256] = "";
            if (h >= 0) describe(h, buf, sizeof buf);
            SendMessageA(g_status, SB_SETTEXTA, 2, (LPARAM)buf);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        g_fov *= d > 0 ? 0.85 : 1 / 0.85;
        if (g_fov < degrad(2)) g_fov = degrad(2);
        if (g_fov > degrad(250)) g_fov = degrad(250);
        InvalidateRect(hw, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case 'Z': face(PI, PI / 2, degrad(190)); break;
        case 'N': face(0, degrad(25), degrad(110)); break;
        case 'E': face(PI / 2, degrad(25), degrad(110)); break;
        case 'S': face(PI, degrad(25), degrad(110)); break;
        case 'W': face(3 * PI / 2, degrad(25), degrad(110)); break;
        case 'U': center_on(SK_SUN); break;
        case 'M': center_on(SK_MOON); break;
        case VK_ADD: case VK_OEM_PLUS: g_fov *= 0.8; InvalidateRect(hw, NULL, FALSE); break;
        case VK_SUBTRACT: case VK_OEM_MINUS: g_fov /= 0.8; if (g_fov > degrad(250)) g_fov = degrad(250); InvalidateRect(hw, NULL, FALSE); break;
        case VK_PRIOR: step_time(+1); break;
        case VK_NEXT: step_time(-1); break;
        case VK_SPACE: SendMessageA(hw, WM_COMMAND, ID_PLAY, 0); break;
        case VK_LEFT: g_az0 -= 0.1 * g_fov / 2; InvalidateRect(hw, NULL, FALSE); break;
        case VK_RIGHT: g_az0 += 0.1 * g_fov / 2; InvalidateRect(hw, NULL, FALSE); break;
        case VK_UP: g_alt0 = min(PI / 2, g_alt0 + 0.05 * g_fov); InvalidateRect(hw, NULL, FALSE); break;
        case VK_DOWN: g_alt0 = max(-PI / 2 + 0.01, g_alt0 - 0.05 * g_fov); InvalidateRect(hw, NULL, FALSE); break;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hw, msg, wp, lp);
}

/* ------------------------------------------------------------- main */
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    WNDCLASSEXA wc;
    MSG m;
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    char dir[MAX_PATH], data[MAX_PATH + 16], msg[512] = "";
    HDC sdc;
    HWND hw;
    (void)hp;

    g_inst = hi;
    SetProcessDPIAware();
    InitCommonControlsEx(&icc);
    sdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(NULL, sdc);
    g_ui = CreateFontA(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_small = CreateFontA(-S(11), 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_label = CreateFontA(-S(12), 0, 0, 0, FW_NORMAL, 1, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    exe_dir(dir, sizeof dir);
    snprintf(data, sizeof data, "%s\\data", dir);
    if (sky_load(&g_sd, data, msg, sizeof msg) < 0) {
        MessageBoxA(NULL, msg, "NeoXEphem Sky", MB_ICONERROR);
        return 1;
    }

    /* defaults: now, from Hanoi; command line "YEAR MONTH DAY HH:MM LAT LON" overrides */
    memset(&g_st, 0, sizeof g_st);
    g_st.temp_c = 10;
    g_st.pres_mb = 1010;
    g_st.lat_deg = 21.0285;
    g_st.lng_deg = 105.8542;
    g_st.ut_mjd = sky_mjd_now();
    if (cmd && *cmd) {
        int y, mo, d, h = 0, mi = 0;
        double la, lo;
        if (sscanf(cmd, "%d %d %d %d:%d %lf %lf", &y, &mo, &d, &h, &mi, &la, &lo) == 7) {
            double mj;
            cal_mjd(mo, d + (h + mi / 60.0) / 24.0, y, &mj);
            g_st.ut_mjd = mj;
            g_st.lat_deg = la;
            g_st.lng_deg = lo;
        }
    }
    sky_set_mjd(&g_st, g_st.ut_mjd);

    g_tz = g_st.lng_deg / 15.0; /* local mean time */

    /* optional: --view AZ ALT FOV (degrees)   --shot FILE.bmp [W H] */
    if (cmd) {
        const char *v = strstr(cmd, "--view"), *s = strstr(cmd, "--shot");
        double az, al, fv;
        if (v && sscanf(v + 6, "%lf %lf %lf", &az, &al, &fv) == 3) {
            g_az0 = degrad(az); g_alt0 = degrad(al); g_fov = degrad(fv);
        }
        if (s) {
            char path[MAX_PATH] = "";
            int w = 1600, h = 1000;
            const char *q = s + 6;
            while (*q == ' ') q++;
            if (*q == '"') { /* quoted path may contain spaces */
                const char *e = strchr(q + 1, '"');
                size_t n = e ? (size_t)(e - q - 1) : strlen(q + 1);
                if (n >= sizeof path) n = sizeof path - 1;
                memcpy(path, q + 1, n);
                path[n] = '\0';
                if (e) sscanf(e + 1, "%d %d", &w, &h);
            } else {
                sscanf(q, "%259s %d %d", path, &w, &h);
            }
            return snapshot(path, w, h);
        }
    }

    ZeroMemory(&wc, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);
    wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
    wc.hbrBackground = NULL;
    wc.lpszClassName = "NeoXEphemSky";
    RegisterClassExA(&wc);

    hw = CreateWindowExA(0, "NeoXEphemSky", "NeoXEphem Sky \x97 historical sky viewer (XEphem libastro)",
                         WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                         S(1280), S(860), NULL, NULL, hi, NULL);
    g_tz = g_st.lng_deg / 15.0; /* start in local mean time */
    set_num(ID_LAT, "%.4f", g_st.lat_deg);
    set_num(ID_LON, "%.4f", g_st.lng_deg);
    time_to_fields();
    recompute();
    ShowWindow(hw, show);
    UpdateWindow(hw);

    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (GetFocus() != hw && IsDialogMessageA(hw, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}
