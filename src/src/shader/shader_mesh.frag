#version 450

// Fragment shader for mesh shading with texture support

layout(location = 0) in vec3    fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

void main() {
    // Apply lighting to texture
    // fragColor contains light intensity (vec3(lightIntensity))
    // Apply it to the texture sample
    vec4 texColor = texture(texSampler, fragTexCoord);
    outColor = texColor * vec4(fragColor, 1.0);
}

