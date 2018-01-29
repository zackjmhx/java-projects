#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoords;
layout(location = 2) in vec4 fragNormal;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {

	//just hardcoding to test, will abstract out to the host side app later

	vec3 ambient =  vec3(0.1, 0.1, 0.3);
	vec3 dirLightInt =  vec3(0.6, 0.6, 0.8);
	vec3 dirLightDir = normalize(vec3(1.0, 4.0, -5.0));
	vec3 surfaceNorm = normalize(fragNormal.xyz);
	vec4 texel = texture(texSampler, fragTexCoords);

	vec3 lightIntense = ambient + dirLightInt * max(dot(surfaceNorm, dirLightDir), 0.0); //simple Phong lighting

	outColor = texel * vec4(lightIntense, 1.0); //display the normals as color
}