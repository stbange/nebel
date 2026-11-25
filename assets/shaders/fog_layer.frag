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
        freq *= 2.03;
        amp *= 0.5;
    }
    return clamp(val, 0.0, 1.0);
}

float henyey_greenstein(float cos_theta, float g) {
    float g2 = g * g;
    float denom = max(1e-4, 1.0 + g2 - 2.0 * g * cos_theta);
    float p = (1.0 - g2) / pow(denom, 1.5);
    float peak = (1.0 - g2) / pow(max(1e-3, 1.0 - g), 3.0);
    return clamp(p / max(peak, 1e-5), 0.0, 1.0);
}

void main() {
    vec4 prev = texture(u_prev, v_uv);
    float m = texture(u_mask, v_uv).r;
    if (m <= 0.0005 || u_density <= 0.0001) {
        frag_color = prev;
        return;
    }

    float d = texture(u_depth, v_uv).r;

    /* 1. True Beer-Lambert depth-integrated slab: ZERO fog in front of depth_min */
    float d_enter = smoothstep(u_depth_min - 0.035, u_depth_min + 0.075, d);
    if (d_enter <= 0.0005) {
        frag_color = prev;
        return;
    }
    float d_exit = (u_depth_max < 0.985)
        ? (1.0 - smoothstep(u_depth_max - 0.08, u_depth_max + 0.04, d))
        : 1.0;
    float d_span = max(0.08, u_depth_max - u_depth_min);
    float d_prog = clamp((d - u_depth_min) / d_span, 0.0, 1.0);
    float depth_weight = d_enter * d_exit * (0.15 + 0.85 * pow(d_prog, 1.3));

    /* 2. 3D depth-sliced volumetric turbulence (each depth plane cuts different wisps) */
    float persp = 0.22 + abs(v_uv.y - u_horizon_y) * 2.4;
    vec2 ground_uv = vec2((v_uv.x - 0.5) / (persp + 0.25), (v_uv.y - u_horizon_y) / (persp + 0.15));
    vec2 p3d = ground_uv * u_noise_scale + vec2(d * 2.3, -d * 1.4);
    float n1 = fbm(p3d, u_seed);
    float n2 = fbm(p3d * 2.1 + vec2(n1 * 1.1, -n1 * 0.8) + vec2(-d * 1.7, d * 2.0), u_seed + 41.0);
    float n = mix(n1, n2, 0.45);

    /* Break circular marker edges into drifting mist tendrils */
    float wispy = smoothstep(0.06, 0.90, m + (n - 0.52) * u_noise_amount * 1.2);
    float m_org = mix(m, m * (0.28 + 1.08 * wispy), u_noise_amount);
    float turb = 1.0 - u_noise_amount * 0.52 + u_noise_amount * 0.98 * n;

    /* 3. Perspective 3D fog ceiling so mist pools in low valleys and clears high peaks/sky */
    float ceiling = u_horizon_y + (1.0 - d) * 0.10;
    float above = max(0.0, ceiling - v_uv.y);
    float h_att = exp(-u_height_falloff * max(0.0, above * 2.8 - (n - 0.48) * 0.22 * u_noise_amount));

    float tau = u_density * m_org * depth_weight * h_att * turb;
    float trans = exp(-tau);
    float opacity = 1.0 - trans;

    /* 4. Scene-illumination-coupled airlight + directional Henyey-Greenstein sun scatter */
    vec3 amb = texture(u_ambient, v_uv).rgb;
    float amb_lum = dot(amb, vec3(0.2126, 0.7152, 0.0722));
    /* Fog catches warm light in sunlit clearings and stays subtle/cool in dark shadows */
    vec3 local_air = mix(u_airlight, u_airlight * (0.38 + 1.15 * sqrt(max(amb_lum, 0.0))) + amb * 0.30, 0.65);

    float ang = clamp(length(v_uv - u_sun_pos) * 2.2, 0.0, 3.14159265);
    float sun_boost = u_sun_strength * henyey_greenstein(cos(ang), u_sun_g) * (0.35 + 0.65 * sqrt(max(amb_lum, 0.0)));
    vec3 warm = vec3(1.15, 1.02, 0.84);
    vec3 air = local_air + vec3(sun_boost * 0.55) * warm;

    vec3 col = clamp(prev.rgb * trans + air * opacity, 0.0, 1.5);
    float accum = clamp(prev.a + opacity * (1.0 - prev.a), 0.0, 1.0);
    frag_color = vec4(col, accum);
}
