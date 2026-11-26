in vec2 v_uv;
in vec2 v_local;
out vec4 frag_color;

uniform sampler2D u_prev;    /* Current accumulated linear RGB + fog opacity in .a */
uniform sampler2D u_depth;   /* Normalized 3D depth [0=near, 1=far] in .r */
uniform sampler2D u_mask;    /* Guided-Filter refined painted fog mask [0,1] in .r */
uniform sampler2D u_ambient; /* Wide blurred scene illumination map (linear RGB) */
uniform sampler2D u_flow;    /* Terrain-guided fluid advection displacement (.rg) */

uniform vec2  u_tile_size;   /* (0,0) for full-frame, (tw,th) for Desktop tiled GPU export */
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
uniform float u_shadow_pool;
uniform float u_fluid_flow;
uniform float u_delight_strength;
uniform float u_god_rays_hint;
uniform int   u_seed;
uniform int   u_ray_slices;
uniform int   u_shadow_steps;
uniform int   u_sample_idx;

uint hash_u32(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

float hash2d(ivec2 ip, uint seed) {
    uint h = hash_u32(uint(ip.x) * 374761393u + uint(ip.y) * 668265263u + seed * 1442695041u);
    return float(h & 0x00FFFFFFu) / 16777215.0;
}

float value_noise(vec2 p, uint seed) {
    vec2 fl = floor(p);
    ivec2 i = ivec2(fl);
    vec2 f = p - fl;
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash2d(i,                seed);
    float b = hash2d(i + ivec2(1, 0),  seed);
    float c = hash2d(i + ivec2(0, 1),  seed);
    float d = hash2d(i + ivec2(1, 1),  seed);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p, uint seed) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 4; i++) {
        val += amp * value_noise(p * freq, seed + uint(i) * 97u);
        freq *= 2.03;
        amp *= 0.5;
    }
    return clamp(val, 0.0, 1.0);
}

float fbm_wide(vec2 p, uint seed) {
    return clamp((fbm(p, seed) - 0.28) / 0.44, 0.0, 1.0);
}

float henyey_greenstein(float cos_theta, float g) {
    float g2 = g * g;
    float denom = max(1e-4, 1.0 + g2 - 2.0 * g * cos_theta);
    float p = (1.0 - g2) / pow(denom, 1.5);
    float peak = (1.0 - g2) / pow(max(1e-3, 1.0 - g), 3.0);
    return clamp(p / max(peak, 1e-5), 0.0, 1.0);
}

/* Soft billowy stratified fog sheet at FIXED world-space depth plane z */
float fog_sheet_density(vec2 uv, float z, uint seed) {
    float persp = 0.35 + (1.0 - z) * 0.95;
    float s_f = float(seed % 10000u);
    vec2 p = vec2((uv.x - 0.5) / persp * 0.78, (uv.y - u_horizon_y) / persp * 1.35);
    p = p * (u_noise_scale * 0.52) + vec2(s_f * 0.13, -s_f * 0.09);

    float qx = fbm_wide(p * 0.65, seed) - 0.5;
    float qy = fbm_wide(p * 0.65 + vec2(4.7, 1.9), seed + 17u) - 0.5;
    vec2 warped = p + vec2(qx * 0.85, qy * 0.45) * u_noise_amount;

    float macro = fbm_wide(warped * 0.82, seed + 31u);
    float detail = fbm_wide(warped * 1.85, seed + 67u);
    float cloud = clamp(macro * 0.72 + detail * 0.28, 0.0, 1.0);

    float patchy = smoothstep(0.16 * u_noise_amount, 0.92 - 0.06 * u_noise_amount, cloud);
    float turb = mix(1.0, 0.08 + 1.35 * patchy * patchy, u_noise_amount);

    float ceiling = u_horizon_y + (1.0 - z) * 0.13 + (macro - 0.5) * 0.14 * u_noise_amount;
    float above = max(0.0, ceiling - uv.y);
    float h_att = exp(-u_height_falloff * above * 2.8);

    return turb * h_att;
}

