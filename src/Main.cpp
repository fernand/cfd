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

const ivec2 velocities[9] = ivec2[9](
    ivec2(-1, 1), ivec2(0, 1), ivec2(1, 1),
    ivec2(-1, 0), ivec2(0, 0), ivec2(1, 0),
    ivec2(-1, -1), ivec2(0, -1), ivec2(1, -1)
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

    if (isSolid(gid.x, gid.y)) {
        // Bounce-back boundary condition for solid
        for (int i = 0; i < 9; i++) {
            f_out[index * 9 + opp[i]] = f_in[index * 9 + i];
        }
        return;
    }

    // Streaming step (pull from neighbors)
    float f[9];
    for (int i = 0; i < 9; i++) {
        ivec2 neighborPos = gid - velocities[i];
        if (neighborPos.x > 0 && neighborPos.x < width - 1 && neighborPos.y > 0 && neighborPos.y < height - 1) {
            int neighborIndex = neighborPos.y * width + neighborPos.x;
            f[i] = f_in[neighborIndex * 9 + i];
        } else {
            // Equilibrium boundaries
            if (neighborPos.x == 0 || neighborPos.x == width - 1 || neighborPos.y == 0 || neighborPos.y == height - 1) {
                float density = 1.0;
                vec2 velocity = vec2(U0, 0.0);
                float velDotC = dot(vec2(velocities[i]), velocity);
                float velSq = dot(velocity, velocity);
                f[i] = weights[i] * density * (1.0 + 3.0 * velDotC +
                                4.5 * velDotC * velDotC - 1.5 * velSq);
            }
        }
    }

    // Compute density and velocity
    float density = 0.0;
    vec2 velocity = vec2(0.0);
    for (int i = 0; i < 9; i++) {
        density += f[i];
        velocity += f[i] * vec2(velocities[i]);
    }
    velocity /= density;

    // Collision step
    float feq[9];
    for (int i = 0; i < 9; i++) {
        float velDotC = dot(vec2(velocities[i]), velocity);
        float velSq = dot(velocity, velocity);
        feq[i] = weights[i] * density * (1.0 + 3.0 * velDotC + 4.5 * velDotC * velDotC - 1.5 * velSq);
    }

    for (int i = 0; i < 9; i++) {
        f_out[index * 9 + i] = f[i] - (f[i] - feq[i]) / tau;
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

layout(std430, binding = 1) buffer DF_In {
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

// Function to convert normalized values to RGB
vec3 color_from_floats(float red, float green, float blue) {
    return vec3(clamp(red, 0.0, 1.0), clamp(green, 0.0, 1.0), clamp(blue, 0.0, 1.0));
}

// Colorscale rainbow
vec3 colorscale_rainbow(float x) {
    x = clamp(6.0 * (1.0 - x), 0.0, 6.0);
    vec3 color = vec3(0.0, 0.0, 0.0);

    if (x < 1.2) {
        color = vec3(1.0, x * 0.83333333, 0.0);
    } else if (x < 2.0) {
        color = vec3(2.5 - x * 1.25, 1.0, 0.0);
    } else if (x < 3.0) {
        color = vec3(0.0, 1.0, x - 2.0);
    } else if (x < 4.0) {
        color = vec3(0.0, 4.0 - x, 1.0);
    } else if (x < 5.0) {
        color = vec3(x * 0.4 - 1.6, 0.0, 3.0 - x * 0.5);
    } else {
        color = vec3(2.4 - x * 0.4, 0.0, 3.0 - x * 0.5);
    }

    return color;
}

// Colorscale iron
vec3 colorscale_iron(float x) {
    x = clamp(4.0 * (1.0 - x), 0.0, 4.0);
    vec3 color = vec3(1.0, 0.0, 0.0);

    if (x < 0.66666667) {
        color.g = 1.0;
        color.b = 1.0 - x * 1.5;
    } else if (x < 2.0) {
        color.g = 1.5 - x * 0.75;
    } else if (x < 3.0) {
        color.r = 2.0 - x * 0.5;
        color.b = x - 2.0;
    } else {
        color.r = 2.0 - x * 0.5;
        color.b = 4.0 - x;
    }

    return color;
}

void main() {
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

    if (isinf(speed) || isinf(density)) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0); // Black for invalid values
        return;
    }

    float normalized_v = clamp(speed / U0, 0.0, 1.0);

    // Choose one of the color scales (e.g., rainbow)
    vec3 color = colorscale_rainbow(normalized_v);

    // Alternatively, use the iron colorscale
    // vec3 color = colorscale_iron(normalized_v);

    // Output the final color
    FragColor = vec4(color_from_floats(color.r, color.g, color.b), 1.0);
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

inline bool isInTriangle(float x, float y, HMM_Vec2 v1, HMM_Vec2 v2, HMM_Vec2 v3)
{
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

    const int width = 512 * 4;
    const int height = 512;
    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    GLFWwindow* window = glfwCreateWindow(width, height, "CFD", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    int kWindowWidth = 0;
    int kWindowHeight = 0;
    glfwGetWindowSize(window, &kWindowWidth, &kWindowHeight);
    // glfwSwapInterval(1); // Enable vsync

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

    float U0 = 0.075f;            // Initial velocity slightly
    const float L = 128;          // Characteristic length
    const float Re = 100.0f;      // Reynolds number
    float nu = U0 * L / Re;       // kinematic viscosity
    float tau = 3.0f * nu + 0.5f; // relaxation time

    // Initialize distribution functions with a uniform flow from left to right
    float rho0 = 1.0f;
    float ux0 = U0;
    float uy0 = 0.0f;

    float centerX = 380;
    float centerY = 512.0f / 2;
    float wingLength = 680 / 4;
    float wingHeight = 320 / 4;

    HMM_Vec2 v1 = {centerX - wingLength / 2, centerY};                  // tip
    HMM_Vec2 v2 = {centerX + wingLength / 2, centerY - wingHeight / 2}; // bottom right
    HMM_Vec2 v3 = {centerX + wingLength / 2, centerY + wingHeight / 2}; // top right

    std::vector<float> f_in(width * height * num_velocities);
    std::vector<uint32_t> solid_cells((width * height + 31) / 32, 0);
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int idx = (y * width + x) * num_velocities;

            float ux, uy;
            if (isInTriangle(x, y, v1, v2, v3))
            {
                ux = 0;
                uy = 0;

                int bit_index = y * width + x;
                solid_cells[bit_index / 32] |= (1u << (bit_index % 32));
            }
            else
            {
                ux = ux0;
                uy = uy0;
            }

            // Calculate equilibrium distribution
            for (int i = 0; i < num_velocities; i++)
            {
                float cu = velocities[i].X * ux + velocities[i].Y * uy;
                float usqr = ux * ux + uy * uy;
                f_in[idx + i] =
                    weights[i] * rho0 * (1.0f + 3.0f * cu + 4.5f * cu * cu - 1.5f * usqr);
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
        glUseProgram(compute_program);
        glUniform1i(glGetUniformLocation(compute_program, "width"), width);
        glUniform1i(glGetUniformLocation(compute_program, "height"), height);
        glUniform1f(glGetUniformLocation(compute_program, "U0"), U0);
        glUniform1f(glGetUniformLocation(compute_program, "tau"), tau);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[0]);      // f_in
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo[1]);      // f_out
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, solid_buffer); // solid cells
        glDispatchCompute(width / 16, height / 16, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(render_program);
        glUniform1i(glGetUniformLocation(render_program, "width"), width);
        glUniform1i(glGetUniformLocation(render_program, "height"), height);
        glUniform1f(glGetUniformLocation(render_program, "U0"), U0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo[1]); // f_out
        glBindVertexArray(quadVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        std::swap(ssbo[0], ssbo[1]);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Dbg");
        ImGui::Text("FPS: %.0f", ImGui::GetIO().Framerate);
        ImGui::Text("Tau: %.2f", tau);
        ImGui::End();

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