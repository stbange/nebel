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

float hash3d(ivec3 ip, uint seed) {
    uint h = hash_u32(uint(ip.x) * 374761393u +
                      uint(ip.y) * 668265263u +
                      uint(ip.z) * 2147483647u +
                      seed * 1442695041u);
    return float(h & 0x00FFFFFFu) / 16777215.0;
}

float value_noise3d(vec3 p, uint seed) {
    vec3 fl = floor(p);
    ivec3 i = ivec3(fl);
    vec3 f = p - fl;
    vec3 u = f * f * (3.0 - 2.0 * f);

    float n000 = hash3d(i + ivec3(0, 0, 0), seed);
    float n100 = hash3d(i + ivec3(1, 0, 0), seed);
    float n010 = hash3d(i + ivec3(0, 1, 0), seed);
    float n110 = hash3d(i + ivec3(1, 1, 0), seed);
    float n001 = hash3d(i + ivec3(0, 0, 1), seed);
    float n101 = hash3d(i + ivec3(1, 0, 1), seed);
    float n011 = hash3d(i + ivec3(0, 1, 1), seed);
    float n111 = hash3d(i + ivec3(1, 1, 1), seed);

    float nx00 = mix(n000, n100, u.x);
    float nx10 = mix(n010, n110, u.x);
    float nx01 = mix(n001, n101, u.x);
    float nx11 = mix(n011, n111, u.x);
    return mix(mix(nx00, nx10, u.y), mix(nx01, nx11, u.y), u.z);
}

/* Full-range contrast-stretched 3D fBm (compensates for 3D trilinear variance narrowing!) */
float fbm3d_2(vec3 p, uint seed) {
    float v = 0.667 * value_noise3d(p, seed)
            + 0.333 * value_noise3d(p * 2.07, seed + 97u);
    return clamp((v - 0.34) / 0.32, 0.0, 1.0);
}

float fbm3d_3(vec3 p, uint seed) {
    float v = 0.560 * value_noise3d(p, seed)
            + 0.290 * value_noise3d(p * 2.07, seed + 97u)
            + 0.150 * value_noise3d(p * 4.19, seed + 194u);
    return clamp((v - 0.35) / 0.30, 0.0, 1.0);
}

float henyey_greenstein(float cos_theta, float g) {
    float g2 = g * g;
    float denom = max(1e-4, 1.0 + g2 - 2.0 * g * cos_theta);
    float p = (1.0 - g2) / pow(denom, 1.5);
    float peak = (1.0 - g2) / pow(max(1e-3, 1.0 - g), 3.0);
    return clamp(p / max(peak, 1e-5), 0.0, 1.0);
}

/* Depth-coherent 3D world coordinates (prevents CLT washout across 16-24 slices!) */
vec3 fog_world_coord(vec2 uv, float z, uint seed) {
    float persp = 0.56 + (1.0 - z) * 0.64;
    float s_f = float(seed % 10000u);
    float sc = u_noise_scale * 0.56;
    return vec3(
        (uv.x - 0.5) / persp * 0.80 * sc + s_f * 0.13,
        (uv.y - u_horizon_y) / persp * 1.40 * sc - s_f * 0.09,
        z * 1.25 + s_f * 0.07
    );
}

/* Sculpted 3D volumetric fog field with Nubis-style wispy erosion: returns vec2(density, crest) */
vec2 fog_volume_3d(vec2 uv, float z, uint seed) {
    vec3 p = fog_world_coord(uv, z, seed);

    /* Swirling domain warp creates organic curling wind ribbons */
    float qx = fbm3d_2(p * 0.58, seed) - 0.5;
    float qy = fbm3d_2(p * 0.58 + vec3(4.7, 1.9, 2.3), seed + 17u) - 0.5;
    vec3 warped = p + vec3(qx * 1.35, qy * 0.72, qx * 0.32) * u_noise_amount;

    /* Coherent billowing banks eroded by crisp mid + fine curling tendrils */
    float macro = fbm3d_3(warped * 0.78, seed + 31u);
    float detail = fbm3d_2(vec3(warped.xy * 2.35, warped.z * 2.10), seed + 67u);
    float fine = clamp((value_noise3d(vec3(warped.xy * 4.4, warped.z * 3.2), seed + 131u) - 0.30) / 0.40, 0.0, 1.0);
    float wisp = 1.0 - abs(detail * 2.0 - 1.0);
    float eroded = clamp(macro - (1.0 - detail) * 0.26 * u_noise_amount
                         + (wisp * 0.14 + (fine - 0.5) * 0.16) * u_noise_amount, 0.0, 1.0);

    float patchy = smoothstep(0.10 * u_noise_amount, 0.86 - 0.08 * u_noise_amount, eroded);
    float billow = patchy * sqrt(max(patchy, 0.0001));
    /* Realistic translucent mist veil (0.24) + dense sculpted billowing banks (+1.85) */
    float turb = mix(1.0, 0.24 + 1.85 * billow, u_noise_amount);

    float ceiling = u_horizon_y + (1.0 - z) * 0.14 + (macro - 0.5) * 0.24 * u_noise_amount;
    float above = max(0.0, ceiling - uv.y);
    float h_att = exp(-u_height_falloff * above * 2.8);

    float crest = clamp(1.0 - max(0.0, uv.y - ceiling) * 3.4, 0.0, 1.0) * (0.35 + 0.65 * eroded);
    return vec2(turb * h_att, crest);
}

