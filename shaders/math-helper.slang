module math_helper;

public float4x4 rotY(float o) {
    return float4x4(
        cos(o), 0., sin(o), 0.,
        0., 1., 0., 0.,
        -sin(o), 0., cos(o), 0.,
        0., 0., 0., 1.
    );
}