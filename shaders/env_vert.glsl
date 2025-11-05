#version 410

uniform mat4 mvpMatrix;
// Normals should be transformed differently than positions:
// https://paroj.github.io/gltut/Illumination/Tut09%20Normal%20Transformation.html

layout(location = 0) in vec3 position;

out vec3 fragTexCoord2d;

void main()
{
    gl_Position = (mvpMatrix * vec4(position, 1)).xyww;

    fragTexCoord2d  = position;
}