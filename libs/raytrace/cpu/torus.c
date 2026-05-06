#include "torus.h"
#include <math.h>

/* Torus: surface of points at distance `r` (minor) from a central circle
 * of radius `R` (major) lying in the plane through `center` with normal
 * `axis` (unit). The implicit equation in axis-local coords (axial = y,
 * radial^2 = x^2 + z^2) is
 *     (x^2 + y^2 + z^2 + R^2 - r^2)^2 = 4 R^2 (x^2 + z^2).
 * Substituting P(t) = ro' + t*rd (with ro' = ro - center) gives a
 * quartic in t solved here by Ferrari's method. */

static double cbrt_signed(double x) {
    return (x >= 0.0) ? cbrt(x) : -cbrt(-x);
}

/* Real roots of a*x^3 + b*x^2 + c*x + d = 0. Returns the count (1, 2, or
 * 3), filling roots[]. Uses Cardano's depressed-cubic form, which is
 * fine for our resolvent — the coefficient magnitudes stay tame. */
static int solve_cubic(double a, double b, double c, double d,
                       double roots[3]) {
    if (fabs(a) < 1e-14) {
        /* Quadratic fallback. */
        if (fabs(b) < 1e-14) {
            if (fabs(c) < 1e-14) return 0;
            roots[0] = -d / c;
            return 1;
        }
        double disc = c*c - 4.0*b*d;
        if (disc < 0.0) return 0;
        double sq = sqrt(disc);
        roots[0] = (-c + sq) / (2.0*b);
        roots[1] = (-c - sq) / (2.0*b);
        return 2;
    }
    double p = b / a;
    double q = c / a;
    double r = d / a;
    /* Depress: x = y - p/3, giving y^3 + P y + Q = 0. */
    double P = q - p*p/3.0;
    double Q = 2.0*p*p*p/27.0 - p*q/3.0 + r;
    double disc = Q*Q/4.0 + P*P*P/27.0;
    if (disc > 0.0) {
        double s  = sqrt(disc);
        double u  = cbrt_signed(-Q/2.0 + s);
        double v  = cbrt_signed(-Q/2.0 - s);
        roots[0] = u + v - p/3.0;
        return 1;
    } else if (disc < 0.0) {
        double rr = sqrt(-P*P*P/27.0);
        double cphi = -Q/(2.0*rr);
        if (cphi >  1.0) cphi =  1.0;
        if (cphi < -1.0) cphi = -1.0;
        double phi = acos(cphi);
        double m = 2.0 * sqrt(-P/3.0);
        roots[0] = m * cos(phi/3.0)                       - p/3.0;
        roots[1] = m * cos((phi + 2.0*M_PI)/3.0)          - p/3.0;
        roots[2] = m * cos((phi + 4.0*M_PI)/3.0)          - p/3.0;
        return 3;
    } else {
        double u = cbrt_signed(-Q/2.0);
        roots[0] =  2.0*u - p/3.0;
        roots[1] = -u     - p/3.0;
        return 2;
    }
}

/* Real roots of a quartic e4*t^4 + e3*t^3 + e2*t^2 + e1*t + e0 = 0.
 * Returns the count (0..4), filling roots[]. Ferrari's resolvent. */
