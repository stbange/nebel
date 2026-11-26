#include "core/cv_depth.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float sample_bilinear(const float *buf, int w, int h, float u, float v) {
    float sx = w > 1 ? clamp01(u) * (float)(w - 1) : 0.0f;
    float sy = h > 1 ? clamp01(v) * (float)(h - 1) : 0.0f;
    int x0 = (int)floorf(sx);
    int y0 = (int)floorf(sy);
    int x1 = x0 + 1 < w ? x0 + 1 : w - 1;
    int y1 = y0 + 1 < h ? y0 + 1 : h - 1;
    float fx = sx - (float)x0;
    float fy = sy - (float)y0;
    float d00 = buf[(size_t)y0 * (size_t)w + (size_t)x0];
    float d10 = buf[(size_t)y0 * (size_t)w + (size_t)x1];
    float d01 = buf[(size_t)y1 * (size_t)w + (size_t)x0];
    float d11 = buf[(size_t)y1 * (size_t)w + (size_t)x1];
    return (d00 + (d10 - d00) * fx) * (1.0f - fy) + (d01 + (d11 - d01) * fx) * fy;
}

static bool box_filter(const float *src, float *dst, int w, int h, int r) {
    if (r <= 0) {
        memcpy(dst, src, (size_t)w * (size_t)h * sizeof(float));
        return true;
    }
    float *tmp = (float *)malloc((size_t)w * (size_t)h * sizeof(float));
    if (!tmp) return false;

    for (int y = 0; y < h; y++) {
        const float *row = src + (size_t)y * (size_t)w;
        float *out = tmp + (size_t)y * (size_t)w;
        double sum = 0.0;
        int x_end = r < w - 1 ? r : w - 1;
        for (int x = 0; x <= x_end; x++) sum += (double)row[x];
        for (int x = 0; x < w; x++) {
            int left = x - r;
            int right = x + r;
            if (x > 0) {
                if (right < w) sum += (double)row[right];
                if (left - 1 >= 0) sum -= (double)row[left - 1];
            }
            int c0 = left < 0 ? 0 : left;
            int c1 = right >= w ? w - 1 : right;
            out[x] = (float)(sum / (double)(c1 - c0 + 1));
        }
    }

    for (int x = 0; x < w; x++) {
        double sum = 0.0;
        int y_end = r < h - 1 ? r : h - 1;
        for (int y = 0; y <= y_end; y++) sum += (double)tmp[(size_t)y * (size_t)w + (size_t)x];
        for (int y = 0; y < h; y++) {
            int top = y - r;
            int bot = y + r;
            if (y > 0) {
                if (bot < h) sum += (double)tmp[(size_t)bot * (size_t)w + (size_t)x];
                if (top - 1 >= 0) sum -= (double)tmp[(size_t)(top - 1) * (size_t)w + (size_t)x];
            }
            int r0 = top < 0 ? 0 : top;
            int r1 = bot >= h ? h - 1 : bot;
            dst[(size_t)y * (size_t)w + (size_t)x] = (float)(sum / (double)(r1 - r0 + 1));
        }
    }

    free(tmp);
    return true;
}

bool nebel_guided_filter(const float *guide, const float *src, float *dst,
                         int width, int height, int radius, float eps) {
    if (!guide || !src || !dst || width <= 0 || height <= 0 || radius < 0) return false;
    if (eps < 1e-6f) eps = 1e-6f;
    size_t n = (size_t)width * (size_t)height;

    float *buf = (float *)malloc(n * 8u * sizeof(float));
    if (!buf) return false;
    float *mean_I  = buf + 0 * n;
    float *mean_p  = buf + 1 * n;
    float *I_sq    = buf + 2 * n;
    float *I_p     = buf + 3 * n;
    float *corr_I  = buf + 4 * n;
    float *corr_Ip = buf + 5 * n;
    float *a       = buf + 6 * n;
    float *b       = buf + 7 * n;

    for (size_t i = 0; i < n; i++) {
        I_sq[i] = guide[i] * guide[i];
        I_p[i]  = guide[i] * src[i];
    }

    bool ok = box_filter(guide, mean_I, width, height, radius) &&
              box_filter(src,   mean_p, width, height, radius) &&
              box_filter(I_sq,  corr_I, width, height, radius) &&
              box_filter(I_p,   corr_Ip, width, height, radius);

    if (ok) {
        for (size_t i = 0; i < n; i++) {
            float var_I = corr_I[i] - mean_I[i] * mean_I[i];
            float cov_Ip = corr_Ip[i] - mean_I[i] * mean_p[i];
            a[i] = cov_Ip / (var_I + eps);
            b[i] = mean_p[i] - a[i] * mean_I[i];
        }
        ok = box_filter(a, mean_I, width, height, radius) &&
             box_filter(b, mean_p, width, height, radius);
        if (ok) {
            for (size_t i = 0; i < n; i++) {
                dst[i] = clamp01(mean_I[i] * guide[i] + mean_p[i]);
            }
        }
    }

    free(buf);
    return ok;
}

