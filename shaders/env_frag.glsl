#version 410
out vec4 fragColor;

in vec3 fragTexCoord2d;

uniform samplerCube skybox;

void main()
{    
    fragColor = texture(skybox, fragTexCoord2d);
}