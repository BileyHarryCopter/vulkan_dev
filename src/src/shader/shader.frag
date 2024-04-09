#version 450

layout(location = 0)  in vec3    fragColor;
layout(location = 1)  in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;
//  layout(location = 0) out vec3 outColor;

//  layout(binding = 1) uniform sampler2D texSampler;
layout(set = 1, binding = 1) uniform sampler2D texSampler;

layout(push_constant) uniform Push {
    mat4  modelMatrix;
    mat4 normalMatrix;
} push;

void main() {
    outColor = vec4(fragColor, 1.0) * texture(texSampler, fragTexCoord);
    //  outColor = fragColor;
}