void main() {
    vec2 prev_uv = (u_tile_size.x > 0.5)
        ? vec2(v_local.x / u_tile_size.x, 1.0 - v_local.y / u_tile_size.y)
        : v_uv;
    vec4 prev = texture(u_prev, prev_uv);
    float m = texture(u_mask, v_uv).r;
    if (m <= 0.0005 || u_density <= 0.0001) {
        frag_color = prev;
        return;
    }

    float d = texture(u_depth, v_uv).r;
    float d_enter = smoothstep(u_depth_min - 0.03, u_depth_min + 0.06, d);
    if (d_enter <= 0.0005) {
        frag_color = prev;
        return;
    }
    float d_exit = (u_depth_max < 0.985)
        ? (1.0 - smoothstep(u_depth_max - 0.08, u_depth_max + 0.04, d))
        : 1.0;

    vec2 flow = (u_fluid_flow > 0.001) ? texture(u_flow, v_uv).rg : vec2(0.0);
    vec2 adv_uv = clamp(v_uv + flow * u_fluid_flow, 0.0, 1.0);

    int slices = clamp(u_ray_slices, 5, 32);
    int sh_steps = clamp(u_shadow_steps, 0, 16);
    bool do_shadows = (sh_steps > 0);

    /* Progressive stochastic jitter when u_sample_idx > 0 (0 = deterministic center matching CPU) */
    float jitter = 0.5;
    float sh_jit = 0.5;
    if (u_sample_idx > 0) {
        ivec2 ip = ivec2(gl_FragCoord.xy);
        uint h1 = hash_u32(uint(ip.x) * 374761393u + uint(ip.y) * 668265263u + uint(u_sample_idx) * 2246822519u);
        uint h2 = hash_u32(h1 ^ 0x9e3779b9u);
        jitter = float(h1 & 0xFFFFu) / 65536.0;
        sh_jit = float(h2 & 0xFFFFu) / 65536.0;
    }

    float d_span = max(0.08, u_depth_max - u_depth_min);
    float slice_norm = 1.10 / float(slices);
    float slice_tau = 0.0;
    float lit_tau = 0.0;
    float macro_ref = 0.0;
    uint base_seed = uint(max(u_seed, 0));

    for (int s = 0; s < 32; s++) {
        if (s >= slices) break;
        float frac = (float(s) + jitter) / float(slices);
        float z_s = u_depth_min + d_span * frac;
        float in_front = smoothstep(z_s - 0.065, z_s + 0.055, d);
        if (in_front <= 0.0005) continue;

        uint s_seed = base_seed + uint((s * 5) / slices) * 29u;
        float sheet = fog_sheet_density(adv_uv, z_s, s_seed);
        float weight = (0.75 + 0.50 * frac) * slice_norm;
        float dt_s = sheet * in_front * weight;

        float sun_vis = 1.0;
        if (do_shadows) {
            float occ = 0.0;
            for (int k = 0; k < 16; k++) {
                if (k >= sh_steps) break;
                float tk = ((float(k) + sh_jit) / float(sh_steps)) * 0.42;
                vec2 suv = clamp(mix(v_uv, u_sun_pos, tk), 0.0, 1.0);
                float sd = texture(u_depth, suv).r;
                occ += smoothstep(z_s - 0.02, z_s - 0.09, sd);
            }
            sun_vis = exp(-(occ / float(sh_steps)) * 2.4);
        }

        slice_tau += dt_s;
        lit_tau += dt_s * sun_vis;
        macro_ref = max(macro_ref, sheet * in_front);
    }

    float edge_m = smoothstep(0.04, 0.90, m + (macro_ref - 0.50) * u_noise_amount * 0.65);
    float m_org = mix(m, m * (0.25 + 1.05 * edge_m), u_noise_amount);

    float px_lum = clamp(dot(prev.rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.5);
    float darkness = 1.0 - smoothstep(0.06, 0.78, sqrt(max(px_lum, 0.0)));
    float pool_factor = mix(1.0, 0.35 + 1.15 * darkness, u_shadow_pool);

    float tau = u_density * m_org * d_enter * d_exit * slice_tau * pool_factor;
    float trans = exp(-tau);
    float opacity = 1.0 - trans;

    vec3 amb = texture(u_ambient, v_uv).rgb;
    float amb_lum = clamp(dot(amb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.5);
    float s_lum = sqrt(max(amb_lum, 0.0));

    /* Intrinsic shadow softening (de-lighting harsh sun shadows inside thick fog) */
    vec3 base_rgb = prev.rgb;
    if (u_delight_strength > 0.001 && opacity > 0.001) {
        float dl_w = clamp(u_delight_strength * opacity * 0.48, 0.0, 0.55);
        float soft_lum = mix(px_lum, amb_lum, 0.65);
        float l_scale = clamp(soft_lum / max(px_lum, 0.04), 0.55, 2.2);
        vec3 delit = prev.rgb * l_scale * 0.78 + amb * 0.22;
        base_rgb = clamp(mix(prev.rgb, delit, dl_w), 0.0, 1.5);
    }

    /* Multi-octave Mie multiple scattering modulated by 3D volumetric sun visibility */
    float col_sun_vis = (slice_tau > 0.0001) ? clamp(lit_tau / slice_tau, 0.0, 1.0) : 1.0;
    float ang = clamp(length(v_uv - u_sun_pos) * 2.2, 0.0, 3.14159265);
    float cos_th = cos(ang);
    float p1 = henyey_greenstein(cos_th, u_sun_g);
    float p2 = henyey_greenstein(cos_th, u_sun_g * 0.45);
    float ms_factor = 1.0 - exp(-tau * 1.4);
    float scatter = p1 * col_sun_vis * 0.72
                  + p2 * (0.35 + 0.65 * col_sun_vis) * 0.38
                  + 0.28 * ms_factor * 0.28;
    float sun_boost = u_sun_strength * scatter * (0.35 + 0.65 * s_lum);

    vec3 tint = mix(vec3(0.80, 0.88, 0.98), vec3(1.04, 1.01, 0.97), col_sun_vis);
    vec3 tinted_air = u_airlight * tint;
    vec3 local_air = mix(tinted_air, tinted_air * (0.40 + 1.12 * s_lum) + amb * 0.28, 0.65);
    vec3 warm = vec3(1.15, 1.02, 0.84);
    vec3 air = local_air + vec3(sun_boost * 0.55) * warm;

    vec3 col = clamp(base_rgb * trans + air * opacity, 0.0, 1.5);
    float accum = clamp(prev.a + opacity * (1.0 - prev.a), 0.0, 1.0);
    frag_color = vec4(col, accum);
}