static int solve_quartic(double e4, double e3, double e2, double e1, double e0,
                         double roots[4]) {
    if (fabs(e4) < 1e-20) return 0;
    double B = e3 / e4;
    double C = e2 / e4;
    double D = e1 / e4;
    double E = e0 / e4;
    /* Depress: t = y - B/4 → y^4 + p y^2 + q y + r = 0. */
    double p = C - 3.0*B*B/8.0;
    double q = D - B*C/2.0 + B*B*B/8.0;
    double r = E - B*D/4.0 + B*B*C/16.0 - 3.0*B*B*B*B/256.0;
    double shift = B / 4.0;

    int n = 0;

    if (fabs(q) < 1e-12) {
        /* Biquadratic: y^2 = (-p ± sqrt(p^2 - 4r))/2. */
        double disc = p*p - 4.0*r;
        if (disc < 0.0) return 0;
        double sq = sqrt(disc);
        double y2a = (-p + sq) / 2.0;
        double y2b = (-p - sq) / 2.0;
        if (y2a >= 0.0) {
            double ya = sqrt(y2a);
            roots[n++] =  ya - shift;
            roots[n++] = -ya - shift;
        }
        if (y2b >= 0.0) {
            double yb = sqrt(y2b);
            roots[n++] =  yb - shift;
            roots[n++] = -yb - shift;
        }
        return n;
    }

    /* Resolvent cubic: 8 z^3 - 4 p z^2 - 8 r z + (4 p r - q^2) = 0.
     * Pick any real root z; need 2z - p > 0 for the square root below. */
    double cr[3];
    int ncr = solve_cubic(8.0, -4.0*p, -8.0*r, 4.0*p*r - q*q, cr);
    if (ncr <= 0) return 0;

    double z = cr[0];
    for (int i = 1; i < ncr; i++) if (cr[i] > z) z = cr[i];

    double s2 = 2.0*z - p;
    if (s2 <= 0.0) return 0;
    double s = sqrt(s2);
    /* Two factored quadratics:
     *   y^2 - s y + (z - q/(2s)) = 0
     *   y^2 + s y + (z + q/(2s)) = 0   */
    double half_qs = q / (2.0 * s);
    {
        double bb = -s, cc = z - half_qs;
        double disc = bb*bb - 4.0*cc;
        if (disc >= 0.0) {
            double sq = sqrt(disc);
            roots[n++] = (-bb + sq)/2.0 - shift;
            roots[n++] = (-bb - sq)/2.0 - shift;
        }
    }
    {
        double bb =  s, cc = z + half_qs;
        double disc = bb*bb - 4.0*cc;
        if (disc >= 0.0) {
            double sq = sqrt(disc);
            roots[n++] = (-bb + sq)/2.0 - shift;
            roots[n++] = (-bb - sq)/2.0 - shift;
        }
    }
    return n;
}

float rt_intersect_torus(vector ro, vector rd, const scene_torus *torus) {
    double R = torus->major_radius;
    double r = torus->minor_radius;

    /* Translate origin so the torus is centered at zero. The axis A is
     * still in world space; we project ro' and rd along it to separate
     * axial and perpendicular components. */
    vector rop = vector_sub(ro, torus->center);
    vector A   = torus->axis;

    double yo = (double)vector_dot(rop, A);   /* axial component of origin */
    double ya = (double)vector_dot(rd,  A);   /* axial component of dir    */
    double a  = (double)vector_dot(rd,  rd);
    double b  = (double)vector_dot(rop, rd);
    double g  = (double)vector_dot(rop, rop);
    double q  = g + R*R - r*r;

    /* G(t)^2 = 4 R^2 * (perp^2). Expanding both sides gives the quartic
     * coefficients below. */
    double e4 = a * a;
    double e3 = 4.0 * a * b;
    double e2 = 4.0 * b * b + 2.0 * a * q - 4.0 * R*R * (a - ya*ya);
    double e1 = 4.0 * b * q - 8.0 * R*R * (b - yo*ya);
    double e0 = q * q - 4.0 * R*R * (g - yo*yo);

    double roots[4];
    int nr = solve_quartic(e4, e3, e2, e1, e0, roots);

    double best = -1.0;
    for (int i = 0; i < nr; i++) {
        double t = roots[i];
        if (t > 1e-4 && (best < 0.0 || t < best)) best = t;
    }
    return (float)best;
}

vector rt_normal_torus(vector hp, const scene_torus *torus) {
    /* Normal points from the closest point on the central circle to the
     * hit. The closest point is `center + R * unit(perp)`, where `perp`
     * is the component of (hp - center) perpendicular to `axis`. */
    vector cp = vector_sub(hp, torus->center);
    float ax = vector_dot(cp, torus->axis);
    vector perp = vector_sub(cp, vector_scale(torus->axis, ax));
    float plen = sqrtf(vector_dot(perp, perp));
    if (plen < 1e-8f) {
        /* Hit on the axis (only possible if R == 0, a sphere edge case).
         * Return the axial direction so we still produce a unit normal. */
        return vector_normalize(torus->axis);
    }
    vector closest = vector_add(torus->center,
                                vector_scale(perp, torus->major_radius / plen));
    return vector_normalize(vector_sub(hp, closest));
}
