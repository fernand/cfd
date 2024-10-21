#pragma once

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include <glad/gl.h>

static const char* ErrorToString(const GLenum errorCode)
{
    switch (errorCode)
    {
    case GL_NO_ERROR:
        return "GL_NO_ERROR";
    case GL_INVALID_ENUM:
        return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:
        return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:
        return "GL_INVALID_OPERATION";
    case GL_OUT_OF_MEMORY:
        return "GL_OUT_OF_MEMORY";
    case GL_STACK_UNDERFLOW:
        return "GL_STACK_UNDERFLOW"; // Legacy; not used on GL3+
    case GL_STACK_OVERFLOW:
        return "GL_STACK_OVERFLOW"; // Legacy; not used on GL3+
    default:
        return "Unknown GL error";
    }
}

static inline void CheckGLError(const char* file, const int line)
{
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR)
    {
        printf("%s(%d) : GL_CORE_ERROR=0x%X - %s\n", file, line, err, ErrorToString(err));
        assert(false);
        exit(1);
    }
}

static void CompileShader(const GLuint shader)
{
    glCompileShader(shader);
    CheckGLError(__FILE__, __LINE__);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    CheckGLError(__FILE__, __LINE__);

    if (status == GL_FALSE)
    {
        GLchar strInfoLog[1024] = {0};
        glGetShaderInfoLog(shader, sizeof(strInfoLog) - 1, nullptr, strInfoLog);
        printf("Shader compiler error: %s\n", strInfoLog);
        assert(false);
        exit(1);
    }
}

static void LinkProgram(const GLuint program)
{
    glLinkProgram(program);
    CheckGLError(__FILE__, __LINE__);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    CheckGLError(__FILE__, __LINE__);

    if (status == GL_FALSE)
    {
        GLchar strInfoLog[1024] = {0};
        glGetProgramInfoLog(program, sizeof(strInfoLog) - 1, nullptr, strInfoLog);
        printf("Program Linker error: %s\n", strInfoLog);
        assert(false);
        exit(1);
    }
}