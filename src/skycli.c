/* neox-sky: print what is above the horizon at a given date and place.
 *
 * usage: neox-sky YEAR MONTH DAY HOUR:MIN LAT LON [maglimit]
 *   YEAR  historical numbering: -500 = 500 BC (Julian calendar before 1582)
 *   time  UT; LAT/LON degrees (+N, +E)
 * example: neox-sky -500 3 21 18:00 37.97 23.72      (Athens, 500 BC)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "skycore.h"

static void exedir(char *out, int n, const char *argv0)
{
    char *p;
    strncpy(out, argv0, n - 1);
    out[n - 1] = '\0';
    p = strrchr(out, '\\');
    if (!p) p = strrchr(out, '/');
    if (p) *p = '\0'; else strcpy(out, ".");
}

static int bymag(const void *a, const void *b)
{
    const SkyObj *x = *(const SkyObj **)a, *y = *(const SkyObj **)b;
    return (x->mag > y->mag) - (x->mag < y->mag);
}

int main(int argc, char **argv)
{
    SkyData sd;
    SkyTime st;
    char dir[1024], data[1100], msg[256] = "", tbuf[96], ra[32], dec[32];
    int h = 0, m = 0, i, n = 0;
    double maglim = 2.0;
    SkyObj **up;

    if (argc < 7) {
        fprintf(stderr, "usage: %s YEAR MONTH DAY HH:MM LAT LON [maglimit]\n"
                        "  YEAR: -500 = 500 BC; time is UT; lat/lon degrees +N +E\n",
                argv[0]);
        return 1;
    }
    exedir(dir, sizeof dir, argv[0]);
    snprintf(data, sizeof data, "%s\\data", dir);
    if (sky_load(&sd, data, msg, sizeof msg) < 0) {
        fprintf(stderr, "%s\n", msg);
        return 1;
    }
    memset(&st, 0, sizeof st);
    st.year = atoi(argv[1]);
    st.month = atoi(argv[2]);
    sscanf(argv[4], "%d:%d", &h, &m);
    st.day = atof(argv[3]) + (h + m / 60.0) / 24.0;
    st.lat_deg = atof(argv[5]);
    st.lng_deg = atof(argv[6]);
    st.temp_c = 10;
    st.pres_mb = 1010;
    if (argc > 7) maglim = atof(argv[7]);

    sky_compute(&sd, &st);
    sky_fmt_time(&st, tbuf, sizeof tbuf);
    fs_sexa(ra, st.lst, 2, 3600);
    printf("Sky for %s at lat %+.4f lon %+.4f\n", tbuf, st.lat_deg, st.lng_deg);
    printf("MJD %.5f  JD %.5f  Delta-T %.0f s  LST %s\n\n", st.ut_mjd, st.ut_mjd + MJD0,
           deltat(st.ut_mjd), ra);

    /* Sun and Moon: position, phase, and rise/transit/set in local mean time */
    {
        double lmt_h = st.lng_deg / 15.0;
        int k;
        for (k = 0; k < 2; k++) {
            SkyObj *p = sky_find_kind(&sd, k ? SK_MOON : SK_SUN);
            SkyRiseSet rs;
            char ev[3][16];
            double t[3];
            int j, no[3] = { RS_NORISE, RS_NOTRANS, RS_NOSET };
            if (!p) continue;
            sky_riset(&st, p, lmt_h, &rs);
            t[0] = rs.rise; t[1] = rs.transit; t[2] = rs.set;
            for (j = 0; j < 3; j++) {
                double f = t[j] + lmt_h / 24.0 + 0.5;
                f = (f - floor(f)) * 1440.0 + 0.5;
                if (rs.flags & RS_CIRCUMPOLAR) strcpy(ev[j], "up all day");
                else if (rs.flags & RS_NEVERUP) strcpy(ev[j], "never up");
                else if (rs.flags & (no[j] | RS_ERROR)) strcpy(ev[j], "--:--");
                else snprintf(ev[j], 16, "%02d:%02d", (int)(f / 60) % 24, (int)f % 60);
            }
            printf("%-5s alt %+6.2f az %6.2f in %-18s rise %s  transit %s  set %s (LMT)",
                   k ? "Moon" : "Sun", raddeg(p->alt), raddeg(p->az),
                   sky_constellation(&st, p->ra, p->dec) + 5, ev[0], ev[1], ev[2]);
            if (!(rs.flags & (RS_ERROR | RS_CIRCUMPOLAR | RS_NEVERUP)))
                printf("  riseaz %.2f setaz %.2f", raddeg(rs.riseaz), raddeg(rs.setaz));
            if (k) {
                SkyMoonPhase mp;
                sky_moon_phase(p, &mp);
                printf("  %s, %.0f%% lit, age %.1f d", mp.name, mp.illum * 100, mp.age);
            }
            printf("\n");
        }
        printf("\n");
    }

    up = (SkyObj **)malloc(sd.nobjs * sizeof *up);
    for (i = 0; i < sd.nobjs; i++) {
        SkyObj *p = &sd.objs[i];
        if (p->kind >= SK_PLANET || (p->alt > 0 && p->mag <= maglim))
            up[n++] = p;
    }
    qsort(up, n, sizeof *up, bymag);
    printf("%-22s %6s %10s %10s %8s %8s  %s\n", "Object", "Mag", "RA(date)",
           "Dec(date)", "Alt", "Az", "Constellation");
    for (i = 0; i < n; i++) {
        SkyObj *p = up[i];
        fs_sexa(ra, radhr(p->ra), 2, 3600);
        fs_sexa(dec, raddeg(p->dec), 3, 3600);
        printf("%-22.22s %6.2f %10s %10s %8.2f %8.2f  %s%s\n", p->label, p->mag, ra,
               dec, raddeg(p->alt), raddeg(p->az),
               sky_constellation(&st, p->ra, p->dec), p->alt < 0 ? "  (below horizon)" : "");
    }
    return 0;
}
