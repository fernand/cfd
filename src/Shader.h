const char* kComputeShader = R"glsl(
#version 430 core

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
    1/36, 1/9, 1/36,
    1/9, 4/9, 1/9,
    1/36, 1/9, 1/36
);

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    int index = gid.y * width + gid.x;

    // Load distribution functions
    float f[9];
    for (int i = 0; i < 9; i++) {
        f[i] = f_in[index * 9 + i];
    }

    // Compute macroscopic variables: density and velocity
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

    // Streaming step (simple example without boundaries)
    ivec2 nextPos;
    for (int i = 0; i < 9; i++) {
        nextPos = gid - ivec2(velocities[i]);
        if (nextPos.x >= 0 && nextPos.x < width && nextPos.y >= 0 && nextPos.y < height) {
            int nextIndex = nextPos.y * width + nextPos.x;
            f_out[nextIndex * 9 + i] = f[i];
        }
    }
}
)glsl";