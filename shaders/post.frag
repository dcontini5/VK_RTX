#version 450
#extension GL_GOOGLE_include_directive : enable

#include "wavefront.glsl"

layout(location = 0) in vec2 outUV;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D noisyTxt;

layout(push_constant) uniform shaderInformation
{
  float aspectRatio;
}
pushc;

void main()
{
  vec2  uv    = outUV;

  //float gamma = 1. / 2.2;
  //fragColor   = pow(texture(noisyTxt, uv).rgba, vec4(gamma));

  vec4 color = texture(noisyTxt, uv).rgba;

  float apertureTime = color.a;

  color *= apertureTime;

  color.rgb = ACESFilm(color.rgb);

  color.rgb = LinearToSRGB(color.rgb);
  
  fragColor = vec4(color.rgb, 1.0); 
}