bool nebel_guided_filter_rgb(const NebelImage *guide, const float *src, float *dst,
                             int radius, float eps) {
    if (!guide || !guide->rgba || !src || !dst ||
        guide->width <= 0 || guide->height <= 0 || radius < 0) {
        return false;
    }
    if (eps < 1e-6f) eps = 1e-6f;
    int w = guide->width;
    int h = guide->height;
    size_t n = (size_t)w * (size_t)h;

    /* 16 planes (<=180MB at 2048px, reused in-place across covariance -> linear coefficients) */
    float *buf = (float *)malloc(n * 16u * sizeof(float));
    if (!buf) return false;

    float *Ir = buf + 0 * n;
    float *Ig = buf + 1 * n;
    float *Ib = buf + 2 * n;
    float *mr = buf + 3 * n;
    float *mg = buf + 4 * n;
    float *mb = buf + 5 * n;
    float *mp = buf + 6 * n;
    float *v_rr = buf + 7 * n;
    float *v_rg = buf + 8 * n;
    float *v_rb = buf + 9 * n;
    float *v_gg = buf + 10 * n;
    float *v_gb = buf + 11 * n;
    float *v_bb = buf + 12 * n;
    float *c_rp = buf + 13 * n;
    float *c_gp = buf + 14 * n;
    float *c_bp = buf + 15 * n;

    for (size_t i = 0; i < n; i++) {
        const float *p = &guide->rgba[i * 4u];
        /* Perceptual sqrt RGB keeps dark fur/pine color boundaries crisp */
        Ir[i] = sqrtf(clamp01(p[0]));
        Ig[i] = sqrtf(clamp01(p[1]));
        Ib[i] = sqrtf(clamp01(p[2]));
    }

    bool ok = box_filter(Ir, mr, w, h, radius) &&
              box_filter(Ig, mg, w, h, radius) &&
              box_filter(Ib, mb, w, h, radius) &&
              box_filter(src, mp, w, h, radius);

    if (ok) {
        for (size_t i = 0; i < n; i++) {
            v_rr[i] = Ir[i] * Ir[i];
            v_rg[i] = Ir[i] * Ig[i];
            v_rb[i] = Ir[i] * Ib[i];
            v_gg[i] = Ig[i] * Ig[i];
            v_gb[i] = Ig[i] * Ib[i];
            v_bb[i] = Ib[i] * Ib[i];
            c_rp[i] = Ir[i] * src[i];
            c_gp[i] = Ig[i] * src[i];
            c_bp[i] = Ib[i] * src[i];
        }
        /* Filter covariance & cross-covariance products in place using dst as temp?
           box_filter allocates its own internal tmp so src==dst is safe! */
        ok = box_filter(v_rr, v_rr, w, h, radius) &&
             box_filter(v_rg, v_rg, w, h, radius) &&
             box_filter(v_rb, v_rb, w, h, radius) &&
             box_filter(v_gg, v_gg, w, h, radius) &&
             box_filter(v_gb, v_gb, w, h, radius) &&
             box_filter(v_bb, v_bb, w, h, radius) &&
             box_filter(c_rp, c_rp, w, h, radius) &&
             box_filter(c_gp, c_gp, w, h, radius) &&
             box_filter(c_bp, c_bp, w, h, radius);
    }

    if (ok) {
        /* Solve 3x3 symmetric linear system (Sigma + eps*I) * [ar, ag, ab]^T = cov_Ip
           Reuse v_rr..v_gg for ar, ag, ab, b */
        float *ar = v_rr;
        float *ag = v_rg;
        float *ab = v_rb;
        float *bv = v_gg;

        for (size_t i = 0; i < n; i++) {
            float rr = v_rr[i] - mr[i] * mr[i] + eps;
            float rg = v_rg[i] - mr[i] * mg[i];
            float rb = v_rb[i] - mr[i] * mb[i];
            float gg = v_gg[i] - mg[i] * mg[i] + eps;
            float gb = v_gb[i] - mg[i] * mb[i];
            float bb = v_bb[i] - mb[i] * mb[i] + eps;

            float cr = c_rp[i] - mr[i] * mp[i];
            float cg = c_gp[i] - mg[i] * mp[i];
            float cb = c_bp[i] - mb[i] * mp[i];

            float inv00 = gg * bb - gb * gb;
            float inv01 = rb * gb - rg * bb;
            float inv02 = rg * gb - rb * gg;
            float inv11 = rr * bb - rb * rb;
            float inv12 = rb * rg - rr * gb;
            float inv22 = rr * gg - rg * rg;

            float det = rr * inv00 + rg * inv01 + rb * inv02;
            float inv_det = (fabsf(det) > 1e-12f) ? (1.0f / det) : 0.0f;

            float a0 = (inv00 * cr + inv01 * cg + inv02 * cb) * inv_det;
            float a1 = (inv01 * cr + inv11 * cg + inv12 * cb) * inv_det;
            float a2 = (inv02 * cr + inv12 * cg + inv22 * cb) * inv_det;

            ar[i] = a0;
            ag[i] = a1;
            ab[i] = a2;
            bv[i] = mp[i] - a0 * mr[i] - a1 * mg[i] - a2 * mb[i];
        }

        ok = box_filter(ar, mr, w, h, radius) &&
             box_filter(ag, mg, w, h, radius) &&
             box_filter(ab, mb, w, h, radius) &&
             box_filter(bv, mp, w, h, radius);

        if (ok) {
            for (size_t i = 0; i < n; i++) {
                dst[i] = clamp01(mr[i] * Ir[i] + mg[i] * Ig[i] + mb[i] * Ib[i] + mp[i]);
            }
        }
    }

    free(buf);
    return ok;
}

