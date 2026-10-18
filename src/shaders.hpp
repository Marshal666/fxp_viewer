#pragma once
namespace fxp_viewer {
// Reconstruct world viewing direction from a fullscreen quad. Unlike a 2D
// gradient this horizon follows camera rotation and never has positional parallax.
inline constexpr const char* skyboxVertex = R"glsl(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
out vec2 screenPosition;
void main() {
    gl_Position = mvp*vec4(vertexPosition,1.0);
    screenPosition = gl_Position.xy/gl_Position.w;
}
)glsl";
inline constexpr const char* skyboxFragment = R"glsl(#version 330
in vec2 screenPosition;
uniform vec3 cameraForward;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform vec2 viewScale;
uniform vec3 skyColor;
uniform vec3 horizonColor;
uniform vec3 groundColor;
out vec4 finalColor;
void main() {
    vec3 direction = normalize(cameraForward
        + cameraRight*screenPosition.x*viewScale.x
        + cameraUp*screenPosition.y*viewScale.y);
    vec3 upper = mix(horizonColor,skyColor,smoothstep(0.0,0.6,direction.y));
    vec3 lower = mix(horizonColor,groundColor,smoothstep(0.0,0.35,-direction.y));
    finalColor = vec4(direction.y >= 0.0 ? upper : lower,1.0);
}
)glsl";
// Lighting works in linear RGB, then encodes output to sRGB.
// Static and skinned vertices share the fragment shader.
inline constexpr const char* lightingVertex = R"glsl(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform mat4 boneMatrices[128];
uniform int skinned;
out vec3 worldPosition;
out vec3 worldNormal;
out vec2 uv;
out vec4 color;
void main() {
    vec4 p = vec4(vertexPosition,1.0);
    vec3 n = vertexNormal;
    if (skinned != 0) {
        mat4 skin = boneMatrices[int(vertexBoneIndices.x)]*vertexBoneWeights.x
                  + boneMatrices[int(vertexBoneIndices.y)]*vertexBoneWeights.y
                  + boneMatrices[int(vertexBoneIndices.z)]*vertexBoneWeights.z
                  + boneMatrices[int(vertexBoneIndices.w)]*vertexBoneWeights.w;
        p = skin*p;
        n = mat3(skin)*n;
    }
    worldPosition = vec3(matModel*p);
    worldNormal = normalize(mat3(matNormal)*n);
    gl_Position = mvp*p;
    uv = vertexTexCoord;
    color = vertexColor;
}
)glsl";
inline constexpr const char* lightingFragment = R"glsl(#version 330
in vec3 worldPosition;
in vec3 worldNormal;
in vec2 uv;
in vec4 color;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 ambient;
uniform int lightCount;
uniform int mirrored;
// position.w: 0 directional / 1 point; directionRange.w: radius.
uniform vec4 lightPosition[8];
uniform vec4 lightDirectionRange[8];
uniform vec4 lightColor[8];
out vec4 finalColor;
vec3 toLinear(vec3 c) {
    return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));
}
vec3 toSrgb(vec3 c) {
    c = max(c,vec3(0.0));
    return mix(c*12.92,1.055*pow(c,vec3(1.0/2.4))-0.055,step(vec3(0.0031308),c));
}
void main() {
    vec4 tex = texture(texture0,uv);
    float alpha = tex.a*colDiffuse.a*color.a;
    if (alpha < 0.5) discard;
    vec3 normal = normalize(worldNormal);
    if ((!gl_FrontFacing) != (mirrored != 0)) normal = -normal;
    vec3 illumination = ambient;
    for (int i=0; i<lightCount; ++i) {
        vec3 L;
        float attenuation = 1.0;
        if (lightPosition[i].w > 0.5) {
            vec3 delta = lightPosition[i].xyz-worldPosition;
            float d2 = dot(delta,delta);
            float radius = lightDirectionRange[i].w;
            float window = max(1.0-d2/(radius*radius),0.0);
            attenuation = window*window/(1.0+d2);
            L = delta*inversesqrt(max(d2,0.00001));
        } else L = -lightDirectionRange[i].xyz;
        illumination += lightColor[i].rgb*(max(dot(normal,L),0.0)*attenuation);
    }
    vec3 albedo = toLinear(tex.rgb)*toLinear(colDiffuse.rgb)*toLinear(color.rgb);
    finalColor = vec4(toSrgb(albedo*illumination),1.0);
}
)glsl";
}



