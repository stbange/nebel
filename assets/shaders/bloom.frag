in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_tex;
uniform vec2 u_dir;     /* (1/w, 0) or (0, 1/h) */
uniform int  u_extract; /* 1 on first pass to threshold highlights */

void main() {
    const float w0 = 0.227027;
    const float w1 = 0.1945946;
    const float w2 = 0.1216216;
    const float w3 = 0.054054;
    const float w4 = 0.016216;

    vec3 sum = vec3(0.0);
    float weights[5] = float[5](w0, w1, w2, w3, w4);
    for (int i = -4; i <= 4; i++) {
        float wt = weights[abs(i)];
        vec3 c = texture(u_tex, clamp(v_uv + u_dir * float(i) * 2.5, 0.0, 1.0)).rgb;
        if (u_extract == 1) {
            float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
            c *= smoothstep(0.55, 1.0, lum);
        }
        sum += c * wt;
    }
    frag_color = vec4(sum, 1.0);
}