static void extract_luma(const NebelImage *img, float *luma) {
    size_t n = (size_t)img->width * (size_t)img->height;
    for (size_t i = 0; i < n; i++) {
        const float *p = &img->rgba[i * 4u];
        float lin = clamp01(0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]);
        luma[i] = sqrtf(lin);
    }
}

/* Spread hyperbolic inverse-depth values cleanly across [0, 1] using blended p2..p98 + CDF */
static void normalize_depth_perceptual(float *buf, size_t n) {
    if (!buf || n == 0) return;
    float min_v = 1e9f, max_v = -1e9f;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] < min_v) min_v = buf[i];
        if (buf[i] > max_v) max_v = buf[i];
    }
    float span = max_v - min_v;
    if (span <= 1e-5f) {
        for (size_t i = 0; i < n; i++) buf[i] = 0.5f;
        return;
    }

    uint32_t hist[256] = {0};
    for (size_t i = 0; i < n; i++) {
        float t = clamp01((buf[i] - min_v) / span);
        int bin = (int)(t * 255.0f + 0.5f);
        if (bin > 255) bin = 255;
        hist[bin]++;
    }

    float cdf[256];
    uint32_t acc = 0;
    float p02 = 0.0f, p98 = 1.0f;
    bool got_lo = false, got_hi = false;
    for (int b = 0; b < 256; b++) {
        acc += hist[b];
        float c = (float)acc / (float)n;
        cdf[b] = c;
        if (!got_lo && c >= 0.02f) { p02 = (float)b / 255.0f; got_lo = true; }
        if (!got_hi && c >= 0.98f) { p98 = (float)b / 255.0f; got_hi = true; }
    }
    float pspan = p98 - p02;
    if (pspan < 0.05f) pspan = 0.05f;

    for (size_t i = 0; i < n; i++) {
        float t = clamp01((buf[i] - min_v) / span);
        int bin = (int)(t * 255.0f + 0.5f);
        if (bin > 255) bin = 255;
        float lin_p = clamp01((t - p02) / pspan);
        buf[i] = clamp01(0.55f * cdf[bin] + 0.45f * lin_p);
    }
}

