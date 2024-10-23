#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#pragma comment(lib, "glfw3.lib")

#include <HandmadeMath.h>

#include <imgui/imgui.h>
#include <imgui/imgui_stdlib.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

#define _USE_MATH_DEFINES
#include <math.h>
#include <vector>
#include <utility>

#include "OpenGLHelpers.h"

const char* kComputeShader = R"glsl(
#version 460 core

layout(local_size_x = 16, local_size_y = 16) in;

layout(std430, binding = 0) buffer DF_In {
    float f_in[];
};

layout(std430, binding = 1) buffer DF_Out {
    float f_out[];
};

layout(std430, binding = 2) buffer SolidCells {
    uint solid_bits[];
};

uniform int width;
uniform int height;
uniform float U0;
uniform float tau;

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

const int opp[9] = int[9](8, 7, 6, 5, 4, 3, 2, 1, 0);

bool isSolid(int x, int y) {
    int bit_index = y * width + x;
    uint word_index = bit_index / 32;
    uint bit_offset = bit_index % 32;
    return (solid_bits[word_index] & (1u << bit_offset)) != 0u;
}

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    int index = gid.y * width + gid.x;

    // Check if current point is solid using bit buffer
    if (isSolid(gid.x, gid.y)) {
        // Bounce-back boundary condition for solid
        for (int i = 0; i < 9; i++) {
            f_out[index * 9 + i] = f_in[index * 9 + opp[i]];
        }
        return;
    }

    // Regular LBM steps for fluid regions
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

    // Boundary handling goes here
    if (gid.x == width - 1 || gid.x == 0) { // Left and right boundaries
        velocity = vec2(-U0, 0.0); // Negative velocity for flow to the left
        density = 1.0;
        // Set equilibrium distribution for inlet
        for (int i = 0; i < 9; i++) {
            float velDotC = dot(velocities[i], velocity);
            float velSq = dot(velocity, velocity);
            f[i] = weights[i] * density * (1.0 + 3.0 * velDotC + 4.5 * velDotC * velDotC - 1.5 * velSq);
        }
    }
    else if (gid.y == 0 || gid.y == height-1) { // Top and bottom boundaries
        // No-slip boundary condition (should it be no slip???)
        velocity = vec2(0.0, 0.0);
        // Set equilibrium distribution for wall
        for (int i = 0; i < 9; i++) {
            float velDotC = dot(velocities[i], velocity);
            float velSq = dot(velocity, velocity);
            f[i] = weights[i] * density * (1.0 + 3.0 * velDotC + 4.5 * velDotC * velDotC - 1.5 * velSq);
        }
    }

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

    // Streaming step
    for (int i = 0; i < 9; i++) {
        ivec2 nextPos = gid + ivec2(velocities[i]);

        // Handle boundaries
        nextPos.x = clamp(nextPos.x, 0, width - 1);
        nextPos.y = clamp(nextPos.y, 0, height - 1);

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

layout(std430, binding = 2) buffer SolidCells {
    uint solid_bits[];
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

    bool solid = (solid_bits[(y * width + x) / 32] & (1u << ((y * width + x) % 32))) != 0u;
    if (solid) {
        FragColor = vec4(0.5, 0.5, 0.5, 1.0);  // Gray for solid
        return;
    }

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
    float vx = velocity.x;
    float vy = velocity.y;

    if (isinf(speed) || isinf(density)) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0); // Black for invalid values
        return;
    }

    // Normalize speed for color mapping
    float normalizedSpeed = speed / U0;
    normalizedSpeed = clamp(normalizedSpeed, 0.0, 1.0);

    // Calculate vorticity (curl of velocity) using central differences
    float dvx_dy = 0.0;
    float dvy_dx = 0.0;
    if (x > 0 && x < width-1 && y > 0 && y < height-1) {
        int idx_up = ((y+1) * width + x) * 9;
        int idx_down = ((y-1) * width + x) * 9;
        int idx_right = (y * width + (x+1)) * 9;
        int idx_left = (y * width + (x-1)) * 9;

        float density_up = 0.0, density_down = 0.0, density_right = 0.0, density_left = 0.0;
        vec2 vel_up = vec2(0.0), vel_down = vec2(0.0), vel_right = vec2(0.0), vel_left = vec2(0.0);

        for (int i = 0; i < 9; i++) {
            density_up += f_in[idx_up + i];
            density_down += f_in[idx_down + i];
            density_right += f_in[idx_right + i];
            density_left += f_in[idx_left + i];

            vel_up += f_in[idx_up + i] * velocities[i];
            vel_down += f_in[idx_down + i] * velocities[i];
            vel_right += f_in[idx_right + i] * velocities[i];
            vel_left += f_in[idx_left + i] * velocities[i];
        }

        vel_up /= density_up;
        vel_down /= density_down;
        vel_right /= density_right;
        vel_left /= density_left;

        dvx_dy = (vel_up.x - vel_down.x) / 2.0;
        dvy_dx = (vel_right.y - vel_left.y) / 2.0;
    }

    float vorticity = dvx_dy - dvy_dx;

    normalizedSpeed = clamp(speed / U0, 0.0, 1.0);

    // Color mapping
    vec3 color = vec3(normalizedSpeed, 0.0, 1.0 - normalizedSpeed);

    // Add vorticity visualization
    float vort_intensity = abs(vorticity) * 0.5;
    color = mix(color, vec3(0.0, 1.0, 0.0) * sign(vorticity), clamp(vort_intensity, 0.0, 0.3));

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

