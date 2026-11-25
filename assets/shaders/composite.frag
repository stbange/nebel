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