bool nebel_depth_estimate_classical(const NebelImage *img, float horizon_y, float *out_depth) {
    if (!img || !img->rgba || !out_depth || img->width <= 0 || img->height <= 0) return false;
    int w = img->width;
    int h = img->height;
    size_t n = (size_t)w * (size_t)h;
    horizon_y = clamp01(horizon_y);

    float *luma = (float *)malloc(n * 4u * sizeof(float));
    if (!luma) return false;
    float *sharp = luma + n;
    float *dark  = luma + 2u * n;
    float *raw   = luma + 3u * n;

    extract_luma(img, luma);

    for (int y = 0; y < h; y++) {
        int ym = y > 0 ? y - 1 : 0;
        int yp = y + 1 < h ? y + 1 : h - 1;
        for (int x = 0; x < w; x++) {
            int xm = x > 0 ? x - 1 : 0;
            int xp = x + 1 < w ? x + 1 : w - 1;
            size_t idx = (size_t)y * (size_t)w + (size_t)x;
            float lap = fabsf(
                4.0f * luma[idx]
                - luma[(size_t)y * (size_t)w + (size_t)xm]
                - luma[(size_t)y * (size_t)w + (size_t)xp]
                - luma[(size_t)ym * (size_t)w + (size_t)x]
                - luma[(size_t)yp * (size_t)w + (size_t)x]
            );
            sharp[idx] = clamp01(lap * 5.0f);

            const float *p = &img->rgba[idx * 4u];
            float m = p[0] < p[1] ? p[0] : p[1];
            if (p[2] < m) m = p[2];
            dark[idx] = clamp01(sqrtf(clamp01(m)));
        }
    }

    int rad = (w < h ? w : h) / 40;
    if (rad < 2) rad = 2;
    if (rad > 16) rad = 16;

    if (!box_filter(sharp, raw, w, h, rad)) { free(luma); return false; }
    memcpy(sharp, raw, n * sizeof(float));
    if (!box_filter(dark, raw, w, h, rad)) { free(luma); return false; }
    memcpy(dark, raw, n * sizeof(float));

    for (int y = 0; y < h; y++) {
        float ny = h > 1 ? (float)y / (float)(h - 1) : 0.0f;
        float horiz_cue;
        if (ny <= horizon_y) {
            horiz_cue = 1.0f;
        } else {
            float denom = 1.0f - horizon_y;
            float t = denom > 1e-4f ? (ny - horizon_y) / denom : 1.0f;
            horiz_cue = powf(1.0f - clamp01(t), 1.35f);
        }
        for (int x = 0; x < w; x++) {
            size_t idx = (size_t)y * (size_t)w + (size_t)x;
            float defocus_cue = 1.0f - clamp01(sharp[idx] * 2.5f);
            float haze_cue = dark[idx];
            raw[idx] = 0.38f * horiz_cue + 0.27f * defocus_cue + 0.35f * haze_cue;
        }
    }

    normalize_depth_perceptual(raw, n);
    bool ok = nebel_guided_filter_rgb(img, raw, out_depth, rad, 0.0012f);
    if (!ok) {
        ok = nebel_guided_filter(luma, raw, out_depth, w, h, rad, 0.0008f);
    }
    free(luma);
    return ok;
}

bool nebel_depth_refine_external(const NebelImage *img, const float *raw_depth,
                                 int raw_w, int raw_h, float *out_depth) {
    if (!img || !img->rgba || !raw_depth || !out_depth ||
        raw_w <= 0 || raw_h <= 0 || img->width <= 0 || img->height <= 0) {
        return false;
    }
    int w = img->width;
    int h = img->height;
    size_t n = (size_t)w * (size_t)h;

    float *up = (float *)malloc(n * sizeof(float));
    if (!up) return false;

    for (int y = 0; y < h; y++) {
        float v = h > 1 ? (float)y / (float)(h - 1) : 0.0f;
        for (int x = 0; x < w; x++) {
            float u = w > 1 ? (float)x / (float)(w - 1) : 0.0f;
            up[(size_t)y * (size_t)w + (size_t)x] = sample_bilinear(raw_depth, raw_w, raw_h, u, v);
        }
    }

    normalize_depth_perceptual(up, n);

    int rad = (w < h ? w : h) / 140;
    if (rad < 2) rad = 2;
    if (rad > 6) rad = 6;
    bool ok = nebel_guided_filter_rgb(img, up, out_depth, rad, 0.0012f);
    if (!ok) {
        float *luma = (float *)malloc(n * sizeof(float));
        if (luma) {
            extract_luma(img, luma);
            ok = nebel_guided_filter(luma, up, out_depth, w, h, rad, 0.001f);
            free(luma);
        }
    }
    free(up);
    return ok;
}

