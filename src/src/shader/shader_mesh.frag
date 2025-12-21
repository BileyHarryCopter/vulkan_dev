#version 450

// Fragment shader for mesh shading with textures

layout(location = 0) in vec3    fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

void main() {
    // Apply texture to vertex color with lighting
    // fragColor contains vertex color multiplied by light intensity
    outColor = vec4(fragColor, 1.0) * texture(texSampler, fragTexCoord);
}

