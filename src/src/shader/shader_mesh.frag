#version 450

// Fragment shader for mesh shading - doesn't use textures
// Just outputs the color from the mesh shader

layout(location = 0) in vec3    fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    // For mesh shading, just output the color directly without texture
    // This allows mesh shaders to work without requiring texture binding
    outColor = vec4(fragColor, 1.0);
}

