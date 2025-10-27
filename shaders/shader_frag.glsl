#version 410

layout(std140) uniform Material // Must match the GPUMaterial defined in src/mesh.h
{
    vec3 kd;
	vec3 ks;
	float shininess;
	float transparency;
};

uniform sampler2D colorMap;
uniform sampler2D shadowMap;
uniform bool hasTexCoords;
uniform bool useMaterial;
uniform int shadingMode;
uniform vec3 customDiffuseColor;
uniform vec3 viewPosition;
uniform vec3 specularColor;
uniform float specularStrength;
uniform float specularShininess;

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
        baseColor = customDiffuseColor;

    if (shadingMode == 0 || numLights <= 0) {
        fragColor = vec4(baseColor, 1);
        return;
    }

    vec3 viewDir = normalize(viewPosition - fragPosition);

    vec3 colorAccum = vec3(0);
    vec3 specAccum = vec3(0);
    float exponent = specularShininess > 0.0 ? specularShininess : shininess;
    int lightCount = min(numLights, MAX_LIGHTS);
    for (int i = 0; i < lightCount; ++i) {
        vec3 lightDir = normalize(lightPositions[i] - fragPosition);
        float diff = max(dot(normal, lightDir), 0.0);

        float spotFactor = 1.0;
        if (lightIsSpotlight[i] != 0) {
            float c = dot(-lightDir, normalize(lightDirections[i]));
            spotFactor = smoothstep(lightSpotCosCutoff[i], lightSpotCosCutoff[i] + lightSpotSoftness[i], c);
        }

        vec3 lightContribution = lightColors[i] * spotFactor;
        colorAccum += baseColor * diff * lightContribution;

        if (shadingMode == 2 && diff > 0.0) {
            vec3 reflectDir = reflect(-lightDir, normal);
            float spec = max(dot(reflectDir, viewDir), 0.0);
            spec = pow(spec, exponent);
            specAccum += lightContribution * spec;
        }
    }

    vec3 finalColor = colorAccum;
    if (shadingMode == 2) {
        vec3 specColor = specularColor * specularStrength;
        if (useMaterial)
            specColor *= ks;

        finalColor += specColor * specAccum;
    }

    fragColor = vec4(finalColor, 1);
}
