#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#pragma comment(lib, "glfw3.lib")

#include <HandmadeMath.h>

#define _USE_MATH_DEFINES
#include <math.h>
#include <vector>
#include <utility>

#include "OpenGLHelpers.h"
#include "Shader.h"

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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow)
{
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_RESIZABLE, false);
    glfwWindowHint(GLFW_DEPTH_BITS, 32);
    const char* glsl_version = "#version 430";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    const int width = 1920 / 2;
    const int height = 1080 / 2;
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

    const int num_velocities = 9; // D2Q9 model

    const HMM_Vec2 velocities[9] = {HMM_Vec2{-1, 1},  HMM_Vec2{0, 1},  HMM_Vec2{1, 1},
                                    HMM_Vec2{-1, 0},  HMM_Vec2{0, 0},  HMM_Vec2{1, 0},
                                    HMM_Vec2{-1, -1}, HMM_Vec2{0, -1}, HMM_Vec2{1, -1}};

    const float weights[9] = {1.0f / 36.0f, 1.0f / 9.0f,  1.0f / 36.0f, 1.0f / 9.0f, 4.0f / 9.0f,
                              1.0f / 9.0f,  1.0f / 36.0f, 1.0f / 9.0f,  1.0f / 36.0f};

    const float L = 1.0f;       // Domain length
    const float U0 = 0.1f;      // Velocity amplitude
    const float dx = L / width; // Lattice spacing
    const float dy = L / height;
    const float rho0 = 1.0f; // Initial density

    GLuint ssbo[2];
    glGenBuffers(2, ssbo);

    size_t bufferSize = width * height * num_velocities * sizeof(float);

    // SSBO for current distribution functions
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, NULL, GL_DYNAMIC_DRAW);

    // SSBO for updated distribution functions
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, NULL, GL_DYNAMIC_DRAW);

    GLuint compute_shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(compute_shader, 1, &kComputeShader, nullptr);
    CompileShader(compute_shader);
    GLuint compute_program = glCreateProgram();
    glAttachShader(compute_program, compute_shader);
    LinkProgram(compute_program);

    std::vector<float> f_in(width * height * num_velocities);

    // Loop over each lattice point
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            // Compute physical coordinates
            float xpos = x * dx;
            float ypos = y * dy;

            // Compute initial velocity components
            float u = U0 * sin(2.0f * M_PI * xpos / L) * cos(2.0f * M_PI * ypos / L);
            float v = -U0 * cos(2.0f * M_PI * xpos / L) * sin(2.0f * M_PI * ypos / L);

            // Compute equilibrium distribution functions
            float feq[num_velocities];
            float velocity[2] = {u, v};
            float density = rho0;

            for (int i = 0; i < num_velocities; i++)
            {
                float ei_dot_u = velocities[i].X * u + velocities[i].Y * v;
                float u_sq = u * u + v * v;
                feq[i] = weights[i] * density *
                         (1.0f + 3.0f * ei_dot_u + 4.5f * ei_dot_u * ei_dot_u - 1.5f * u_sq);
                // Store in the distribution function array
                int index = (y * width + x) * num_velocities + i;
                f_in[index] = feq[i];
            }
        }
    }

    // Upload the initialized distribution functions to the GPU
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[0]);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, f_in.data());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[1]);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, f_in.data());


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
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[0]); // f_in
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo[1]); // f_out
        glUniform1i(glGetUniformLocation(compute_program, "width"), width);
        glUniform1i(glGetUniformLocation(compute_program, "height"), height);
        glUniform1f(glGetUniformLocation(compute_program, "tau"), 0.6f);
        glDispatchCompute(width / 16, height / 16, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

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

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}