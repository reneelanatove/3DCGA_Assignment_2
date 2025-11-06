#version 410

layout(std140) uniform Material // Must match the GPUMaterial defined in src/mesh.h
{
    vec4 kdMetallic;      // xyz = diffuse/albedo, w = metallic
	vec4 ksRoughness;     // xyz = specular colour, w = roughness
	vec4 miscParams;      // x = shininess, y = transparency, z = ambient occlusion, w = unused
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
uniform vec3 pbrBaseColor;
uniform float pbrMetallic;
uniform float pbrRoughness;
uniform float pbrAo;

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
uniform samplerCube environmentMap;
uniform bool useEnvMap;
uniform float envMapStrength;

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

layout(location = 0) out vec4 fragColor;

const float PI = 3.14159265359;

float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-4);
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 1e-4);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = geometrySchlickGGX(NdotV, roughness);
    float ggx2 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

void main()
{
    vec3 normal = normalize(fragNormal);

    vec3 matKd = kdMetallic.rgb;
    float matMetallic = clamp(kdMetallic.a, 0.0, 1.0);
    vec3 matKs = ksRoughness.rgb;
    float matRoughness = clamp(ksRoughness.a, 0.04, 1.0);
    float matShininess = miscParams.x;
    float matAo = clamp(miscParams.z, 0.0, 1.0);

    if (useNormalMap && hasTexCoords) {
        vec3 normalMapValue = texture(normalMap, fragTexCoord).xyz;
        normal = normalize(normalMapValue * 2.0 - 1.0);
    }

    vec3 baseColor = vec3(0.0);
    if (shadingMode == 3) {
        if (hasTexCoords)
            baseColor = texture(colorMap, fragTexCoord).rgb;
        else if (useMaterial)
            baseColor = matKd;
        else
            baseColor = pbrBaseColor;
    } else {
        if (hasTexCoords)
            baseColor = texture(colorMap, fragTexCoord).rgb;
        else if (useMaterial)
            baseColor = matKd;
        else
            baseColor = customDiffuseColor;
    }

    float metallicValue = clamp(pbrMetallic, 0.0, 1.0);
    float roughnessValue = clamp(pbrRoughness, 0.04, 1.0);
    float aoValue = clamp(pbrAo, 0.0, 1.0);
    if (useMaterial) {
        metallicValue = matMetallic;
        roughnessValue = matRoughness;
        aoValue = matAo;
    }

    if (shadingMode == 0) {
        fragColor = vec4(baseColor, 1.0);
        return;
    }

    vec3 viewDir = normalize(viewPosition - fragPosition);
    const float epsilon = 1e-4;

    if (shadingMode == 3) {
        vec3 F0 = mix(vec3(0.04), baseColor, metallicValue);
        vec3 Lo = vec3(0.0);

        if (sunIntensity > epsilon && length(sunColor) > epsilon) {
            vec3 lightDir = normalize(-sunDirection);
            float NdotL = max(dot(normal, lightDir), 0.0);
            if (NdotL > 0.0) {
                vec3 halfVec = normalize(viewDir + lightDir);
                float NDF = distributionGGX(normal, halfVec, roughnessValue);
                float G = geometrySmith(normal, viewDir, lightDir, roughnessValue);
                vec3 F = fresnelSchlick(max(dot(halfVec, viewDir), 0.0), F0);

                vec3 kS = F;
                vec3 kD = (vec3(1.0) - kS) * (1.0 - metallicValue);
                vec3 diffuse = kD * baseColor / PI;
                vec3 specular = (NDF * G * F) / max(4.0 * max(dot(normal, viewDir), 0.0) * NdotL, epsilon);

                vec3 radiance = sunColor * sunIntensity;
                Lo += (diffuse + specular) * radiance * NdotL;
            }
        }

        int lightCount = min(numLights, MAX_LIGHTS);
        for (int i = 0; i < lightCount; ++i) {
            vec3 lightDir = normalize(lightPositions[i] - fragPosition);
            float NdotL = max(dot(normal, lightDir), 0.0);

            float spotFactor = 1.0;
            if (lightIsSpotlight[i] != 0) {
                float c = dot(-lightDir, normalize(lightDirections[i]));
                spotFactor = smoothstep(lightSpotCosCutoff[i], lightSpotCosCutoff[i] + lightSpotSoftness[i], c);
            }

            if (NdotL > 0.0 && spotFactor > 0.0) {
                vec3 halfVec = normalize(viewDir + lightDir);
                float NDF = distributionGGX(normal, halfVec, roughnessValue);
                float G = geometrySmith(normal, viewDir, lightDir, roughnessValue);
                vec3 F = fresnelSchlick(max(dot(halfVec, viewDir), 0.0), F0);

                vec3 kS = F;
                vec3 kD = (vec3(1.0) - kS) * (1.0 - metallicValue);
                vec3 diffuse = kD * baseColor / PI;
                vec3 specular = (NDF * G * F) / max(4.0 * max(dot(normal, viewDir), 0.0) * NdotL, epsilon);

                vec3 radiance = lightColors[i] * spotFactor;
                Lo += (diffuse + specular) * radiance * NdotL;
            }
        }

        vec3 ambient = ambientLight * baseColor * aoValue;
        vec3 finalColor = ambient + Lo;

        finalColor = finalColor / (finalColor + vec3(1.0));
        finalColor = pow(finalColor, vec3(1.0 / 2.2));

        fragColor = vec4(finalColor, 1.0);
        return;
    }

    vec3 colorAccum = ambientLight * baseColor;
    vec3 specAccum = vec3(0.0);
    float exponent = specularShininess > 0.0 ? specularShininess : matShininess;

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
            specColor *= matKs;

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

    if (useEnvMap && envMapStrength > 0.0) {
        vec3 reflectedDir = reflect(-viewDir, normal);
        vec3 envColor = texture(environmentMap, reflectedDir).rgb;
        float mixFactor = clamp(envMapStrength, 0.0, 1.0);
        finalColor = mix(finalColor, envColor, mixFactor);
    }

    fragColor = vec4(finalColor, 1.0);
}
