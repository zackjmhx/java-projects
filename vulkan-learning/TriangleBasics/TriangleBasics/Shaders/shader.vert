#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
	vec4 gl_Position;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 normal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec4 fragNormal;

layout(binding = 0) uniform UniformBufferObject {
	mat4 model;
	mat4 view;
	mat4 mvp;
} ubo;

void main(){
	gl_Position = ubo.mvp *  vec4(inPosition, 1.0);
	fragNormal = ubo.view * ubo.model * vec4(normal, 1.0);
	fragColor = inColor;
	fragTexCoord = inTexCoord;
}