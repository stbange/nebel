in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_flow;  /* Previous advected UV displacement in .rg */
uniform sampler2D u_depth; /* Normalized 3D depth [0=near, 1=far] in .r */
uniform vec2  u_texel;     /* (2/fw, 2/fh) central difference step */
uniform float u_dt;        /* Per-iteration advection step */

void main() {
    vec2 cur = texture(u_flow, v_uv).rg;
    vec2 suv = clamp(v_uv + cur, 0.0, 1.0);

    float dl = texture(u_depth, clamp(suv - vec2(u_texel.x, 0.0), 0.0, 1.0)).r;
    float dr = texture(u_depth, clamp(suv + vec2(u_texel.x, 0.0), 0.0, 1.0)).r;
    float dt = texture(u_depth, clamp(suv - vec2(0.0, u_texel.y), 0.0, 1.0)).r;
    float db = texture(u_depth, clamp(suv + vec2(0.0, u_texel.y), 0.0, 1.0)).r;
    float gx = (dr - dl) * 0.5;
    float gy = (db - dt) * 0.5;

    /* Curl around foreground obstacles + valley pooling + cold-air settling */
    vec2 vel = vec2(-gy * 0.85 + gx * 0.55,
                     gx * 0.85 + gy * 0.55 + 0.025);

    vec2 nxt = clamp(cur + clamp(vel * u_dt, -0.008, 0.008), -0.065, 0.065);
    vec2 prev_f = texture(u_flow, clamp(v_uv - vel * u_dt * 0.5, 0.0, 1.0)).rg;
    vec2 out_f = mix(prev_f, nxt, 0.72);
    frag_color = vec4(out_f, 0.0, 1.0);
}
