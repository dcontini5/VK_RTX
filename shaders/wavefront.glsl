struct Vertex
{
  vec3 pos;
  vec3 nrm;
  vec3 color;
  vec2 texCoord;
};

struct WaveFrontMaterial
{
  vec3  ambient;
  vec3  diffuse;
  vec3  specular;
  vec3  transmittance;
  vec3  emission;
  float shininess;
  float ior;       // index of refraction
  float dissolve;  // 1 == opaque; 0 == fully transparent
  int   illum;     // illumination model (see http://www.fileformat.info/format/material/)
  int   textureId;
};

struct sceneDesc
{
  int  objId;
  int  txtOffset;
  mat4 transfo;
  mat4 transfoIT;
};


vec3 computeDiffuse(WaveFrontMaterial mat, vec3 lightDir, vec3 normal)
{
  // Lambertian
  float dotNL = max(dot(normal, lightDir), 0.0);
  vec3  c     = mat.diffuse * dotNL;
  if(mat.illum >= 1)
    return c + mat.ambient;
}

vec3 computeSpecular(WaveFrontMaterial mat, vec3 viewDir, vec3 lightDir, vec3 normal)
{
  if(mat.illum < 2)
    return vec3(0);

  // Compute specular only if not in shadow
  const float kPi        = 3.14159265;
  const float kShininess = max(mat.shininess, 1.0);

  // Specular
  const float kEnergyConservation = (2.0 + kShininess) / (2.0 * kPi);
  vec3        V                   = normalize(-viewDir);
  vec3        R                   = reflect(-lightDir, normal);
  float       specular            = kEnergyConservation * pow(max(dot(V, R), 0.0), kShininess);

  return vec3(mat.specular * specular);
}

vec3 LessThan(vec3 f, float value)
{
	return vec3(
		(f.x < value) ? 1.0f : 0.0f,
		(f.y < value) ? 1.0f : 0.0f,
		(f.z < value) ? 1.0f : 0.0f);
}

vec3 LinearToSRGB(vec3 rgb)
{
	rgb = clamp(rgb, 0.0f, 1.0f);

	return mix(
		pow(rgb, vec3(1.0f / 2.4f)) * 1.055f - 0.055f,
		rgb * 12.92f,
		LessThan(rgb, 0.0031308f)
	);
}

vec3 SRGBToLinear(vec3 rgb)
{
	rgb = clamp(rgb, 0.0f, 1.0f);

	return mix(
		pow(((rgb + 0.055f) / 1.055f), vec3(2.4f)),
		rgb / 12.92f,
		LessThan(rgb, 0.04045f)
	);
}

// ACES tone mapping curve fit to go from HDR to LDR
//https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 ACESFilm(vec3 x)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return clamp((x*(a*x + b)) / (x*(c*x + d) + e), 0.0f, 1.0f);
}