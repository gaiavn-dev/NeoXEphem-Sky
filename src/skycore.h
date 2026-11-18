/* skycore: XEphem libastro wrapper for "the sky at any date, from any place".
 *
 * Date convention follows XEphem: historical year numbering (no year 0;
 * -1 = 1 BC, -500 = 500 BC). Dates before 1582-10-15 are Julian calendar,
 * from then on Gregorian. Time is UT (libastro adds Delta-T internally).
 */
#ifndef NEOX_SKYCORE_H
#define NEOX_SKYCORE_H

#include "astro.h"

typedef enum { SK_STAR, SK_DEEPSKY, SK_PLANET, SK_SUN, SK_MOON } SkyKind;

typedef struct {
    Obj o;          /* libastro object (catalog or built-in) */
    SkyKind kind;
    char label[32]; /* short display name */
    double alt, az; /* topocentric, refracted, radians (az E of N) */
    double ra, dec; /* apparent of date, radians */
    double mag;
} SkyObj;

typedef struct {
    char name[64];
    double lat_deg, lng_deg; /* degrees, + north / + east */
    double elev_m;     /* meters */
} SkySite;

typedef struct {
    double ra0, dec0, ra1, dec1; /* of date, radians */
    int pen;                     /* 1 solid, 2 dashed */
    int id;                      /* constellation id, -1 for boundaries */
} SkySeg;

typedef struct {
    /* inputs */
    int year, month;   /* historical year, 1..12 */
    double day;        /* day of month incl. fraction (UT) */
    double lat_deg, lng_deg;   /* degrees */
    double elev_m;       /* meters */
    double temp_c, pres_mb;
    /* derived */
    Now now;
    double ut_mjd;        /* UT ut_mjd */
    double lst;        /* local sidereal time, hours */
} SkyTime;

typedef struct {
    SkyObj *objs; int nobjs;
    SkySite *sites; int nsites;
    SkySeg *figs; int nfigs;     /* constellation figure segments of date */
    SkySeg *bounds; int nbounds; /* constellation boundaries of date */
    int nstars;
} SkyData;

/* load catalogs from datadir (SKY2k65.edb, Messier.edb, xephem_sites,
 * classicfigs.csf). returns 0 or -1 with msg filled. */
int sky_load(SkyData *sd, const char *datadir, char *msg, int msglen);

/* set date/place and compute every object's alt/az. */
void sky_compute(SkyData *sd, SkyTime *st);

/* convert of-date ra/dec (radians) to refracted alt/az for st. */
void sky_radec2altaz(const SkyTime *st, double ra, double dec,
                     double *alt, double *az);

/* constellation abbreviation (e.g. "UMa") at of-date ra/dec. */
const char *sky_constellation(const SkyTime *st, double ra, double dec);

/* format the sky time like "-0500-03-21 22:00:00 UT (Julian)". */
void sky_fmt_time(const SkyTime *st, char *out, int outlen);

/* rise / transit / set for the local day containing st (tz = hours added to
 * UT for "local"). times are UT mjd; flags are libastro RS_* bits. */
typedef struct {
    int flags;
    double rise, transit, set;
    double riseaz, setaz, tranalt; /* radians */
} SkyRiseSet;
void sky_riset(const SkyTime *st, const SkyObj *p, double tz_hours, SkyRiseSet *rs);

/* Moon phase summary from a computed Moon object. */
typedef struct {
    double illum;    /* fraction lit, 0..1 */
    double age;      /* days since new moon (approx, from elongation) */
    double elong;    /* degrees, >0 east of Sun (evening) */
    const char *name;
} SkyMoonPhase;
void sky_moon_phase(const SkyObj *moon, SkyMoonPhase *mp);

/* find the Sun / Moon entries */
SkyObj *sky_find_kind(SkyData *sd, SkyKind k);

/* UT ut_mjd from current system clock. */
double sky_mjd_now(void);
void sky_set_mjd(SkyTime *st, double ut_mjd);

#endif
