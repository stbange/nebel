in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_orig;  /* Original linear photo */
uniform sampler2D u_fog;   /* Fogged linear RGB + fog opacity in .a */
uniform sampler2D u_bloom; /* Blurred highlight veiling glare */
uniform sampler2D u_depth; /* Normalized 3D depth map */
uniform float u_bloom_amount;
uniform float u_grain_sigma;
uniform float u_split_enabled;
uniform float u_split_pos;
uniform float u_show_depth;
uniform vec2  u_sun_pos;
uniform float u_god_rays;

vec3 linear_to_srgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, lo, step(c, vec3(0.0031308)));
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    if (u_show_depth > 0.5) {
        float d = texture(u_depth, v_uv).r;
        /* Dark navy = foreground/near, teal = midground, bright warm white = far */
        vec3 near_c = vec3(0.05, 0.11, 0.24);
        vec3 mid_c  = vec3(0.18, 0.58, 0.68);
        vec3 far_c  = vec3(0.96, 0.94, 0.86);
        vec3 vis = d < 0.5 ? mix(near_c, mid_c, d * 2.0) : mix(mid_c, far_c, (d - 0.5) * 2.0);
        frag_color = vec4(vis, 1.0);
        return;
    }

    vec3 orig = texture(u_orig, v_uv).rgb;
    vec4 fog = texture(u_fog, v_uv);
    vec3 bloom = texture(u_bloom, v_uv).rgb;

    vec3 col = fog.rgb + bloom * (fog.a * u_bloom_amount * 0.75);

    /* Volumetric crepuscular sunbeams occluded by foreground 3D depth */
    if (u_god_rays > 0.001 && fog.a > 0.001) {
        float d_here = texture(u_depth, v_uv).r;
        float fg_vis = smoothstep(0.18, 0.46, d_here);
        if (fg_vis > 0.001) {
            float jitter = hash12(gl_FragCoord.xy);
            vec2 step_uv = (u_sun_pos - v_uv) * (0.85 / 16.0);
            vec2 s_uv = v_uv + step_uv * jitter;
            float decay = 1.0;
            float shaft = 0.0;
            for (int i = 0; i < 16; i++) {
                s_uv += step_uv;
                if (s_uv.x < 0.0 || s_uv.x > 1.0 || s_uv.y < 0.0 || s_uv.y > 1.0) break;
                float sd = texture(u_depth, s_uv).r;
                vec3 sc = texture(u_orig, s_uv).rgb;
                float slum = dot(sc, vec3(0.2126, 0.7152, 0.0722));
                float open = smoothstep(0.38, 0.68, sd);
                float bright = smoothstep(0.28, 0.88, sqrt(max(slum, 0.0)));
                shaft += open * bright * decay;
                decay *= 0.90;
            }
            float fog_w = smoothstep(0.0, 0.25, fog.a) * (0.30 + 0.70 * fog.a);
            float gain = (shaft / 16.0) * u_god_rays * fog_w * fg_vis * 0.65;
            col += vec3(1.18, 0.98, 0.70) * gain;
        }
    }

    if (u_grain_sigma > 0.00001 && fog.a > 0.001) {
        float g = (hash12(gl_FragCoord.xy) + hash12(gl_FragCoord.xy + vec2(17.1, 31.7)) - 1.0)
                  * u_grain_sigma * fog.a;
        col += vec3(g);
    }

    if (u_split_enabled > 0.5) {
        if (v_uv.x < u_split_pos) {
            col = orig;
        }
        if (abs(v_uv.x - u_split_pos) < 0.0022) {
            frag_color = vec4(0.95, 0.97, 1.0, 1.0);
            return;
        }
    }

    frag_color = vec4(linear_to_srgb(col), 1.0);
}
