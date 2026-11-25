in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_prev;    /* Current accumulated linear RGB + fog opacity in .a */
uniform sampler2D u_depth;   /* Normalized 3D depth [0=near, 1=far] in .r */
uniform sampler2D u_mask;    /* Guided-Filter refined painted fog mask [0,1] in .r */
uniform sampler2D u_ambient; /* Wide blurred scene illumination map (linear RGB) */

uniform float u_density;
uniform float u_height_falloff;
uniform float u_horizon_y;
uniform float u_depth_min;
uniform float u_depth_max;
uniform vec3  u_airlight;
uniform vec2  u_sun_pos;
uniform float u_sun_strength;
uniform float u_sun_g;
uniform float u_noise_scale;
uniform float u_noise_amount;
uniform float u_seed;

float hash21(vec2 p, float seed) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031 + seed * 0.00137);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float value_noise(vec2 p, float seed) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i, seed);
    float b = hash21(i + vec2(1.0, 0.0), seed);
    float c = hash21(i + vec2(0.0, 1.0), seed);
    float d = hash21(i + vec2(1.0, 1.0), seed);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p, float seed) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 4; i++) {
        val += amp * value_noise(p * freq, seed + float(i) * 19.3);
        freq *= 2.02;
        amp *= 0.5;
    }
    return clamp(val, 0.0, 1.0);
}

/* Remap narrow fBm [0.28, 0.72] -> [0, 1] for genuine patchy clearings and soft cloud banks */
float fbm_wide(vec2 p, float seed) {
    return clamp((fbm(p, seed) - 0.28) / 0.44, 0.0, 1.0);
}

float henyey_greenstein(float cos_theta, float g) {
    float g2 = g * g;
    float denom = max(1e-4, 1.0 + g2 - 2.0 * g * cos_theta);
    float p = (1.0 - g2) / pow(denom, 1.5);
    float peak = (1.0 - g2) / pow(max(1e-3, 1.0 - g), 3.0);
    return clamp(p / max(peak, 1e-5), 0.0, 1.0);
}

/* Soft billowy stratified fog sheet at FIXED world-space depth plane z (zero depth-contour distortion) */
float fog_sheet_density(vec2 uv, float z, float seed) {
    float persp = 0.35 + (1.0 - z) * 0.95;
    /* Gently stratified horizontal mist banks (~1.7x wider than tall) */
    vec2 p = vec2((uv.x - 0.5) / persp * 0.78, (uv.y - u_horizon_y) / persp * 1.35);
    p = p * (u_noise_scale * 0.52) + vec2(seed * 0.13, -seed * 0.09);

    /* Subtle low-frequency drift warp keeps clouds silky without marbled creases */
    float qx = fbm_wide(p * 0.65, seed) - 0.5;
    float qy = fbm_wide(p * 0.65 + vec2(4.7, 1.9), seed + 17.0) - 0.5;
    vec2 warped = p + vec2(qx * 0.85, qy * 0.45) * u_noise_amount;

    float macro = fbm_wide(warped * 0.82, seed + 31.0);
    float detail = fbm_wide(warped * 1.85, seed + 67.0);
    float cloud = clamp(macro * 0.72 + detail * 0.28, 0.0, 1.0);

    float patchy = smoothstep(0.16 * u_noise_amount, 0.92 - 0.06 * u_noise_amount, cloud);
    float turb = mix(1.0, 0.08 + 1.35 * patchy * patchy, u_noise_amount);

    /* Perspective 3D ceiling at world depth z, softly feathered by cloud billows */
    float ceiling = u_horizon_y + (1.0 - z) * 0.13 + (macro - 0.5) * 0.14 * u_noise_amount;
    float above = max(0.0, ceiling - uv.y);
    float h_att = exp(-u_height_falloff * above * 2.8);

    return turb * h_att;
}

void main() {
    vec4 prev = texture(u_prev, v_uv);
    float m = texture(u_mask, v_uv).r;
    if (m <= 0.0005 || u_density <= 0.0001) {
        frag_color = prev;
        return;
    }

    float d = texture(u_depth, v_uv).r;

    /* 1. Zero foreground bleed: ray only enters fog past depth_min */
    float d_enter = smoothstep(u_depth_min - 0.03, u_depth_min + 0.06, d);
    if (d_enter <= 0.0005) {
        frag_color = prev;
        return;
    }
    float d_exit = (u_depth_max < 0.985)
        ? (1.0 - smoothstep(u_depth_max - 0.08, u_depth_max + 0.04, d))
        : 1.0;

    /* 2. True fixed-plane 5-slice volumetric ray-march between depth_min and depth_max */
    float d_span = max(0.08, u_depth_max - u_depth_min);
    float slice_tau = 0.0;
    float macro_ref = 0.0;
    for (int s = 0; s < 5; s++) {
        float frac = (float(s) + 0.5) / 5.0;
        float z_s = u_depth_min + d_span * frac;
        /* Only integrate fog planes that physically lie IN FRONT of this pixel's surface d */
        float in_front = smoothstep(z_s - 0.065, z_s + 0.055, d);
        if (in_front <= 0.0005) continue;
        float sheet = fog_sheet_density(v_uv, z_s, u_seed + float(s) * 29.0);
        float weight = (0.75 + 0.50 * frac) * 0.22;
        slice_tau += sheet * in_front * weight;
        macro_ref = max(macro_ref, sheet * in_front);
    }

    /* Break circular marker edges organically using accumulated volumetric wisps */
    float edge_m = smoothstep(0.04, 0.90, m + (macro_ref - 0.50) * u_noise_amount * 0.65);
    float m_org = mix(m, m * (0.25 + 1.05 * edge_m), u_noise_amount);

    float tau = u_density * m_org * d_enter * d_exit * slice_tau;
    float trans = exp(-tau);
    float opacity = 1.0 - trans;

    /* 3. Scene-illumination-coupled airlight + directional Henyey-Greenstein sun scatter */
    vec3 amb = texture(u_ambient, v_uv).rgb;
    float amb_lum = dot(amb, vec3(0.2126, 0.7152, 0.0722));
    vec3 local_air = mix(u_airlight, u_airlight * (0.40 + 1.12 * sqrt(max(amb_lum, 0.0))) + amb * 0.28, 0.65);

    float ang = clamp(length(v_uv - u_sun_pos) * 2.2, 0.0, 3.14159265);
    float sun_boost = u_sun_strength * henyey_greenstein(cos(ang), u_sun_g) * (0.35 + 0.65 * sqrt(max(amb_lum, 0.0)));
    vec3 warm = vec3(1.15, 1.02, 0.84);
    vec3 air = local_air + vec3(sun_boost * 0.55) * warm;

    vec3 col = clamp(prev.rgb * trans + air * opacity, 0.0, 1.5);
    float accum = clamp(prev.a + opacity * (1.0 - prev.a), 0.0, 1.0);
    frag_color = vec4(col, accum);
}
