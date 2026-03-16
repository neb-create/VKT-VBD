import Raytracing;

struct UniformBuffer {
    float off;
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
struct Vertex {
    float3 pos;
    float3 color;
    float2 uv;
    float3 norm;
};

ConstantBuffer<UniformBuffer> ubo;

SamplerCube skybox;

StructuredBuffer<Vertex> worldVertices;
StructuredBuffer<uint32_t> worldIndices;
Sampler2D worldTex;
Sampler2D worldMetal;
Sampler2D worldRoughness;
Sampler2D worldAO;

StructuredBuffer<float> skyboxSh;

struct VSInput {
    float3 position;
    float3 color;
    float2 uv;
    float3 norm;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 worldPos;
    float3 color;
    float2 uv;
    float3 norm;
};


[shader("vertex")]
VSOutput vertMain(VSInput vIn) { //uint vid : SV_VertexID) {
    VSOutput vOut;
    float3 objPos = vIn.position;
    float4 worldPos = mul(ubo.model, float4(objPos, 1.0));
    vOut.worldPos = worldPos.xyz;
    vOut.position = mul(ubo.proj, mul(ubo.view, worldPos));
    vOut.color = vIn.color;
    vOut.uv = vIn.uv;
    vOut.norm = mul(ubo.model, float4(vIn.norm, 0.0)).xyz; // TODO: make do opposite scale (inv trans)
    return vOut;
}

float3 sampleSh(float3 rd) {

    float cosCo1 = PI;
    float cosCo2 = (2. / 3.) * PI;
    float cosCo3 = PI * .25;
    float mult = 4. * PI / (128.*128.);
    // TODO: put in module andor generate into a cubemap so we dont have to do math after caching
    float3 c1 = mult * cosCo1 * float3(skyboxSh[0], skyboxSh[1], skyboxSh[2]);

    float3 c2 = mult * cosCo2 * float3(skyboxSh[3], skyboxSh[4], skyboxSh[5]);
    float3 c3 = mult * cosCo2 * float3(skyboxSh[6], skyboxSh[7], skyboxSh[8]);
    float3 c4 = mult * cosCo2 * float3(skyboxSh[9], skyboxSh[10], skyboxSh[11]);

    float3 c5 = mult * cosCo3 * float3(skyboxSh[12], skyboxSh[13], skyboxSh[14]);
    float3 c6 = mult * cosCo3 * float3(skyboxSh[15], skyboxSh[16], skyboxSh[17]);
    float3 c7 = mult * cosCo3 * float3(skyboxSh[18], skyboxSh[19], skyboxSh[20]);
    float3 c8 = mult * cosCo3 * float3(skyboxSh[21], skyboxSh[22], skyboxSh[23]);
    float3 c9 = mult * cosCo3 * float3(skyboxSh[24], skyboxSh[25], skyboxSh[26]);

    return 
        c1 * 0.28209479 +
        (-1.) * c2 * 0.4886025119029199 * rd.y +
        c3 * 0.4886025119029199 * rd.z +
        (-1.) * c4 * 0.4886025119029199 * rd.x +
        c5 * 1.0925484305920792 * rd.x * rd.y +
        (-1.) * c6 * 1.0925484305920792 * rd.y * rd.z +
        c7 * 0.31539156525252005 * (3 * rd.z * rd.z - 1) +
        (-1.) * c8 * 1.0925484305920792 * rd.x * rd.z +
        c9 * 0.5462742152960396 * (rd.x * rd.x - rd.y * rd.y);
}

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

float3 reinhardOp(float3 energy) {
    return energy / (1.0 + energy);
}

float3 gammaCorrect(float3 col) {
    return pow(col, float3(1. / 2.2));
}

float3 textureGammaCorrect(float3 texColor) {
    return pow(texColor, float3(2.2));
}

float2 hash22(float2 p)
{
    float3 p3 = fract(p.xyx * float3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

// from ShaderToy https://www.shadertoy.com/view/4tXyWN
float rng(uint2 seed) {
    uint2 q = 1103515245U * ((seed >> 1U) ^ (seed.yx));
    uint n = 1103515245U * ((q.x) ^ (q.y >> 3U));
    return float(n) * (1.0 / float(0xffffffffU));
}

float rngf(float f) {
    return fract(8592.682 * sin(f * 32.301));
}

float rngi(int i) {
    return fract(8592.682 * sin(float(i)*0.0325 * 32.301));
}

float3 uniformSphere(float2 xi) {
    float theta = xi.x * 2. * PI;

    float y = xi.y * 2. - 1.;
    float r = 1. - y * y;

    float3 v = float3(r * float2(cos(theta), sin(theta)), y).xzy;

    return normalize(v); // Should already be normalized but just in case
}

float3 SamplePBR(float3 wo, float3 worldPos, float3 norm, float3 albedo, float metallic, float roughness, float ao) {
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

    return Lo;
}

// For reflections, ro is position of first hitPos from camera (the frag pos), rd is reflected ray
float3 SampleEnvironment(float3 camPos, float3 ro, float3 rd) {
    float minDist = 10000.;
    float2 hitUv;
    float3 hitPos, hitNorm;
    for (uint32_t i = 0; i < 4596; i += 3) {
        Vertex vs[3] = Vertex[](worldVertices[worldIndices[i]], worldVertices[worldIndices[i + 1]], worldVertices[worldIndices[i + 2]]);
        float3 vp[3] = float3[](
            mul(ubo.model, float4(vs[0].pos, 1.)).xyz,
            mul(ubo.model, float4(vs[1].pos, 1.)).xyz,
            mul(ubo.model, float4(vs[2].pos, 1.)).xyz
        );
        // TODO: ( later should localize query to run bvh)
        float3 cl = cross(vp[2] - vp[0], vp[1] - vp[0]);
        if (length(cl) < 0.001) continue;
        float area = 0.5 * length(cl);
        float3 oNorm = normalize(cl);

        // Plane
        float dist = -dot(oNorm, ro - vp[0]) / dot(oNorm, rd);
        if (dist > 0.01 && dist < minDist) {
            float3 hitPnt = ro + rd * dist;
            float b2 = 0.5 * length(cross(hitPnt - vp[0], hitPnt - vp[1]));
            float b0 = 0.5 * length(cross(hitPnt - vp[1], hitPnt - vp[2]));
            float b1 = 0.5 * length(cross(hitPnt - vp[2], hitPnt - vp[0]));
            if (b0 + b1 + b2 <= area+0.001) {
                minDist = dist;

                hitUv = (b0 * vs[0].uv + b1 * vs[1].uv + b2 * vs[2].uv) / area;
                hitPos = (b0 * vp[0] + b1 * vp[1] + b2 * vp[2]) / area;
                hitNorm = normalize(mul(ubo.model, float4(b0 * vs[0].norm + b1 * vs[1].norm + b2 * vs[2].norm,0.)).xyz); // TODO: make inverse transpose
            }
        }
    }

    if (minDist < 10000.) {
        float3 albedo = textureGammaCorrect(worldTex.Sample(hitUv).rgb);
        float metallic = 0.; // worldMetal.Sample(hitUv).r;
        float roughness = 0.2; // worldRoughness.Sample(hitUv).r;
        float ao = 0.;//worldAO.Sample(hitUv).r;
        return SamplePBR(normalize(ro-hitPos), hitPos, hitNorm, albedo, metallic, roughness, ao);
    } else {
        return float3(0.);//SampleSkybox(rd, skybox);
    }
}

[shader("fragment")]
float4 fragMain(VSOutput o) : SV_Target
{
    float4x4 viewTranspose = transpose(ubo.view);
    float3 camPos = -mul(float3x3(viewTranspose), viewTranspose[3].xyz);
    float3 norm = normalize(o.norm);

    float3 sta = o.worldPos + norm * 0.01;
    float3 reflRay = normalize(reflect(o.worldPos - camPos, norm));

    float3 Lo = SampleEnvironment(camPos, sta, reflRay);
    float3 col = gammaCorrect(reinhardOp(Lo));

    return float4(col, 1.);

}