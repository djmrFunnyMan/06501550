#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>

static const char* vs_src = R"(
#version 330 core

const vec2 verts[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

out vec2 uv;

void main()
{
    vec2 p = verts[gl_VertexID];
    uv = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

static const char* fs_src = R"(
#version 330 core

in vec2 uv;
out vec4 FragColor;

const float pi = 3.14159265358979323846;

float sinM(float x)
{
    return sin(mod(x, 2.0 * pi));
}

float randA(vec2 inCoord)
{
    return fract(sinM(dot(inCoord, vec2(23.53, 44.0))) * 42350.45);
}

float randB(vec2 inCoord)
{
    return fract(sinM(dot(inCoord, vec2(23.5, 44.0))) * 42350.45);
}

float perlinA(vec2 inCoord)
{
    vec2 i = floor(inCoord);
    vec2 j = fract(inCoord);
    vec2 coord = smoothstep(0.0, 1.0, j);

    float a = randA(i);
    float b = randA(i + vec2(1.0, 0.0));
    float c = randA(i + vec2(0.0, 1.0));
    float d = randA(i + vec2(1.0, 1.0));

    return mix(mix(a, b, coord.x), mix(c, d, coord.x), coord.y);
}

float perlinB(vec2 inCoord)
{
    vec2 i = floor(inCoord);
    vec2 j = fract(inCoord);
    vec2 coord = smoothstep(0.0, 1.0, j);

    float a = randB(i);
    float b = randB(i + vec2(1.0, 0.0));
    float c = randB(i + vec2(0.0, 1.0));
    float d = randB(i + vec2(1.0, 1.0));

    return mix(mix(a, b, coord.x), mix(c, d, coord.x), coord.y);
}

void main()
{
    vec2 gridUV = uv;

    if (uv.x < 0.5)
        gridUV.x = uv.x * 2.0;
    else
        gridUV.x = (uv.x - 0.5) * 2.0;

    vec2 inCoord = gridUV * 16.0;

    float v = (uv.x < 0.5) ? perlinA(inCoord) : perlinB(inCoord);

    vec3 color = vec3(v);

    float divider = 1.0 - smoothstep(0.0, 0.002, abs(uv.x - 0.5));
    color = mix(color, vec3(1.0), divider);

    FragColor = vec4(color, 1.0);
}
)";

GLuint compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

    GLint logLen = 0;
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &logLen);
    if (logLen > 1) {
        std::vector<char> log(logLen);
        glGetShaderInfoLog(s, logLen, nullptr, log.data());
        std::cout << (type == GL_VERTEX_SHADER ? "VS" : "FS")
                  << " log:\n" << log.data() << "\n";
    }

    if (!ok)
        std::cout << "Shader compilation failed.\n";

    return s;
}

int main()
{
    if (!glfwInit()) {
        std::cerr << "glfwInit failed\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(1200, 900, "Case 06501550 repro | 1 = Left: perlinA (23.53) | 2 = Right: perlinB (23.5)", nullptr, nullptr);
    if (!win) {
        std::cerr << "glfwCreateWindow failed\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(win);

    GLenum glew_ok = glewInit();
    if (glew_ok != GLEW_OK) {
        std::cerr << "glewInit failed: " << glewGetErrorString(glew_ok) << "\n";
        glfwTerminate();
        return -1;
    }

    std::cout << "Vendor   : " << glGetString(GL_VENDOR) << "\n";
    std::cout << "Renderer : " << glGetString(GL_RENDERER) << "\n";
    std::cout << "Version  : " << glGetString(GL_VERSION) << "\n";
    std::cout << "GLSL     : " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";

    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);

    GLint logLen = 0;
    glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
    if (logLen > 1) {
        std::vector<char> log(logLen);
        glGetProgramInfoLog(prog, logLen, nullptr, log.data());
        std::cout << "Program log:\n" << log.data() << "\n";
    }

    if (!linked) {
        std::cerr << "Program link failed\n";
        glfwTerminate();
        return -1;
    }

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glUseProgram(prog);

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(win, GLFW_TRUE);

        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(win, GLFW_TRUE);

        int w = 0, h = 0;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);

        glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(win);
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glfwTerminate();
    return 0;
}