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
//layout(location = 2) rayPayloadInEXT hitPayload prd2;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 1, scalar) buffer MatColorBufferObject { WaveFrontMaterial m[]; } materials[];
layout(binding = 2, set = 1, scalar) buffer ScnDesc { sceneDesc i[]; } scnDesc;
layout(binding = 3, set = 1) uniform sampler2D textureSamplers[];
layout(binding = 4, set = 1)  buffer MatIndexColorBuffer { int i[]; } matIndex[];
layout(binding = 5, set = 1, scalar) buffer Vertices { Vertex v[]; } vertices[];
layout(binding = 6, set = 1) buffer Indices { uint i[]; } indices[];

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
  // Object of this instance
  uint objId = scnDesc.i[gl_InstanceID].objId;

  // Indices of the triangle
  ivec3 ind = ivec3(indices[nonuniformEXT(objId)].i[3 * gl_PrimitiveID + 0],   //
                    indices[nonuniformEXT(objId)].i[3 * gl_PrimitiveID + 1],   //
                    indices[nonuniformEXT(objId)].i[3 * gl_PrimitiveID + 2]);  //
  // Vertex of the triangle
  Vertex v0 = vertices[nonuniformEXT(objId)].v[ind.x];
  Vertex v1 = vertices[nonuniformEXT(objId)].v[ind.y];
  Vertex v2 = vertices[nonuniformEXT(objId)].v[ind.z];

  const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

  // Computing the normal at hit position
  vec3 normal = v0.nrm * barycentrics.x + v1.nrm * barycentrics.y + v2.nrm * barycentrics.z;
  // Transforming the normal to world space
  normal = normalize(vec3(scnDesc.i[gl_InstanceID].transfoIT * vec4(normal, 0.0)));


  // Computing the coordinates of the hit position
  vec3 worldPos = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
  // Transforming the position to world space
  worldPos = vec3(scnDesc.i[gl_InstanceID].transfo * vec4(worldPos, 1.0));
  
  int               matIdx = matIndex[nonuniformEXT(objId)].i[gl_PrimitiveID];
  WaveFrontMaterial mat    = materials[nonuniformEXT(objId)].m[matIdx];
 
 
 
 vec3         emittance;
  
  if(mat.illum == 5)
	emittance = mat.emission.xyz * vec3(pushC.lightIntensity);
  else 
	emittance = mat.emission.xyz;

  // Pick a random direction from here and keep going.
  vec3 tangent, bitangent;
  createCoordinateSystem(normal, tangent, bitangent);
  vec3 rayOrigin    = worldPos;
 
  
  float p = 1.0;


  vec3 rayDirection = vec3(0);
  
  
  // Compute the BRDF for this ray (assuming Lambertian reflection)
  // BRDF = Bidirectional Reflectance Distribution Function
  float cos_theta;


  if(mat.illum == 3){
  
	vec3 rayDir = reflect(gl_WorldRayDirectionEXT, normal);
	createCoordinateSystem(rayDir, tangent, bitangent);
	rayDirection = samplingPhongDistribution(prd.seed, pushC.mirrorGlossiness, tangent, bitangent, rayDir, p, normal);
	cos_theta = dot(rayDirection, rayDir);

  }  else{
	rayDirection = samplingHemisphere2(prd.seed, tangent, bitangent, normal, p);
	cos_theta = dot(rayDirection, normal);
  }



  vec3  albedo    = mat.diffuse;

  vec3 BRDF = albedo / M_PI;

  if(mat.illum == 3) BRDF = albedo * p;

  prd.rayOrigin    = rayOrigin;
  prd.rayDir = rayDirection;
  prd.hitValue     = emittance ;
  prd.weight       = BRDF * cos_theta / p;
  return;

}
