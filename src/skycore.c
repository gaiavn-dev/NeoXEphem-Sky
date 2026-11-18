/* skycore.c: catalogs + ephemerides for NeoXEphem on Windows.
 * Everything astronomical is delegated to XEphem's libastro. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "skycore.h"

#define NPLAN 10 /* MERCURY..MOON in PLCode order */

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = '\0';
    return s;
}

static void join(char *out, int n, const char *dir, const char *fn)
{
    snprintf(out, n, "%s\\%s", dir, fn);
}

static int push_obj(SkyData *sd, int *cap, const Obj *o, SkyKind k, const char *label)
{
    SkyObj *p;
    if (sd->nobjs == *cap) {
        *cap = *cap ? *cap * 2 : 1024;
        sd->objs = (SkyObj *)realloc(sd->objs, *cap * sizeof(SkyObj));
        if (!sd->objs) return -1;
    }
    p = &sd->objs[sd->nobjs++];
    memset(p, 0, sizeof(*p));
    p->o = *o;
    p->kind = k;
    strncpy(p->label, label, sizeof(p->label) - 1);
    return 0;
}

/* pick a friendly label: proper/Bayer name if the catalog gives one, else
 * the first alias. SKY2000 lines start with e.g. "Sirius|Alp CMa|HD 48915". */
static void star_label(const char *line, char *out, int n)
{
    int i = 0;
    while (line[i] && line[i] != '|' && line[i] != ',' && i < n - 1) {
        out[i] = line[i];
        i++;
    }
    out[i] = '\0';
}

static int load_edb(SkyData *sd, int *cap, const char *path, SkyKind kind)
{
    char line[1024], nm[1][MAXNM], why[256], label[64];
    FILE *fp = fopen(path, "r");
    int n = 0;
    if (!fp) return -1;
    while (fgets(line, sizeof line, fp)) {
        Obj o;
        char *s = trim(line);
        if (*s == '#' || *s == '!' || *s == '\0') continue;
        star_label(s, label, sizeof label);
        memset(&o, 0, sizeof o);
        if (db_crack_line(s, &o, nm, 0, why) < 0) continue;
        if (o.o_type != FIXED && o.o_type != BINARYSTAR) continue;
        if (push_obj(sd, cap, &o, kind, label) < 0) break;
        n++;
    }
    fclose(fp);
    return n;
}

static double parse_dms(const char *s)
{
    double d = 0, m = 0, x = 0;
    char hemi = 0;
    int n = sscanf(s, " %lf %lf %lf %c", &d, &m, &x, &hemi);
    double v;
    if (n < 4) { /* tolerate "d m N" */
        hemi = 0;
        if (sscanf(s, " %lf %lf %c", &d, &m, &hemi) == 3) x = 0;
    }
    v = fabs(d) + m / 60.0 + x / 3600.0;
    if (hemi == 'S' || hemi == 's' || hemi == 'W' || hemi == 'w') v = -v;
    return v;
}

static void load_sites(SkyData *sd, const char *path)
{
    char line[512];
    int cap = 0;
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    while (fgets(line, sizeof line, fp)) {
        char *f[6];
        int nf = 0;
        char *p = line;
        SkySite *s;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        f[nf++] = p;
        while (*p && nf < 6) {
            if (*p == ';') { *p = '\0'; f[nf++] = p + 1; }
            p++;
        }
        if (nf < 4) continue;
        if (sd->nsites == cap) {
            cap = cap ? cap * 2 : 256;
            sd->sites = (SkySite *)realloc(sd->sites, cap * sizeof(SkySite));
        }
        s = &sd->sites[sd->nsites++];
        strncpy(s->name, trim(f[0]), sizeof(s->name) - 1);
        s->name[sizeof(s->name) - 1] = '\0';
        s->lat_deg = parse_dms(f[1]);
        s->lng_deg = parse_dms(f[2]);
        s->elev_m = atof(f[3]);
        if (s->elev_m < 0) s->elev_m = 0;
    }
    fclose(fp);
}

