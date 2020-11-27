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
layout(location = 0) rayPayloadInEXT hitPayload prd;
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

 
  // Material of the object
   int               matIdx = matIndex[nonuniformEXT(gl_InstanceID)].i[gl_PrimitiveID];
  WaveFrontMaterial mat    = materials[nonuniformEXT(gl_InstanceID)].m[matIdx];
  
  vec3 emittance;

  if(mat.illum == 5)
	emittance = vec3(pushC.lightIntensity);
  else 
	emittance = mat.emission.xyz;

  // Pick a random direction from here and keep going.
  vec3 tangent, bitangent;
  createCoordinateSystem(normal, tangent, bitangent);
  vec3 rayOrigin    = worldPos;
 
  float p = 1.0;

  vec3 rayDirection = gl_WorldRayDirectionEXT;
  
  vec3  albedo    = mat.diffuse;
  //mat.illum = 4;
  //
  //if(rnd(prd.seed) < mat.shininess) {
  //
	//albedo = mat.specular;
	//mat.illum = 3;
  //
  //
  //}

   // Compute the BRDF for this ray (assuming Lambertian reflection)
  // BRDF = Bidirectional Reflectance Distribution Function
	 
  float cos_theta;


  if(mat.illum == 3){
  
	vec3 rayDir = reflect(gl_WorldRayDirectionEXT, normal);
	createCoordinateSystem(rayDir, tangent, bitangent);
	rayDirection = samplingPhongDistribution(prd.seed, pushC.sphereGlossiness, tangent, bitangent, rayDir, p, normal);
	cos_theta = dot(rayDirection, rayDir);

  }  else if(mat.illum == 2){

  	vec3 rayDir = refract(gl_WorldRayDirectionEXT, normal, mat.ior);
	createCoordinateSystem(rayDir, tangent, bitangent);
	rayDirection = samplingPhongDistribution(prd.seed, pushC.sphereGlossiness, tangent, bitangent, rayDir, p, normal);
	cos_theta = dot(rayDirection, rayDir);

  }else{
	rayDirection = samplingHemisphere2(prd.seed, tangent, bitangent, normal, p);
	cos_theta = dot(rayDirection, normal);
}

  //p = 1.0 / M_PI;
  

    

  vec3 BRDF = albedo / M_PI;
	
  if(mat.illum == 3 || mat.illum == 2) BRDF = albedo * p;


  prd.rayOrigin    = rayOrigin;
  prd.rayDir = rayDirection;
  prd.hitValue     = emittance;
  prd.weight       = BRDF * cos_theta / p;
  return;

}

