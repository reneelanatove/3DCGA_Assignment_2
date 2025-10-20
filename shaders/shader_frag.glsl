#version 410

layout(std140) uniform Material // Must match the GPUMaterial defined in src/mesh.h
{
    vec3 kd;
	vec3 ks;
	float shininess;
	float transparency;
};

uniform sampler2D colorMap;
uniform bool hasTexCoords;
uniform bool useMaterial;
uniform bool enableLambert;
uniform vec3 lambertDiffuseColor;

const int MAX_LIGHTS = 8;
uniform int numLights;
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform int lightIsSpotlight[MAX_LIGHTS];
uniform vec3 lightDirections[MAX_LIGHTS];
uniform float lightSpotCosCutoff[MAX_LIGHTS];
uniform float lightSpotSoftness[MAX_LIGHTS];

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec3 normal = normalize(fragNormal);

    vec3 baseColor;
    if (hasTexCoords)
        baseColor = texture(colorMap, fragTexCoord).rgb;
    else if (useMaterial)
        baseColor = kd;
    else
        baseColor = enableLambert ? lambertDiffuseColor : normal;

    if (enableLambert && numLights > 0) {
        vec3 lambertSum = vec3(0);
        int lightCount = min(numLights, MAX_LIGHTS);
        for (int i = 0; i < lightCount; ++i) {
            vec3 lightDir = normalize(lightPositions[i] - fragPosition);
            float diff = max(dot(normal, lightDir), 0.0);

            float spotFactor = 1.0;
            if (lightIsSpotlight[i] != 0) {
                float c = dot(-lightDir, normalize(lightDirections[i]));
                spotFactor = smoothstep(lightSpotCosCutoff[i], lightSpotCosCutoff[i] + lightSpotSoftness[i], c);
            }

            lambertSum += baseColor * diff * lightColors[i] * spotFactor;
        }
        fragColor = vec4(lambertSum, 1);
    } else {
        fragColor = vec4(baseColor, 1);
    }
}
