#version 450

layout(location = 0) in  vec3  position;
layout(location = 1) in  vec3     color;
layout(location = 2) in  vec3    normal;
layout(location = 3) in  vec2        uv;


layout(location = 0) out vec3    fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragSpecularColor;
layout(location = 3) out float fragShininess;
layout(location = 4) out float fragDissolve;


layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionViewMatrix;
    vec3     directionToLight;
} ubo;

// Push constant contains object index and material index
layout(push_constant) uniform Push {
    uint objectIndex;
    uint materialIndex;
    uint padding[2];  // Padding to align to 16 bytes
} push;

// Object matrices storage buffer (shared across all objects)
struct ObjectMatrices {
    mat4 modelMatrix;
    mat4 normalMatrix;
};

layout(std430, set = 0, binding = 6) readonly buffer ObjectMatricesBuffer {
    ObjectMatrices matrices[];
};

// Material structure
struct Material {
    vec3 diffuseColor;
    vec3 specularColor;
    float shininess;
    float dissolve;
};

// Materials storage buffer
layout(std430, set = 0, binding = 7) readonly buffer MaterialsBuffer {
    Material materials[];
};

const float AMBIENT = 0.3;

void main() {
    // Read object matrices from storage buffer
    ObjectMatrices objectMatrices = matrices[push.objectIndex];
    
    gl_Position = ubo.projectionViewMatrix * objectMatrices.modelMatrix * vec4(position, 1.0);     //  homogeneous coordinate

    vec3 normalWorldSpace = normalize(mat3(objectMatrices.normalMatrix) * normal);

    // Get material
    Material material = materials[push.materialIndex];
    
    // Use material diffuse color if vertex color is white (default), otherwise use vertex color
    vec3 finalColor = (color == vec3(1.0)) ? material.diffuseColor : color * material.diffuseColor;

    float lightIntensity  = AMBIENT + max(dot (normalWorldSpace, ubo.directionToLight), 0);

    fragColor    = lightIntensity * finalColor;
    fragTexCoord =                     uv;
    fragSpecularColor = material.specularColor;
    fragShininess = material.shininess;
    fragDissolve = material.dissolve;
}
