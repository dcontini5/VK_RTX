// Generate a random unsigned int from two unsigned int values, using 16 pairs
// of rounds of the Tiny Encryption Algorithm. See Zafar, Olano, and Curtis,
// "GPU Random Numbers via the Tiny Encryption Algorithm"
#define M_PI 3.141592

uint tea(uint val0, uint val1)
{
  uint v0 = val0;
  uint v1 = val1;
  uint s0 = 0;

  for(uint n = 0; n < 16; n++)
  {
    s0 += 0x9e3779b9;
    v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
    v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
  }

  return v0;
}

// Generate a random unsigned int in [0, 2^24) given the previous RNG state
// using the Numerical Recipes linear congruential generator
uint lcg(inout uint prev)
{
  uint LCG_A = 1664525u;
  uint LCG_C = 1013904223u;
  prev       = (LCG_A * prev + LCG_C);
  return prev & 0x00FFFFFF;
}

// Generate a random float in [0, 1) given the previous RNG state
float rnd(inout uint prev)
{
  return (float(lcg(prev)) / float(0x01000000));
}


//-------------------------------------------------------------------------------------------------
// Sampling
//-------------------------------------------------------------------------------------------------

// Randomly sampling around +Z


vec3 samplingHemisphere(inout uint seed, in vec3 x, in vec3 y, in vec3 z, out float p)
{
#define M_PI 3.141592

	float u0 = rnd(seed);
	float u1 = rnd(seed);
	float sq = sqrt(u0);

	vec3 direction = vec3(cos(2 * M_PI * u1) * sq, sin(2 * M_PI * u1) * sq, sqrt(1 - u0));
	p = direction.z / M_PI;
	direction = direction.x * x + direction.y * y + direction.z * z;

	return direction;
}

// Return the tangent and binormal from the incoming normal
void createCoordinateSystem(in vec3 N, out vec3 Nt, out vec3 Nb)
{
  if(abs(N.x) > abs(N.y))
    Nt = vec3(N.z, 0, -N.x) / sqrt(N.x * N.x + N.z * N.z);
  else
    Nt = vec3(0, -N.z, N.y) / sqrt(N.y * N.y + N.z * N.z);
  Nb = cross(N, Nt);
}

float calcMaxCosTheta(in vec3 N, in float L,  in vec3 T, in float R){

	return dot(N, normalize(N * L + T * R)); 

}

vec3 samplingCone(inout uint seed, in float maxCosTheta, in vec3 x, in vec3 y, in vec3 z){
	
	
	float r1 = rnd(seed);
	float r2 = rnd(seed);


	float cosTheta = (1 - r1) + r1 * maxCosTheta;
	float sinTheta = sqrt(1 - cosTheta * cosTheta);
	float phi = r2 * 2 * M_PI;

	vec3 direction = vec3( cos(phi) * sinTheta,		//x
							sin(phi) * sinTheta,	//y
							cosTheta);				//z
							
	direction = direction.x * x + direction.y * y + direction.z * z;
	
	return direction;

}


vec3 samplingPhongDistribution(inout uint seed, in float exp, in vec3 x, in vec3 y, in vec3 z, inout float p, inout float cosTheta) {
	
	float u0 = rnd(seed);
	float u1 = rnd(seed);

	cosTheta = pow(1 - u0, 1/(1 + exp));
	float sinTheta = sqrt(1 - cosTheta * cosTheta);
	float phi = u1 * 2 * M_PI;

	vec3 refDirection = vec3(
						cos(phi) * sinTheta,
						sin(phi) * sinTheta,
						cosTheta);

	//p = (exp + 1) / 2 * M_PI * pow(cosTheta, exp);

	vec3 direction =  refDirection.x * x + refDirection.y * y + refDirection.z * z;
	

	return direction;
	
}

