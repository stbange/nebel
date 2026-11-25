in vec2 v_uv;
in vec2 v_local;
out vec4 frag_color;

uniform int   u_mode;      /* 0=rounded rect, 1=font alpha, 2=rgba icon, 3=ring */
uniform vec2  u_rect_size;
uniform float u_radius;
uniform vec4  u_color;
uniform sampler2D u_tex;

float sd_rounded_box(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    if (u_mode == 1) {
        float a = texture(u_tex, v_uv).r;
        frag_color = vec4(u_color.rgb, u_color.a * a);
        return;
    }
    if (u_mode == 2) {
        vec4 t = texture(u_tex, v_uv);
        frag_color = t * u_color;
        return;
    }
    if (u_mode == 3) {
        vec2 c = v_local - u_rect_size * 0.5;
        float dist = abs(length(c) - (u_radius - 1.5));
        float a = 1.0 - smoothstep(0.8, 2.0, dist);
        frag_color = vec4(u_color.rgb, u_color.a * a);
        return;
    }
    vec2 half_sz = u_rect_size * 0.5;
    float d = sd_rounded_box(v_local - half_sz, half_sz, min(u_radius, min(half_sz.x, half_sz.y)));
    float a = 1.0 - smoothstep(-0.75, 0.75, d);
    frag_color = vec4(u_color.rgb, u_color.a * a);
}