static float edge_feather(float t, bool touches_lo, bool touches_hi) {
    float w_lo = touches_lo ? 1.0f : (t < 0.22f ? (0.5f - 0.5f * cosf(t / 0.22f * 3.14159265f)) : 1.0f);
    float w_hi = touches_hi ? 1.0f : (t > 0.78f ? (0.5f - 0.5f * cosf((1.0f - t) / 0.22f * 3.14159265f)) : 1.0f);
    float w = w_lo * w_hi;
    return w < 0.04f ? 0.04f : w;
}

bool nebel_depth_fuse_tiles(const float *global_depth, int gw, int gh,
                            const NebelDepthTile *tiles, int tile_count,
                            int out_w, int out_h, float *out_depth) {
    if (!global_depth || gw <= 0 || gh <= 0 || out_w <= 0 || out_h <= 0 || !out_depth) {
        return false;
    }
    size_t n = (size_t)out_w * (size_t)out_h;
    float *base = (float *)malloc(n * sizeof(float));
    float *acc_num = (float *)calloc(n, sizeof(float));
    float *acc_den = (float *)calloc(n, sizeof(float));
    if (!base || !acc_num || !acc_den) {
        free(base); free(acc_num); free(acc_den);
        return false;
    }

    for (int y = 0; y < out_h; y++) {
        float v = out_h > 1 ? (float)y / (float)(out_h - 1) : 0.0f;
        for (int x = 0; x < out_w; x++) {
            float u = out_w > 1 ? (float)x / (float)(out_w - 1) : 0.0f;
            base[(size_t)y * (size_t)out_w + (size_t)x] = sample_bilinear(global_depth, gw, gh, u, v);
        }
    }

    if (!tiles || tile_count <= 0) {
        memcpy(out_depth, base, n * sizeof(float));
        free(base); free(acc_num); free(acc_den);
        return true;
    }

    for (int k = 0; k < tile_count; k++) {
        const NebelDepthTile *t = &tiles[k];
        if (!t->data || t->width <= 0 || t->height <= 0 || t->u1 <= t->u0 || t->v1 <= t->v0) continue;

        int x0 = (int)floorf(clamp01(t->u0) * (float)(out_w - 1));
        int x1 = (int)ceilf(clamp01(t->u1) * (float)(out_w - 1));
        int y0 = (int)floorf(clamp01(t->v0) * (float)(out_h - 1));
        int y1 = (int)ceilf(clamp01(t->v1) * (float)(out_h - 1));
        if (x1 >= out_w) x1 = out_w - 1;
        if (y1 >= out_h) y1 = out_h - 1;

        /* Closed-form least-squares affine fit: a * tile + b ~= global base */
        double sum_t = 0.0, sum_g = 0.0, sum_tt = 0.0, sum_tg = 0.0;
        size_t cnt = 0;
        int step_x = (x1 - x0) > 128 ? (x1 - x0) / 64 : 1;
        int step_y = (y1 - y0) > 128 ? (y1 - y0) / 64 : 1;
        for (int y = y0; y <= y1; y += step_y) {
            float v = out_h > 1 ? (float)y / (float)(out_h - 1) : 0.0f;
            float tv = clamp01((v - t->v0) / (t->v1 - t->v0));
            for (int x = x0; x <= x1; x += step_x) {
                float u = out_w > 1 ? (float)x / (float)(out_w - 1) : 0.0f;
                float tu = clamp01((u - t->u0) / (t->u1 - t->u0));
                double td = (double)sample_bilinear(t->data, t->width, t->height, tu, tv);
                double gd = (double)base[(size_t)y * (size_t)out_w + (size_t)x];
                sum_t += td;
                sum_g += gd;
                sum_tt += td * td;
                sum_tg += td * gd;
                cnt++;
            }
        }
        if (cnt == 0) continue;
        double mt = sum_t / (double)cnt;
        double mg = sum_g / (double)cnt;
        double var_t = sum_tt / (double)cnt - mt * mt;
        double cov_tg = sum_tg / (double)cnt - mt * mg;
        float a = (var_t > 1e-6) ? (float)(cov_tg / var_t) : 1.0f;
        if (a < 0.15f) a = 0.15f;
        if (a > 4.0f) a = 4.0f;
        float b = (float)(mg - (double)a * mt);

        bool touch_l = (t->u0 <= 0.005f);
        bool touch_r = (t->u1 >= 0.995f);
        bool touch_t = (t->v0 <= 0.005f);
        bool touch_b = (t->v1 >= 0.995f);

        for (int y = y0; y <= y1; y++) {
            float v = out_h > 1 ? (float)y / (float)(out_h - 1) : 0.0f;
            float tv = clamp01((v - t->v0) / (t->v1 - t->v0));
            float wy = edge_feather(tv, touch_t, touch_b);
            for (int x = x0; x <= x1; x++) {
                float u = out_w > 1 ? (float)x / (float)(out_w - 1) : 0.0f;
                float tu = clamp01((u - t->u0) / (t->u1 - t->u0));
                float wx = edge_feather(tu, touch_l, touch_r);
                float wgt = wx * wy;
                float aligned = clamp01(a * sample_bilinear(t->data, t->width, t->height, tu, tv) + b);
                size_t idx = (size_t)y * (size_t)out_w + (size_t)x;
                acc_num[idx] += aligned * wgt;
                acc_den[idx] += wgt;
            }
        }
    }

    /* Blend aligned high-detail tile consensus (72%) with global depth anchor (28%) */
    for (size_t i = 0; i < n; i++) {
        if (acc_den[i] > 1e-5f) {
            float tile_d = acc_num[i] / acc_den[i];
            out_depth[i] = clamp01(0.28f * base[i] + 0.72f * tile_d);
        } else {
            out_depth[i] = clamp01(base[i]);
        }
    }

    free(base);
    free(acc_num);
    free(acc_den);
    return true;
}

