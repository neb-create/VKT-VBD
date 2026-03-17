module SHSampling;

#define PI 3.141592

// TODO: potentially put skybox SH into cubemap since it's one of a kind and is used a lot
public float3 SampleSh(float3 rd, StructuredBuffer<float> shCoefficients, uint bufferOffset) {

    float cosCo1 = PI;
    float cosCo2 = (2. / 3.) * PI;
    float cosCo3 = PI * .25;
    float mult = 4. * PI / (128. * 128.); // TODO: dont hardcode the monte carlo count
    float3 c1 = mult * cosCo1 * float3(shCoefficients[0 + bufferOffset], shCoefficients[1 + bufferOffset], shCoefficients[2 + bufferOffset]);

    float3 c2 = mult * cosCo2 * float3(shCoefficients[3 + bufferOffset], shCoefficients[4 + bufferOffset], shCoefficients[5 + bufferOffset]);
    float3 c3 = mult * cosCo2 * float3(shCoefficients[6 + bufferOffset], shCoefficients[7 + bufferOffset], shCoefficients[8 + bufferOffset]);
    float3 c4 = mult * cosCo2 * float3(shCoefficients[9 + bufferOffset], shCoefficients[10 + bufferOffset], shCoefficients[11 + bufferOffset]);

    float3 c5 = mult * cosCo3 * float3(shCoefficients[12 + bufferOffset], shCoefficients[13 + bufferOffset], shCoefficients[14 + bufferOffset]);
    float3 c6 = mult * cosCo3 * float3(shCoefficients[15 + bufferOffset], shCoefficients[16 + bufferOffset], shCoefficients[17 + bufferOffset]);
    float3 c7 = mult * cosCo3 * float3(shCoefficients[18 + bufferOffset], shCoefficients[19 + bufferOffset], shCoefficients[20 + bufferOffset]);
    float3 c8 = mult * cosCo3 * float3(shCoefficients[21 + bufferOffset], shCoefficients[22 + bufferOffset], shCoefficients[23 + bufferOffset]);
    float3 c9 = mult * cosCo3 * float3(shCoefficients[24 + bufferOffset], shCoefficients[25 + bufferOffset], shCoefficients[26 + bufferOffset]);

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