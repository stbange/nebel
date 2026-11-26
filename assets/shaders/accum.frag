in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_accum;  /* Running accumulated linear RGBA */
uniform sampler2D u_sample; /* Latest progressive volumetric sample */
uniform float u_weight;     /* 1.0 / sample_index */

void main() {
    vec4 prev = texture(u_accum, v_uv);
    vec4 samp = texture(u_sample, v_uv);
    frag_color = mix(prev, samp, u_weight);
}
