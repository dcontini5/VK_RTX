struct hitPayload
{
  vec3 hitValue;
  int depth;
  vec3 attenuation;
  int done;
  vec3 rayOrigin;
  vec3 rayDir;
  uint seed;
};

struct Sphere
{
  vec3  center;
  float radius;
};

struct Aabb
{
  vec3 minimum;
  vec3 maximum;
};

#define KIND_SPHERE 0
#define KIND_CUBE 1