int sky_load(SkyData *sd, const char *datadir, char *msg, int msglen)
{
    char path[1024];
    int cap = 0, i, n;
    Obj *bi;
    static const char *pnames[NPLAN] = { "Mercury", "Venus", "Mars", "Jupiter",
        "Saturn", "Uranus", "Neptune", "Pluto", "Sun", "Moon" };
    FILE *fp;

    memset(sd, 0, sizeof *sd);

    join(path, sizeof path, datadir, "SKY2k65.edb");
    n = load_edb(sd, &cap, path, SK_STAR);
    if (n <= 0) {
        snprintf(msg, msglen, "Cannot read star catalog %s", path);
        return -1;
    }
    sd->nstars = n;
    join(path, sizeof path, datadir, "Messier.edb");
    load_edb(sd, &cap, path, SK_DEEPSKY);

    getBuiltInObjs(&bi);
    for (i = 0; i < NPLAN; i++) {
        SkyKind k = (i == SUN) ? SK_SUN : (i == MOON) ? SK_MOON : SK_PLANET;
        push_obj(sd, &cap, &bi[i], k, pnames[i]);
    }

    join(path, sizeof path, datadir, "xephem_sites");
    load_sites(sd, path);

    join(path, sizeof path, datadir, "classicfigs.csf");
    fp = fopen(path, "r");
    if (fp) {
        char m[256];
        if (cns_loadfigs(fp, m) < 0)
            snprintf(msg, msglen, "constellation figures: %s", m);
        fclose(fp);
    }
    return 0;
}

static void build_lines(SkyData *sd, double e)
{
    static double ra[2000], dec[2000];
    static int dc[2000];
    double *r0, *d0, *r1, *d1;
    int id, i, n, cap;

    free(sd->figs);
    sd->figs = NULL;
    sd->nfigs = 0;
    cap = 0;
    for (id = 0; id < NCNS; id++) {
        n = cns_figure(id, e, ra, dec, dc);
        for (i = 1; i < n; i++) {
            if (dc[i] == 0) continue;
            if (sd->nfigs == cap) {
                cap = cap ? cap * 2 : 1024;
                sd->figs = (SkySeg *)realloc(sd->figs, cap * sizeof(SkySeg));
            }
            sd->figs[sd->nfigs].ra0 = ra[i - 1];
            sd->figs[sd->nfigs].dec0 = dec[i - 1];
            sd->figs[sd->nfigs].ra1 = ra[i];
            sd->figs[sd->nfigs].dec1 = dec[i];
            sd->figs[sd->nfigs].pen = dc[i];
            sd->figs[sd->nfigs].id = id;
            sd->nfigs++;
        }
    }

    free(sd->bounds);
    sd->bounds = NULL;
    sd->nbounds = 0;
    n = cns_edges(e, &r0, &d0, &r1, &d1);
    if (n > 0) {
        sd->bounds = (SkySeg *)malloc(n * sizeof(SkySeg));
        for (i = 0; i < n; i++) {
            sd->bounds[i].ra0 = r0[i];
            sd->bounds[i].dec0 = d0[i];
            sd->bounds[i].ra1 = r1[i];
            sd->bounds[i].dec1 = d1[i];
            sd->bounds[i].pen = 2;
            sd->bounds[i].id = -1;
        }
        sd->nbounds = n;
    }
}

void sky_set_mjd(SkyTime *st, double ut_mjd)
{
    int mn, yr;
    double dy;
    mjd_cal(ut_mjd, &mn, &dy, &yr);
    st->year = yr;
    st->month = mn;
    st->day = dy;
}

static void make_now(SkyTime *st)
{
    Now *np = &st->now;
    double ut_mjd;
    cal_mjd(st->month, st->day, st->year, &ut_mjd);
    st->ut_mjd = ut_mjd;
    memset(np, 0, sizeof *np);
    np->n_mjd = ut_mjd;
    np->n_lat = degrad(st->lat_deg);
    np->n_lng = degrad(st->lng_deg);
    np->n_tz = 0;
    np->n_temp = st->temp_c;
    np->n_pressure = st->pres_mb;
    np->n_elev = st->elev_m / ERAD;
    np->n_dip = degrad(18);
    np->n_epoch = EOD;
    strcpy(np->n_tznm, "UT");
    now_lst(np, &st->lst);
}

