#version 450

//  const int MAX_TEXTURES = 16;

layout(location = 0) in vec3    fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

// Push constant contains object index and material index
layout(push_constant) uniform Push {
    uint objectIndex;
    uint materialIndex;
    uint padding[2];  // Padding to align to 16 bytes
} push;

layout(location = 0) in vec3    fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragSpecularColor;
layout(location = 3) in float fragShininess;
layout(location = 4) in float fragDissolve;

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 finalColor = fragColor * texColor.rgb;
    
    // Apply dissolve (transparency)
    float alpha = texColor.a * fragDissolve;
    
    outColor = vec4(finalColor, alpha);
}
