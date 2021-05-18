#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#include "raycommon.glsl"

layout(location = 0) rayPayloadInEXT hitPayload prd;

layout(push_constant) uniform Constants{

	vec4 clearColor;
	vec3  lightPosition;
	float lightIntensity;
	float areaLightRadius;
	int   lightType;
	int   maxDepth;

};

void main()
{
    if(prd.depth == 0)
    prd.hitValue = clearColor.xyz * 0.8;
	else
    //prd.hitValue = clearColor.xyz * 2.0;  // Lower contribution from environment
    prd.hitValue = clearColor.xyz * 0.5;
    //prd.hitValue = clearColor.xyz;
	prd.depth = 100;
}