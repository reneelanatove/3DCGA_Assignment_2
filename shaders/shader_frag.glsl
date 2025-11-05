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
uniform int shadingMode;
uniform vec3 customDiffuseColor;
uniform vec3 viewPosition;
uniform vec3 specularColor;
uniform float specularStrength;
uniform float specularShininess;
uniform vec3 ambientLight;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float sunIntensity;

const int MAX_LIGHTS = 8;
uniform int numLights;
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform int lightIsSpotlight[MAX_LIGHTS];
uniform vec3 lightDirections[MAX_LIGHTS];
uniform float lightSpotCosCutoff[MAX_LIGHTS];
uniform float lightSpotSoftness[MAX_LIGHTS];

uniform bool shadowsEnabled;
uniform mat4 lightMVP;
uniform sampler2D shadowMap;
uniform bool pcf;

uniform bool useNormalMap;
uniform sampler2D normalMap;

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec3 normal = normalize(fragNormal);

    if (useNormalMap && hasTexCoords) {
        vec3 normalMapValue = texture(normalMap, fragTexCoord).xyz;
        normal = normalize(normalMapValue * 2.0 - 1.0);
    }

    vec3 baseColor = vec3(0.0);
    if (hasTexCoords)
        baseColor = texture(colorMap, fragTexCoord).rgb;
    else if (useMaterial)
        baseColor = kd;
    else
        baseColor = customDiffuseColor;

    if (shadingMode == 0) {
        fragColor = vec4(baseColor, 1);
        return;
    }

    vec3 viewDir = normalize(viewPosition - fragPosition);

    const float epsilon = 1e-4;
    vec3 colorAccum = ambientLight * baseColor;
    vec3 specAccum = vec3(0);
    float exponent = specularShininess > 0.0 ? specularShininess : shininess;
    if (sunIntensity > epsilon && length(sunColor) > epsilon) {
        vec3 dirToSun = normalize(-sunDirection);
        float sunDiff = max(dot(normal, dirToSun), 0.0);
        vec3 sunContribution = baseColor * sunColor * sunIntensity * sunDiff;
        colorAccum += sunContribution;

        if (shadingMode == 2 && sunDiff > 0.0) {
            vec3 sunReflect = reflect(-dirToSun, normal);
            float sunSpec = max(dot(sunReflect, viewDir), 0.0);
            sunSpec = pow(sunSpec, exponent);
            specAccum += sunColor * sunIntensity * sunSpec;
        }
    }

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
    if (lightCount == 0 && sunIntensity <= epsilon && length(ambientLight) <= epsilon)
        finalColor = baseColor;

    if (shadingMode == 2) {
        vec3 specColor = specularColor * specularStrength;
        if (useMaterial)
            specColor *= ks;

        finalColor += specColor * specAccum;
    }

    float shadowValue = 1.0;
    if (shadowsEnabled) {
        vec4 lightSpacePos = lightMVP * vec4(fragPosition, 1.0);
        lightSpacePos /= lightSpacePos.w;
        lightSpacePos = lightSpacePos * 0.5 + 0.5;

        float closestDepth = texture(shadowMap, lightSpacePos.xy).r;
        float currentDepth = lightSpacePos.z;
        float bias = 0.001;

        shadowValue = currentDepth - bias > closestDepth ? 0.0 : 1.0;
    }

    if (pcf) {
        float pixelSize = 1.0 / 1024.0; 

        vec4 lightSpacePos = lightMVP * vec4(fragPosition, 1.0);
        lightSpacePos /= lightSpacePos.w;
        lightSpacePos = lightSpacePos * 0.5 + 0.5; 

        float fragLightDepth = lightSpacePos.z;
        float bias = 0.001;
        shadowValue = 0.0;

        for (int i = -1; i <= 1; ++i) {
            for (int j = -1; j <= 1; ++j) {
                vec2 offset = vec2(i, j) * pixelSize;

                float closestDepth = texture(shadowMap, lightSpacePos.xy + offset).r;

                if (fragLightDepth - bias > closestDepth)
                    shadowValue += 0.0;
                else
                    shadowValue += 1.0;
            }
        }

        shadowValue /= 9.0; 
    }

    finalColor = finalColor * shadowValue;

    fragColor = vec4(finalColor, 1);
}
