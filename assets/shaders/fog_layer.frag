in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_prev;  /* Current accumulated linear RGB + fog opacity in .a */
uniform sampler2D u_depth; /* Normalized depth [0,1] in .r */
uniform sampler2D u_mask;  /* Guided-Filter refined layer mask [0,1] in .r */

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
    float d_gate = smoothstep(u_depth_min - 0.08, u_depth_min + 0.12, d);
    if (u_depth_max < 0.999) {
        d_gate *= 1.0 - smoothstep(u_depth_max - 0.08, u_depth_max + 0.08, d);
    }

    float height_above = max(0.0, u_horizon_y - v_uv.y);
    float h_att = exp(-u_height_falloff * height_above * 2.5);

    float persp = 0.25 + abs(v_uv.y - u_horizon_y) * 2.5;
    vec2 ground_uv = vec2((v_uv.x - 0.5) / (persp + 0.25), (v_uv.y - u_horizon_y) / (persp + 0.15));
    float n1 = fbm(ground_uv * u_noise_scale, u_seed);
    float n2 = fbm(ground_uv * (u_noise_scale * 1.9) + vec2(n1 * 0.9, -n1 * 0.6), u_seed + 41.0);
    float n = mix(n1, n2, 0.45);
    /* Break circular marker edges into organic drifting mist wisps */
    float wispy = smoothstep(0.06, 0.92, m + (n - 0.52) * u_noise_amount * 1.15);
    float m_org = mix(m, m * (0.30 + 1.05 * wispy), u_noise_amount);
    float turb = 1.0 - u_noise_amount * 0.55 + u_noise_amount * 0.95 * n;

    float tau = u_density * m_org * (0.08 + 0.92 * d_gate) * h_att * turb;
    float trans = exp(-tau);
    float opacity = 1.0 - trans;

    float ang = clamp(length(v_uv - u_sun_pos) * 2.2, 0.0, 3.14159265);
    float sun_boost = u_sun_strength * henyey_greenstein(cos(ang), u_sun_g);
    vec3 warm = vec3(1.12, 1.03, 0.88);
    vec3 air = u_airlight + vec3(sun_boost * 0.45) * warm;

    vec3 col = clamp(prev.rgb * trans + air * opacity, 0.0, 1.5);
    float accum = clamp(prev.a + opacity * (1.0 - prev.a), 0.0, 1.0);
    frag_color = vec4(col, accum);
}