void sky_compute(SkyData *sd, SkyTime *st)
{
    static double last_e = -1e30;
    int i;

    make_now(st);
    for (i = 0; i < sd->nobjs; i++) {
        SkyObj *p = &sd->objs[i];
        if (obj_cir(&st->now, &p->o) < 0) {
            p->alt = -PI;
            continue;
        }
        p->alt = p->o.s_alt;
        p->az = p->o.s_az;
        p->ra = p->o.s_ra;
        p->dec = p->o.s_dec;
        p->mag = get_mag(&p->o);
    }
    /* figures only move by precession; rebuild when > ~1 day apart */
    if (fabs(st->ut_mjd - last_e) > 1.0) {
        build_lines(sd, st->ut_mjd);
        last_e = st->ut_mjd;
    }
}

void sky_radec2altaz(const SkyTime *st, double ra, double dec, double *alt, double *az)
{
    double ha = hrrad(st->lst) - ra;
    double ta, aa;
    hadec_aa(st->now.n_lat, ha, dec, &ta, az);
    refract(st->now.n_pressure, st->now.n_temp, ta, &aa);
    *alt = aa;
}

const char *sky_constellation(const SkyTime *st, double ra, double dec)
{
    int id = cns_pick(ra, dec, st->ut_mjd);
    return cns_name(id);
}

void sky_fmt_time(const SkyTime *st, char *out, int outlen)
{
    int d = (int)floor(st->day);
    double f = (st->day - d) * 24.0;
    int h = (int)floor(f);
    double mf = (f - h) * 60.0;
    int m = (int)floor(mf);
    int s = (int)floor((mf - m) * 60.0 + 0.5);
    int julian = st->year < 1582 || (st->year == 1582 && (st->month < 10 ||
                 (st->month == 10 && st->day < 15)));
    if (s == 60) { s = 0; m++; }
    if (m == 60) { m = 0; h++; }
    if (st->year < 0)
        snprintf(out, outlen, "%d BC %02d-%02d %02d:%02d:%02d UT (Julian)",
                 -st->year, st->month, d, h, m, s);
    else
        snprintf(out, outlen, "%04d-%02d-%02d %02d:%02d:%02d UT (%s)",
                 st->year, st->month, d, h, m, s, julian ? "Julian" : "Gregorian");
}

double sky_mjd_now(void)
{
    /* unix epoch 1970-01-01 0h UT == MJD(xephem) 25567.5 */
    return 25567.5 + (double)time(NULL) / 86400.0;
}

void sky_riset(const SkyTime *st, const SkyObj *p, double tz_hours, SkyRiseSet *rs)
{
    Now n = st->now;
    Obj o = p->o;
    RiseSet r;
    double dis = degrad(o.s_size / 3600.0 / 2.0); /* upper limb; refraction added inside */

    n.n_tz = -tz_hours; /* libastro: hours behind UTC */
    memset(&r, 0, sizeof r);
    riset_cir(&n, &o, dis, &r);
    rs->flags = r.rs_flags;
    rs->rise = r.rs_risetm;
    rs->transit = r.rs_trantm;
    rs->set = r.rs_settm;
    rs->riseaz = r.rs_riseaz;
    rs->setaz = r.rs_setaz;
    rs->tranalt = r.rs_tranalt;
}

void sky_moon_phase(const SkyObj *moon, SkyMoonPhase *mp)
{
    double e = moon->o.s_elong, k = moon->o.s_phase / 100.0;
    int waxing = e > 0;
    mp->illum = k;
    mp->elong = e;
    mp->age = (waxing ? e : 360.0 + e) / 360.0 * 29.530589;
    if (k < 0.03) mp->name = "New Moon";
    else if (k > 0.97) mp->name = "Full Moon";
    else if (k >= 0.47 && k <= 0.53) mp->name = waxing ? "First Quarter" : "Last Quarter";
    else if (k < 0.5) mp->name = waxing ? "Waxing Crescent" : "Waning Crescent";
    else mp->name = waxing ? "Waxing Gibbous" : "Waning Gibbous";
}

SkyObj *sky_find_kind(SkyData *sd, SkyKind k)
{
    int i;
    for (i = sd->nobjs - 1; i >= 0; i--)
        if (sd->objs[i].kind == k) return &sd->objs[i];
    return NULL;
}
