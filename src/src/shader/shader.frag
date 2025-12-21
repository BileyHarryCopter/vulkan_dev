#version 450

//  const int MAX_TEXTURES = 16;

layout(location = 0) in vec3    fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

// Push constant now only contains object index (matrices are in storage buffer)
layout(push_constant) uniform Push {
    uint objectIndex;
    uint padding[3];  // Padding to align to 16 bytes
} push;

void main() {
    outColor = vec4(fragColor, 1.0) * texture(texSampler, fragTexCoord);
}
