#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <chrono>

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

uniform vec2 gridOffset;

float current_float = 23;  // manually test these values: (23) | (23.5) | (23.75) | (23.625) | (23.03125) | (23.000488) | (23.53)

const float pi = 3.14159265358979323846;

float sinM(float x)
{
    return sin(mod(x, 2.0 * pi));
}

float randA(vec2 inCoord)
{
    return fract(sinM(dot(inCoord, vec2(current_float, 44.0))) * 42350.45);
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


void main()
{
    vec2 gridUV = uv;

    gridUV.x = uv.x * 2.0;

    vec2 inCoord = gridUV * 64.0 - gridOffset;

    float v = perlinA(inCoord);

    vec3 color = vec3(v);

    float divider = 0.0;
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

    GLFWwindow* win = glfwCreateWindow(1200, 900, "Case 06501550 repro | UP Arrow = High Speed | DOWN Arrow = Low Speed | LEFT Arrow & Right Arrow = Apply Offset", nullptr, nullptr);
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
    int gridOffsetLoc = glGetUniformLocation(prog, "gridOffset");
    float offsetX = 0.0f;
    float offsetY = 0.0f;
	float speed1 = 10000.0f;

    auto prefFrameTime = std::chrono::system_clock::now();

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        auto now = std::chrono::system_clock::now();
        auto timeDelta = now - prefFrameTime;
        float speed = 16.0 * ((double) (timeDelta.count()) / speed1);

        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(win, GLFW_TRUE);

        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(win, GLFW_TRUE);

        if (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) {
            offsetX -= speed;
			offsetY += speed;
		}
        if (glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS) {
            offsetX += speed;
			offsetY -= speed;
		}
		if (glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS)
            speed1 = 2000000.0;
		
		if (glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS)
            speed1 = 10000.0;

        int w = 0, h = 0;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);

        glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUniform2f(gridOffsetLoc, offsetX, offsetY);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(win);

        prefFrameTime = now;
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glfwTerminate();
    return 0;
}
