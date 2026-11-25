layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;

uniform vec4 u_rect;     /* x, y, w, h in top-left pixel coords */
uniform vec2 u_viewport; /* viewport width, height */
uniform vec4 u_uv_rect;  /* u0, v0, u1, v1 */

out vec2 v_uv;
out vec2 v_local;

void main() {
    vec2 px = u_rect.xy + a_pos * u_rect.zw;
    vec2 ndc = vec2(
        (px.x / u_viewport.x) * 2.0 - 1.0,
        1.0 - (px.y / u_viewport.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = mix(u_uv_rect.xy, u_uv_rect.zw, a_uv);
    v_local = a_pos * u_rect.zw;
}
