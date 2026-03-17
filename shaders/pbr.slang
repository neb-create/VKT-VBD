module PBR;
import sh_sampling;

static float3 light_pos[4] = float3[](float3(-10, 10, 10),
                                      float3(10, 10, 10),
                                      float3(-10, -10, 10),
                                      float3(10, -10, 10));

static float3 light_col[4] = float3[](float3(300.f, 300.f, 900.f),
                                      float3(300.f, 900.f, 300.f),
                                      float3(900.f, 300.f, 300.f),
                                      float3(300.f, 300.f, 900.f));

static float PI = 3.14159f;
static float INVPI = 0.31830988618;

float3 fresnelShlick(float cosAngleFromMicroNormal, float3 baseCol) {
    return baseCol + (float3(1.0) - baseCol) * pow(1.0 - cosAngleFromMicroNormal, 5.);
}

float distribFunc(float3 macroNormal, float3 microNormal, float roughness) {
    float a = roughness * roughness;
    return a * a * INVPI * 1.0
            / pow(
                pow(dot(macroNormal, microNormal), 2.)
                * (a * a - 1.) + 1.,
                    2.);
}

float gShlick(float cosW, float k) {
    return cosW / (cosW * (1.0 - k) + k);
}

float geomOcclusion(float3 wo, float3 wi, float3 macroNormal, float roughness) {
    float k = 1.0; // pow(roughness+1, 2.) / 8.;
    return gShlick(dot(wo, macroNormal), k) * gShlick(dot(wi, macroNormal), k);
}

public float3 reinhardOp(float3 energy) {
    return energy / (1.0 + energy);
}

public float3 gammaCorrect(float3 col) {
    return pow(col, float3(1. / 2.2));
}

public float3 textureGammaCorrect(float3 texColor) {
    return pow(texColor, float3(2.2));
}

public float3 SamplePBR(float3 wo, float3 worldPos, float3 norm, float3 albedo, float metallic, float roughness, float ao, StructuredBuffer<float> skyboxSh) {
    float3 Lo = float3(0.);
    Lo += 0.03 * ao * albedo; // Ambient Light

    float att = 0.;

    for (int i = 0; i < 4; i++) {
        float3 wi = normalize(light_pos[i] - worldPos);
        float3 wh = normalize(wo + wi);

        float3 fragToLight = light_pos[i] - worldPos;
        float attenuation = 1.0 / dot(fragToLight, fragToLight); att += attenuation;
        float3 Li = light_col[i] * attenuation;

        //
        float3 fresnelCol = lerp(float3(0.04), albedo, metallic);
        float3 fresnel = fresnelShlick(max(dot(wh, wo), 0.0), fresnelCol);

        float distrib = distribFunc(norm, wh, roughness);

        float geom = geomOcclusion(wo, wi, norm, roughness);

        //
        float lambert = max(0., dot(wi, norm));

        //
        float3 fCookTorrance = fresnel * distrib * geom / (4.0 * dot(norm, wo) * dot(norm, wi));
        float3 fLambert = albedo * INVPI;

        float3 ks = fresnel;
        float3 kd = float3(1.0) - ks;
        kd *= 1.0 - metallic; // More metal less diffuse

        Lo += fCookTorrance * Li * lambert; // Specular
        Lo += kd * fLambert * Li * lambert; // Diffuse
    }

    return SampleSh(norm, skyboxSh, 0) * INVPI * albedo;
}