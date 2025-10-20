#version 410

uniform vec3 markerColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(markerColor, 1.0);
}
