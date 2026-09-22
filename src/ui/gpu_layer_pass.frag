#version 440

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float layerOpacity;
    float blendMode;
    float hasMask;
    float maskDefault;
    float maskDensity;
    vec4 maskRect;
};

layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D backdrop;
layout(binding = 3) uniform sampler2D maskTexture;

float safe_divide(float numerator, float denominator)
{
    return denominator > 0.000001 ? numerator / denominator : 0.0;
}

float color_dodge(float s, float d)
{
    if (d <= 0.0)
        return 0.0;
    if (s >= 1.0)
        return 1.0;
    return min(1.0, d / max(0.000001, 1.0 - s));
}

float color_burn(float s, float d)
{
    if (d >= 1.0)
        return 1.0;
    if (s <= 0.0)
        return 0.0;
    return 1.0 - min(1.0, (1.0 - d) / max(0.000001, s));
}

float soft_light(float s, float d)
{
    if (s <= 0.5)
        return d - (1.0 - 2.0 * s) * d * (1.0 - d);
    float base = d <= 0.25 ? ((16.0 * d - 12.0) * d + 4.0) * d : sqrt(max(d, 0.0));
    return d + (2.0 * s - 1.0) * (base - d);
}

float blend_channel(float s, float d, int mode)
{
    if (mode == 1) // Normal
        return s;
    if (mode == 2) // Multiply
        return s * d;
    if (mode == 3) // Screen
        return s + d - s * d;
    if (mode == 4) // Overlay
        return d < 0.5 ? 2.0 * s * d : 1.0 - 2.0 * (1.0 - s) * (1.0 - d);
    if (mode == 5) // Darken
        return min(s, d);
    if (mode == 6) // Lighten
        return max(s, d);
    if (mode == 7) // Color Dodge
        return color_dodge(s, d);
    if (mode == 8) // Color Burn
        return color_burn(s, d);
    if (mode == 9) // Hard Light
        return s < 0.5 ? 2.0 * s * d : 1.0 - 2.0 * (1.0 - s) * (1.0 - d);
    if (mode == 10) // Soft Light
        return soft_light(s, d);
    if (mode == 11) // Difference
        return abs(d - s);
    if (mode == 12) // Linear Burn
        return max(0.0, s + d - 1.0);
    if (mode == 13) // Pin Light
        return s < 0.5 ? min(d, 2.0 * s) : max(d, 2.0 * (s - 0.5));
    if (mode == 16) // Exclusion
        return s + d - 2.0 * s * d;
    if (mode == 19) // Linear Dodge (Add)
        return min(1.0, s + d);
    if (mode == 20) // Subtract
        return max(0.0, d - s);
    if (mode == 21) // Divide
        return s <= 0.0 ? 1.0 : min(1.0, d / s);
    if (mode == 22) { // Vivid Light
        return s < 0.5 ? color_burn(2.0 * s, d) : color_dodge(2.0 * (s - 0.5), d);
    }
    if (mode == 23) { // Linear Light
        return clamp(d + 2.0 * s - 1.0, 0.0, 1.0);
    }
    if (mode == 24) { // Hard Mix
        float vivid = s < 0.5 ? 1.0 - min(1.0, (1.0 - d) / max(0.000001, 2.0 * s))
                              : min(1.0, d / max(0.000001, 2.0 * (1.0 - s)));
        return vivid > 0.5 ? 1.0 : 0.0;
    }
    return s;
}

vec3 blend_color(vec3 sourceColor, vec3 backdropColor, int mode)
{
    return vec3(blend_channel(sourceColor.r, backdropColor.r, mode),
                blend_channel(sourceColor.g, backdropColor.g, mode),
                blend_channel(sourceColor.b, backdropColor.b, mode));
}

float sample_mask(vec2 coordinate)
{
    if (hasMask < 0.5)
        return 1.0;
    bool inside = maskRect.z > 0.0 && maskRect.w > 0.0 &&
                  coordinate.x >= maskRect.x && coordinate.y >= maskRect.y &&
                  coordinate.x <= maskRect.x + maskRect.z &&
                  coordinate.y <= maskRect.y + maskRect.w;
    float coverage = inside ? texture(maskTexture, coordinate).a : maskDefault;
    return clamp(coverage * maskDensity + (1.0 - maskDensity), 0.0, 1.0);
}

void main()
{
    vec4 sourcePremultiplied = texture(source, texCoord);
    vec4 backdropPremultiplied = texture(backdrop, texCoord);
    float coverage = sample_mask(texCoord);
    float sourceAlpha = clamp(sourcePremultiplied.a * layerOpacity * coverage, 0.0, 1.0);
    float backdropAlpha = clamp(backdropPremultiplied.a, 0.0, 1.0);
    vec3 sourceColor = sourceAlpha > 0.000001
            ? sourcePremultiplied.rgb / max(sourcePremultiplied.a, 0.000001)
            : vec3(0.0);
    vec3 backdropColor = backdropAlpha > 0.000001
            ? backdropPremultiplied.rgb / backdropAlpha
            : vec3(0.0);
    vec3 blended = blend_color(sourceColor, backdropColor, int(blendMode + 0.5));

    float outputAlpha = sourceAlpha + backdropAlpha * (1.0 - sourceAlpha);
    vec3 outputRgb = blended * sourceAlpha * backdropAlpha +
                     sourceColor * sourceAlpha * (1.0 - backdropAlpha) +
                     backdropColor * backdropAlpha * (1.0 - sourceAlpha);
    fragColor = vec4(outputRgb, outputAlpha) * qt_Opacity;
}
