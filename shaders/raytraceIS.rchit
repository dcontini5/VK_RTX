#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#include "raycommon.glsl"
#include "wavefront.glsl"
#include "sampling.glsl"

hitAttributeEXT vec2 attribs;

// clang-format off
layout(location = 0) rayPayloadInEXT RTHitPayload prd;
layout(location = 1) rayPayloadEXT bool isShadowed;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 1, scalar) buffer MatColorBufferObject { WaveFrontMaterial m[]; } materials[];
layout(binding = 2, set = 1, scalar) buffer ScnDesc { sceneDesc i[]; } scnDesc;
layout(binding = 3, set = 1) uniform sampler2D textureSamplers[];
layout(binding = 4, set = 1)  buffer MatIndexColorBuffer { int i[]; } matIndex[];
layout(binding = 5, set = 1, scalar) buffer Vertices { Vertex v[]; } vertices[];
layout(binding = 6, set = 1) buffer Indices { uint i[]; } indices[];
layout(binding = 7, set = 1, scalar) buffer allSpheres_ {Sphere i[];} allSpheres;

// clang-format on

layout(push_constant) uniform Constants
{
  vec4  clearColor;
  vec3  lightPosition;
  float lightIntensity;
  float areaLightRadius;
  int   lightType;
  int   maxDepth;
  int   frame;
  float accWeight;
  float mirrorGlossiness;
  float sphereGlossiness;
  float apertureRadius;
  float	focalLenght;
  int	lightIndex;
}
pushC;


void main()
{
  vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

  Sphere instance = allSpheres.i[gl_PrimitiveID];

  // Computing the normal at hit position
  vec3 normal = normalize(worldPos - instance.center);

  // Computing the normal for a cube
  if(gl_HitKindEXT == KIND_CUBE)  // Aabb
  {
    vec3  absN = abs(normal);
    float maxC = max(max(absN.x, absN.y), absN.z);
    normal     = (maxC == absN.x) ?
                 vec3(sign(normal.x), 0, 0) :
                 (maxC == absN.y) ? vec3(0, sign(normal.y), 0) : vec3(0, 0, sign(normal.z));
  }

  
  vec3 lDir      = pushC.lightPosition - worldPos;
  float lightDistance  = length(lDir);
  float lightIntensity = pushC.lightIntensity / (lightDistance * lightDistance);
  vec3 L              = normalize(lDir);

  // Material of the object
  int               matIdx = matIndex[nonuniformEXT(gl_InstanceID)].i[gl_PrimitiveID];
  WaveFrontMaterial mat    = materials[nonuniformEXT(gl_InstanceID)].m[matIdx];
  
  if(mat.illum == 2 || mat.illum == 3){
	mat.specular = vec3(1);
	mat.diffuse = vec3(0.1);
	mat.shininess = 100.0;
  }

  // Diffuse
  vec3  diffuse     = computeDiffuse(mat, L, normal);
  vec3  specular    = vec3(0);

  
  float attenuation = 1.0;
  float minAtt = 0.3;
  
  //vec3 normL = normalize(L);
  vec3 tangent, bitangent;
  createCoordinateSystem(L, tangent, bitangent);

  //Move this value into const buffer
  float lightRadius = pushC.areaLightRadius;

  float maxCosTheta = calcMaxCosTheta(L, lightDistance, tangent, lightRadius);

  // Tracing shadow ray only if the light is visible from the surface
  if(dot(normal, L) > 0)
  {

	

	float tMin   = 0.001;
	float tMax   = lightDistance;
	vec3  origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
	//vec3  rayDir = L;
	vec3  rayDir = samplingCone(prd.seed, maxCosTheta, tangent, bitangent, L);
	uint  flags  = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT
	             | gl_RayFlagsSkipClosestHitShaderEXT;
	isShadowed = true;
	traceRayEXT(topLevelAS,  // acceleration structure
	            flags,       // rayFlags
	            0xFF,        // cullMask
	            0,           // sbtRecordOffset
	            0,           // sbtRecordStride
	            1,           // missIndex
	            origin,      // ray origin
	            tMin,        // ray min range
	            rayDir,      // ray direction
	            tMax,        // ray max range
	            1            // payload (location = 1)
	);
			if(isShadowed)
	{
			  
	  attenuation = 0.5;
	  
	}
	else
	{
	  // Specular
	  specular = computeSpecular(mat, gl_WorldRayDirectionEXT, L, normal);
	}
		
	
  }
  float cosTheta = 1;
  float p = 1;

   if(mat.illum == 2) {
  
	vec3 origin = worldPos;
	
	vec3 rayDir = refract(gl_WorldRayDirectionEXT, normal, mat.ior);
	createCoordinateSystem(rayDir, tangent, bitangent);
	prd.attenuation *= vec3(0.7f);
	prd.done = 0;
	prd.rayOrigin = origin;
	prd.rayDir = samplingPhongDistribution(prd.seed, pushC.sphereGlossiness, tangent, bitangent, rayDir, p, cosTheta);
	//prd.rayDir = rayDir;
	

  }
 
   if(mat.illum == 3) {
  
	vec3 origin = worldPos;
	vec3 rayDir = reflect(gl_WorldRayDirectionEXT, normal);
	createCoordinateSystem(rayDir, tangent, bitangent);
	prd.attenuation *= vec3(0.7f);;
	prd.done = 0;
	prd.rayOrigin = origin;
	prd.rayDir = samplingPhongDistribution(prd.seed, pushC.sphereGlossiness, tangent, bitangent, rayDir, p, cosTheta);
	//prd.rayDir = rayDir;

  }
 

 
  prd.hitValue = vec3(lightIntensity * attenuation * (diffuse + specular));// * cosTheta
  
}