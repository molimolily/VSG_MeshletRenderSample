#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in VertexInput {
  vec3 normal;
  vec2 uv;
  flat uint meshletID;
} vertexInput;

layout(location = 0) out vec4 outFragColor;

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 intToColor(uint value, float period) {
    float hue = mod(float(value),period);
    float h = hue / period;
    float s = 1.0;
    float v = 1.0;
    return hsv2rgb(vec3(h, s, v));
}

void main()
{
    vec3 col = intToColor(vertexInput.meshletID, 6.0);
    outFragColor = vec4(col, 1.0);
}