// clang-format off
float quadVertices[] = {
    // Positions    // Texture Coords
    -1.0f,  1.0f,   0.0f, 1.0f, // Top-left
    -1.0f, -1.0f,    0.0f, 0.0f, // Bottom-left
    1.0f, -1.0f,    1.0f, 0.0f, // Bottom-right
    1.0f,  1.0f,    1.0f, 1.0f  // Top-right
};
// clang-format on

unsigned int quadIndices[] = {0, 1, 2, 0, 2, 3};

bool isInsideTriangle(float x, float y, float width, float height)
{
    // Triangle vertices (wing pointing left)
    float centerX = width / 2.0f;
    float centerY = height / 2.0f;
    float wingLength = width / 2.0f;
    float wingHeight = wingLength / 4.0f; // aspect ratio of 4:1

    // Triangle vertices
    HMM_Vec2 v1 = {centerX - wingLength / 2, centerY};                  // tip
    HMM_Vec2 v2 = {centerX + wingLength / 2, centerY - wingHeight / 2}; // bottom right
    HMM_Vec2 v3 = {centerX + wingLength / 2, centerY + wingHeight / 2}; // top right

    // Barycentric coordinate calculation
    float d = (v2.Y - v3.Y) * (v1.X - v3.X) + (v3.X - v2.X) * (v1.Y - v3.Y);
    float a = ((v2.Y - v3.Y) * (x - v3.X) + (v3.X - v2.X) * (y - v3.Y)) / d;
    float b = ((v3.Y - v1.Y) * (x - v3.X) + (v1.X - v3.X) * (y - v3.Y)) / d;
    float c = 1 - a - b;

    return a >= 0 && a <= 1 && b >= 0 && b <= 1 && c >= 0 && c <= 1;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow)
{
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_RESIZABLE, false);
    glfwWindowHint(GLFW_DEPTH_BITS, 32);
    const char* glsl_version = "#version 460";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    const int width = 1920;
    const int height = 1080;
    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    GLFWwindow* window = glfwCreateWindow(width, height, "CFD", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    int kWindowWidth = 0;
    int kWindowHeight = 0;
    glfwGetWindowSize(window, &kWindowWidth, &kWindowHeight);
    glfwSwapInterval(1); // Enable vsync

    bool glad_err = gladLoadGL(glfwGetProcAddress) == 0;
    if (glad_err)
        return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    const float kFontSize = 32;
    ImFont* im_font = io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\SegoeUI.ttf)", kFontSize);
    IM_ASSERT(im_font != nullptr);
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    const int num_velocities = 9; // D2Q9 model

    const HMM_Vec2 velocities[9] = {HMM_Vec2{-1, 1},  HMM_Vec2{0, 1},  HMM_Vec2{1, 1},
                                    HMM_Vec2{-1, 0},  HMM_Vec2{0, 0},  HMM_Vec2{1, 0},
                                    HMM_Vec2{-1, -1}, HMM_Vec2{0, -1}, HMM_Vec2{1, -1}};

    const float weights[9] = {1.0f / 36.0f, 1.0f / 9.0f,  1.0f / 36.0f, 1.0f / 9.0f, 4.0f / 9.0f,
                              1.0f / 9.0f,  1.0f / 36.0f, 1.0f / 9.0f,  1.0f / 36.0f};

    GLuint ssbo[2];
    glGenBuffers(2, ssbo);

    size_t bufferSize = width * height * num_velocities * sizeof(float);

    // SSBO for current distribution functions
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[0]);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER, bufferSize, NULL, GL_DYNAMIC_STORAGE_BIT);

    // SSBO for updated distribution functions
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[1]);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER, bufferSize, NULL, GL_DYNAMIC_STORAGE_BIT);

    GLuint compute_shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(compute_shader, 1, &kComputeShader, nullptr);
    CompileShader(compute_shader);
    GLuint compute_program = glCreateProgram();
    glAttachShader(compute_program, compute_shader);
    LinkProgram(compute_program);

    float U0 = 0.1f;               // Flow velocity
    const float L = width / 10.0f; // Characteristic length
    const float Re = 100.0f;       // Reynolds number
    float nu = U0 * L / Re;        // kinematic viscosity
    float tau = 3.0f * nu + 0.5f;  // relaxation time

    // Initialize distribution functions with a uniform flow from right to left
    float rho0 = 1.0f;
    float ux0 = -U0; // Negative velocity for flow from right to left
    float uy0 = 0.0f;

    std::vector<float> f_in(width * height * num_velocities);
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int idx = (y * width + x) * num_velocities;
            float ux = ux0;
            float uy = uy0;
            float rho = rho0;

            if (isInsideTriangle(x, y, width, height))
            {
                // Zero velocity for solid nodes (wing)
                ux = 0.0f;
                uy = 0.0f;
            }

            // Calculate equilibrium distribution
            for (int i = 0; i < num_velocities; i++)
            {
                float cu = velocities[i].X * ux + velocities[i].Y * uy;
                float usqr = ux * ux + uy * uy;
                f_in[idx + i] =
                    weights[i] * rho * (1.0f + 3.0f * cu + 4.5f * cu * cu - 1.5f * usqr);
            }
        }
    }

    std::vector<uint32_t> solid_cells((width * height + 31) / 32, 0);

    // Initialize the bit buffer for the delta wing
    float centerX = width / 2.0f;
    float centerY = height / 2.0f;
    float wingLength = width / 2.0f;
    float wingHeight = wingLength / 4.0f;

    HMM_Vec2 v1 = {centerX - wingLength / 2, centerY};                  // tip
    HMM_Vec2 v2 = {centerX + wingLength / 2, centerY - wingHeight / 2}; // bottom right
    HMM_Vec2 v3 = {centerX + wingLength / 2, centerY + wingHeight / 2}; // top right

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            // Barycentric coordinate calculation
            float d = (v2.Y - v3.Y) * (v1.X - v3.X) + (v3.X - v2.X) * (v1.Y - v3.Y);
            float a = ((v2.Y - v3.Y) * (x - v3.X) + (v3.X - v2.X) * (y - v3.Y)) / d;
            float b = ((v3.Y - v1.Y) * (x - v3.X) + (v1.X - v3.X) * (y - v3.Y)) / d;
            float c = 1 - a - b;

            if (a >= 0 && a <= 1 && b >= 0 && b <= 1 && c >= 0 && c <= 1)
            {
                int bit_index = y * width + x;
                solid_cells[bit_index / 32] |= (1u << (bit_index % 32));
            }
        }
    }

    // Create and initialize the solid cells buffer
    GLuint solid_buffer;
    glGenBuffers(1, &solid_buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, solid_buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, solid_cells.size() * sizeof(uint32_t),
                 solid_cells.data(), GL_STATIC_DRAW);

    // Upload the initialized distribution functions to the GPU
    glNamedBufferSubData(ssbo[0], 0, bufferSize, f_in.data());
    glNamedBufferSubData(ssbo[1], 0, bufferSize, f_in.data());

    GLuint quadVAO, quadVBO, quadEBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glGenBuffers(1, &quadEBO);

    glBindVertexArray(quadVAO);

    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    // Texture Coordinate attribute
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &kVertexShader, nullptr);
    CompileShader(vertex_shader);
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(fragment_shader, 1, &kFragmentShader, nullptr);
    CompileShader(fragment_shader);
    GLuint render_program = glCreateProgram();

    glAttachShader(render_program, vertex_shader);
    glAttachShader(render_program, fragment_shader);
    LinkProgram(render_program);

    while (!glfwWindowShouldClose(window))
    {
        // Run the compute shader
        glUseProgram(compute_program);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[0]);      // f_in
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo[1]);      // f_out
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, solid_buffer); // solid cells
        glUniform1i(glGetUniformLocation(compute_program, "width"), width);
        glUniform1i(glGetUniformLocation(compute_program, "height"), height);
        glUniform1f(glGetUniformLocation(compute_program, "U0"), U0);
        glUniform1f(glGetUniformLocation(compute_program, "tau"), tau);
        glDispatchCompute(width / 16, height / 16, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

        // Render the visualization
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(render_program);
        glUniform1i(glGetUniformLocation(render_program, "width"), width);
        glUniform1i(glGetUniformLocation(render_program, "height"), height);
        glUniform1f(glGetUniformLocation(render_program, "U0"), U0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[1]); // The latest data is in ssbo[1]
        glBindVertexArray(quadVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        // Swap the SSBOs for the next iteration
        std::swap(ssbo[0], ssbo[1]);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::SliderFloat("U0", &U0, 0.01f, 0.2f);
        ImGui::SliderFloat("tau", &tau, 0.6f, 2.0f);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}