const char* kComputeShader = R"glsl(
#version 460 core

layout(local_size_x = 16, local_size_y = 16) in;

layout(std430, binding = 0) buffer DF_In {
    float f_in[];
};

layout(std430, binding = 1) buffer DF_Out {
    float f_out[];
};

uniform int width;
uniform int height;
uniform float tau; // Relaxation time

const vec2 velocities[9] = vec2[9](
    vec2(-1, 1), vec2(0, 1), vec2(1, 1),
    vec2(-1, 0), vec2(0, 0), vec2(1, 0),
    vec2(-1, -1), vec2(0, -1), vec2(1, -1)
);

const float weights[9] = float[9](
    1.0f/36, 1.0f/9, 1.0f/36,
    1.0f/9, 4.0f/9, 1.0f/9,
    1.0f/36, 1.0f/9, 1.0f/36
);

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    int index = gid.y * width + gid.x;

    float f[9];
    for (int i = 0; i < 9; i++) {
        f[i] = f_in[index * 9 + i];
    }

    float density = 0.0;
    vec2 velocity = vec2(0.0);
    for (int i = 0; i < 9; i++) {
        density += f[i];
        velocity += f[i] * velocities[i];
    }
    velocity /= density;

    // Compute equilibrium distribution functions
    float feq[9];
    for (int i = 0; i < 9; i++) {
        float velDotC = dot(velocities[i], velocity);
        float velSq = dot(velocity, velocity);
        feq[i] = weights[i] * density * (1.0 + 3.0 * velDotC + 4.5 * velDotC * velDotC - 1.5 * velSq);
    }

    // Collision step
    for (int i = 0; i < 9; i++) {
        f[i] += -(f[i] - feq[i]) / tau;
    }

    // Streaming step with periodic boundaries
    ivec2 nextPos;
    for (int i = 0; i < 9; i++) {
        nextPos = ivec2(gl_GlobalInvocationID.xy) - ivec2(velocities[i]);

        // Apply periodic boundary conditions
        nextPos.x = (nextPos.x + width) % width;
        nextPos.y = (nextPos.y + height) % height;

        int nextIndex = nextPos.y * width + nextPos.x;
        f_out[nextIndex * 9 + i] = f[i];
    }
}
)glsl";

const char* kFragmentShader = R"glsl(
#version 460 core

out vec4 FragColor;

in vec2 TexCoords;

// Simulation parameters
uniform int width;
uniform int height;
uniform float U0; // Initial maximum speed for normalization

layout(std430, binding = 0) buffer DF_In {
    float f_in[];
};

// D2Q9 model velocities
const vec2 velocities[9] = vec2[9](
    vec2(-1, 1), vec2(0, 1), vec2(1, 1),
    vec2(-1, 0), vec2(0, 0), vec2(1, 0),
    vec2(-1, -1), vec2(0, -1), vec2(1, -1)
);

void main()
{
    // Compute pixel coordinates
    int x = int(TexCoords.x * float(width));
    int y = int(TexCoords.y * float(height));

    if (x >= width || y >= height)
    {
        FragColor = vec4(0.0);
        return;
    }

    int index = y * width + x;

    float f[9];
    for (int i = 0; i < 9; i++)
    {
        f[i] = f_in[index * 9 + i];
    }

    float density = 0.0;
    vec2 velocity = vec2(0.0);
    for (int i = 0; i < 9; i++)
    {
        density += f[i];
        velocity += f[i] * velocities[i];
    }
    velocity /= density;

    float speed = length(velocity);

    // Normalize speed for color mapping
    float normalizedSpeed = speed / U0;
    normalizedSpeed = clamp(normalizedSpeed, 0.0, 1.0);

    // Map normalized speed to color (e.g., from blue to red)
    vec3 color = vec3(normalizedSpeed, 0.0, 1.0 - normalizedSpeed);

    FragColor = vec4(color, 1.0);
}
)glsl";

const char* kVertexShader = R"glsl(
#version 460 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

void main()
{
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";