/* Fast 3D macro density probe for directional self-shadowing inside fog billows */
float fog_volume_macro_3d(vec2 uv, float z, uint seed) {
    vec3 p = fog_world_coord(uv, z, seed);
    float qx = fbm3d_2(p * 0.58, seed) - 0.5;
    float qy = fbm3d_2(p * 0.58 + vec3(4.7, 1.9, 2.3), seed + 17u) - 0.5;
    vec3 warped = p + vec3(qx * 1.35, qy * 0.72, qx * 0.32) * u_noise_amount;
    float macro = fbm3d_2(warped * 0.78, seed + 31u);
    float patchy = smoothstep(0.10 * u_noise_amount, 0.86 - 0.08 * u_noise_amount, macro);
    float billow = patchy * sqrt(max(patchy, 0.0001));
    float turb = mix(1.0, 0.24 + 1.85 * billow, u_noise_amount);
    float ceiling = u_horizon_y + (1.0 - z) * 0.14 + (macro - 0.5) * 0.24 * u_noise_amount;
    float above = max(0.0, ceiling - uv.y);
    return turb * exp(-u_height_falloff * above * 2.8);
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

    /* Subtle sub-slice anti-aliasing jitter (preserves crisp 3D billows!) + full shadow ray jitter */
    float jitter = 0.5;
    float sh_jit = 0.5;
    if (u_sample_idx > 0) {
        ivec2 ip = ivec2(gl_FragCoord.xy);
        uint h1 = hash_u32(uint(ip.x) * 374761393u + uint(ip.y) * 668265263u + uint(u_sample_idx) * 2246822519u);
        uint h2 = hash_u32(h1 ^ 0x9e3779b9u);
        jitter = 0.5 + (float(h1 & 0xFFFFu) / 65536.0 - 0.5) * 0.35;
        sh_jit = float(h2 & 0xFFFFu) / 65536.0;
    }

    float px_lum = clamp(dot(prev.rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.5);
    float darkness = 1.0 - smoothstep(0.06, 0.78, sqrt(max(px_lum, 0.0)));
    float pool_factor = mix(1.0, 0.35 + 1.15 * darkness, u_shadow_pool);

    vec3 amb = texture(u_ambient, v_uv).rgb;
    float amb_lum = clamp(dot(amb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.5);
    float s_lum = sqrt(max(amb_lum, 0.0));

    vec2 to_sun = u_sun_pos - v_uv;
    float sun_dist = length(to_sun);
    vec2 sun_dir = (sun_dist > 0.001) ? (to_sun / sun_dist) : vec2(0.0, -1.0);
    float ang = clamp(sun_dist * 2.2, 0.0, 3.14159265);
    float cos_th = cos(ang);
    float p1 = henyey_greenstein(cos_th, u_sun_g);
    float p2 = henyey_greenstein(cos_th, u_sun_g * 0.45);
    vec3 warm = vec3(1.18, 1.03, 0.80);

    float d_span = max(0.08, u_depth_max - u_depth_min);
    float slice_norm = 1.12 / float(slices);
    uint base_seed = uint(max(u_seed, 0));

    /* True front-to-back volumetric radiative transfer with directional cloud self-shadowing */
    float T = 1.0;
    vec3 accum_L = vec3(0.0);

    for (int s = 0; s < 32; s++) {
        if (s >= slices || T < 0.004) break;
        float frac = (float(s) + jitter) / float(slices);
        float z_s = u_depth_min + d_span * frac;
        float in_front = smoothstep(z_s - 0.055, z_s + 0.045, d);
        if (in_front <= 0.0005) continue;

        vec2 vol = fog_volume_3d(adv_uv, z_s, base_seed);
        float sheet = vol.x;
        float crest = vol.y;
        float weight = (0.76 + 0.48 * frac) * slice_norm;

        /* Strong organic 3D feathered brush silhouette per depth slice */
        float edge_s = smoothstep(0.04, 0.88, m + (sheet - 0.72) * u_noise_amount * 0.88);
        float m_org_s = mix(m, m * (0.20 + 1.18 * edge_s), u_noise_amount);

        float dt_s = u_density * m_org_s * d_enter * d_exit * pool_factor * sheet * in_front * weight;
        if (dt_s <= 0.0001) continue;

        /* 1. Terrain 3D heightfield shadow cone (local ridges & trees cast volumetric shafts) */
        float terrain_vis = 1.0;
        if (do_shadows) {
            float occ = 0.0;
            for (int k = 0; k < 16; k++) {
                if (k >= sh_steps) break;
                float tk = ((float(k) + sh_jit) / float(sh_steps)) * 0.26;
                vec2 suv = clamp(mix(v_uv, u_sun_pos, tk), 0.0, 1.0);
                float sd = texture(u_depth, suv).r;
                occ += smoothstep(z_s - 0.02, z_s - 0.09, sd);
            }
            terrain_vis = exp(-(occ / float(sh_steps)) * 1.75);
        }

        /* 2. Directional cloud self-shadowing + Beer-Powder sunlit rim relief */
        vec2 sun_uv = clamp(adv_uv + sun_dir * 0.042 + vec2(0.0, -0.022), 0.0, 1.0);
        float sheet_sun = fog_volume_macro_3d(sun_uv, max(0.0, z_s - 0.045), base_seed);
        float self_shadow = exp(-sheet_sun * u_density * (0.25 + 0.65 * u_noise_amount));
        float powder = 1.0 - 0.62 * exp(-sheet * u_density * 1.8);
        float slice_vis = clamp(terrain_vis * (0.28 + 0.72 * self_shadow), 0.0, 1.0);

        /* 3. Sculpted 3D slice radiance: luminous sunlit crests vs cool shaded bellies */
        vec3 shadow_col = vec3(0.60, 0.72, 0.90);
        vec3 sunlit_col = vec3(1.22, 1.15, 1.04);
        vec3 relief_tint = mix(shadow_col, sunlit_col, clamp(slice_vis * 0.72 + crest * 0.34, 0.0, 1.0));
        /* Aerial depth perspective: near mist wisps crisp & luminous, far banks cooler */
        vec3 depth_tint = mix(vec3(1.06, 1.03, 1.00), vec3(0.90, 0.96, 1.04), frac);
        vec3 tinted_air = u_airlight * relief_tint * depth_tint;
        vec3 local_air = mix(tinted_air, tinted_air * (0.40 + 1.16 * s_lum) + amb * 0.28, 0.60);

        float scatter = p1 * slice_vis * (0.82 + 0.52 * powder)
                      + p2 * (0.32 + 0.68 * slice_vis) * 0.36;
        float sun_boost = u_sun_strength * scatter * (0.38 + 0.62 * s_lum);
        vec3 slice_air = local_air + vec3(sun_boost * 0.64) * warm;

        float step_op = 1.0 - exp(-dt_s);
        accum_L += T * step_op * slice_air;
        T *= (1.0 - step_op);
    }

    float opacity = 1.0 - T;

    /* Intrinsic shadow softening (de-lighting harsh sun shadows inside thick fog) */
    vec3 base_rgb = prev.rgb;
    if (u_delight_strength > 0.001 && opacity > 0.001) {
        float dl_w = clamp(u_delight_strength * opacity * 0.56, 0.0, 0.60);
        float soft_lum = mix(px_lum, amb_lum, 0.65);
        float l_scale = clamp(soft_lum / max(px_lum, 0.04), 0.55, 2.2);
        vec3 delit = prev.rgb * l_scale * 0.78 + amb * 0.22;
        base_rgb = clamp(mix(prev.rgb, delit, dl_w), 0.0, 1.5);
    }

    vec3 col = clamp(base_rgb * T + accum_L, 0.0, 1.5);
    float accum = clamp(prev.a + opacity * (1.0 - prev.a), 0.0, 1.0);
    frag_color = vec4(col, accum);
}
