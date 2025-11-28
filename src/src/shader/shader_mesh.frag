#version 450

// Fragment shader for mesh shading with colored meshlets

layout(location = 0) in vec3    fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

// Texture sampler is still bound but not used
// (kept for compatibility with descriptor set layout)
layout(binding = 1) uniform sampler2D texSampler;

void main() {
    // Output colored meshlet data directly (color already includes lighting)
    // fragColor contains meshlet color multiplied by light intensity
    outColor = vec4(fragColor, 1.0);
}