void nebel_depth_paint_brush(float *depth, int w, int h,
                             float u0, float v0, float u1, float v1,
                             float radius, float strength, bool nearer) {
    if (!depth || w <= 0 || h <= 0 || radius <= 0.0f || strength <= 0.0f) return;
    float min_dim = (float)(w < h ? w : h);
    float r_px = radius * min_dim;
    if (r_px < 2.0f) r_px = 2.0f;

    float x0 = u0 * (float)(w - 1);
    float y0 = v0 * (float)(h - 1);
    float x1 = u1 * (float)(w - 1);
    float y1 = v1 * (float)(h - 1);
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    float spacing = r_px * 0.25f;
    if (spacing < 1.0f) spacing = 1.0f;
    int steps = (int)ceilf(dist / spacing);
    if (steps < 1) steps = 1;

    float delta = (nearer ? -0.06f : 0.06f) * clamp01(strength);
    for (int s = 0; s <= steps; s++) {
        float t = steps > 0 ? (float)s / (float)steps : 0.0f;
        float cx = x0 + dx * t;
        float cy = y0 + dy * t;
        int ix0 = (int)floorf(cx - r_px); if (ix0 < 0) ix0 = 0;
        int iy0 = (int)floorf(cy - r_px); if (iy0 < 0) iy0 = 0;
        int ix1 = (int)ceilf(cx + r_px);  if (ix1 >= w) ix1 = w - 1;
        int iy1 = (int)ceilf(cy + r_px);  if (iy1 >= h) iy1 = h - 1;
        float inv_r = 1.0f / r_px;
        for (int y = iy0; y <= iy1; y++) {
            float py = ((float)y - cy) * inv_r;
            for (int x = ix0; x <= ix1; x++) {
                float px = ((float)x - cx) * inv_r;
                float r2 = px * px + py * py;
                if (r2 >= 1.0f) continue;
                float k = 1.0f - sqrtf(r2);
                float falloff = k * k * (3.0f - 2.0f * k);
                size_t idx = (size_t)y * (size_t)w + (size_t)x;
                depth[idx] = clamp01(depth[idx] + delta * falloff);
            }
        }
    }
}
