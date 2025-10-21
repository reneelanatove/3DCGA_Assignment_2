//#include "Image.h"
#include "mesh.h"
#include "texture.h"
// Always include window first (because it includes glfw, which includes GL which needs to be included AFTER glew).
// Can't wait for modules to fix this stuff...
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glad/glad.h>
// Include glad before glfw3
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()
#include <framework/shader.h>
#include <framework/window.h>
#include <framework/trackball.h>
#include <fmt/format.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <string>
#include <memory>
#include <vector>

class Application {
public:
    Application()
        : m_window("Final Project", glm::ivec2(1024, 1024), OpenGLVersion::GL41)
        , m_texture(RESOURCE_ROOT "resources/checkerboard.png")
    {
        m_window.registerKeyCallback([this](int key, int scancode, int action, int mods) {
            if (action == GLFW_PRESS)
                onKeyPressed(key, mods);
            else if (action == GLFW_RELEASE)
                onKeyReleased(key, mods);
        });
        m_meshes = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/dragon.obj");
        try {
            ShaderBuilder defaultBuilder;
            defaultBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/shader_vert.glsl");
            defaultBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shader_frag.glsl");
            m_defaultShader = defaultBuilder.build();

            ShaderBuilder shadowBuilder;
            shadowBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/shadow_vert.glsl");
            shadowBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "Shaders/shadow_frag.glsl");
            m_shadowShader = shadowBuilder.build();

            ShaderBuilder lightBuilder;
            lightBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/light_marker_vert.glsl");
            lightBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/light_marker_frag.glsl");
            m_lightShader = lightBuilder.build();

            // Any new shaders can be added below in similar fashion.
            // ==> Don't forget to reconfigure CMake when you do!
            //     Visual Studio: PROJECT => Generate Cache for ComputerGraphics
            //     VS Code: ctrl + shift + p => CMake: Configure => enter
            // ....
        } catch (ShaderLoadingException e) {
            std::cerr << e.what() << std::endl;
        }

        initializeViews();
        initializeLightGeometry();
        resetLights();
        initializeLightPath();
        m_lastFrameTime = glfwGetTime();
    }

    ~Application()
    {
        if (m_lightVbo != 0)
            glDeleteBuffers(1, &m_lightVbo);
        if (m_lightVao != 0)
            glDeleteVertexArrays(1, &m_lightVao);
        if (m_lightPathVbo != 0)
            glDeleteBuffers(1, &m_lightPathVbo);
        if (m_lightPathVao != 0)
            glDeleteVertexArrays(1, &m_lightPathVao);
    }

    void update()
    {
        while (!m_window.shouldClose()) {
            // This is your game loop
            // Put your real-time logic and rendering in here
            m_window.updateInput();

            const double currentTime = glfwGetTime();
            float deltaTime = static_cast<float>(currentTime - m_lastFrameTime);
            m_lastFrameTime = currentTime;

            updateLightPath(deltaTime);

            renderGui();

            // Clear the screen
            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // ...
            glEnable(GL_DEPTH_TEST);

            Trackball& camera = activeTrackball();
            if (!m_views.empty()) {
                Viewpoint& viewState = m_views[m_activeViewIndex];
                viewState.lookAt = camera.lookAt();
                viewState.rotations = camera.rotationEulerAngles();
                viewState.distance = camera.distanceFromLookAt();
            }
            m_viewMatrix = camera.viewMatrix();
            m_projectionMatrix = camera.projectionMatrix();
            const glm::mat4 mvpMatrix = m_projectionMatrix * m_viewMatrix * m_modelMatrix;
            // Normals should be transformed differently than positions (ignoring translations + dealing with scaling):
            // https://paroj.github.io/gltut/Illumination/Tut09%20Normal%20Transformation.html
            const glm::mat3 normalModelMatrix = glm::inverseTranspose(glm::mat3(m_modelMatrix));

            for (GPUMesh& mesh : m_meshes) {
                m_defaultShader.bind();
                glUniformMatrix4fv(m_defaultShader.getUniformLocation("mvpMatrix"), 1, GL_FALSE, glm::value_ptr(mvpMatrix));
                glUniformMatrix4fv(m_defaultShader.getUniformLocation("modelMatrix"), 1, GL_FALSE, glm::value_ptr(m_modelMatrix));
                glUniformMatrix3fv(m_defaultShader.getUniformLocation("normalModelMatrix"), 1, GL_FALSE, glm::value_ptr(normalModelMatrix));
                if (mesh.hasTextureCoords()) {
                    m_texture.bind(GL_TEXTURE0);
                    glUniform1i(m_defaultShader.getUniformLocation("colorMap"), 0);
                    glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_TRUE);
                    glUniform1i(m_defaultShader.getUniformLocation("useMaterial"), GL_FALSE);
                } else {
                    glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_FALSE);
                    glUniform1i(m_defaultShader.getUniformLocation("useMaterial"), m_useMaterial);
                }
                glUniform1i(m_defaultShader.getUniformLocation("shadingMode"), static_cast<int>(m_shadingModel));
                glUniform3fv(m_defaultShader.getUniformLocation("customDiffuseColor"), 1, glm::value_ptr(m_customDiffuseColor));
                glUniform3fv(m_defaultShader.getUniformLocation("viewPosition"), 1, glm::value_ptr(camera.position()));
                glUniform3fv(m_defaultShader.getUniformLocation("specularColor"), 1, glm::value_ptr(m_specularColor));
                glUniform1f(m_defaultShader.getUniformLocation("specularStrength"), m_specularStrength);
                glUniform1f(m_defaultShader.getUniformLocation("specularShininess"), m_specularShininess);
                uploadLightsToShader();
                mesh.draw(m_defaultShader);
            }

            // Processes input and swaps the window buffer
            renderLightPath();
            renderLightMarkers();
            m_window.swapBuffers();
        }
    }

    // In here you can handle key presses
    // key - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__keys.html
    // mods - Any modifier keys pressed, like shift or control
    void onKeyPressed(int key, int mods)
    {
        std::cout << "Key pressed: " << key << std::endl;
    }

    // In here you can handle key releases
    // key - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__keys.html
    // mods - Any modifier keys pressed, like shift or control
    void onKeyReleased(int key, int mods)
    {
        std::cout << "Key released: " << key << std::endl;
    }

    // If the mouse is moved this function will be called with the x, y screen-coordinates of the mouse
    void onMouseMove(const glm::dvec2& cursorPos)
    {
        std::cout << "Mouse at position: " << cursorPos.x << " " << cursorPos.y << std::endl;
    }

    // If one of the mouse buttons is pressed this function will be called
    // button - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__buttons.html
    // mods - Any modifier buttons pressed
    void onMouseClicked(int button, int mods)
    {
        std::cout << "Pressed mouse button: " << button << std::endl;
    }

    // If one of the mouse buttons is released this function will be called
    // button - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__buttons.html
    // mods - Any modifier buttons pressed
    void onMouseReleased(int button, int mods)
    {
        std::cout << "Released mouse button: " << button << std::endl;
    }

private:
    Window m_window;

    // Shader for default rendering and for depth rendering
    Shader m_defaultShader;
    Shader m_shadowShader;

    Shader m_lightShader;

    struct Light {
        glm::vec3 position { 0.0f, 0.0f, 3.0f };
        glm::vec3 color { 1.0f };
        bool isSpotlight { false };
        bool peelsDepth { false };
        glm::vec3 direction { 0.0f, -1.0f, 0.0f };
        float spotCosCutoff { 0.9f };
        float spotSoftness { 0.1f };
        bool hasTexture { false };
        GLuint textureId { 0 };
    };

    struct BezierSegment {
        glm::vec3 p0;
        glm::vec3 p1;
        glm::vec3 p2;
        glm::vec3 p3;
    };

    struct PathSample {
        glm::vec3 position;
        glm::vec3 tangent;
        float cumulativeLength;
    };

    enum class ShadingModel : int {
        Unlit = 0,
        Lambert = 1,
        Phong = 2
    };

    std::vector<GPUMesh> m_meshes;
    Texture m_texture;
    bool m_useMaterial { true };
    ShadingModel m_shadingModel { ShadingModel::Lambert };
    glm::vec3 m_customDiffuseColor { 0.8f, 0.4f, 0.2f };
    glm::vec3 m_specularColor { 1.0f, 1.0f, 1.0f };
    float m_specularStrength { 1.0f };
    float m_specularShininess { 32.0f };
    std::vector<Light> m_lights;
    size_t m_selectedLightIndex { 0 };

    struct Viewpoint {
        std::string name;
        glm::vec3 lookAt { 0.0f };
        glm::vec3 rotations { 0.0f };
        float distance { 5.0f };
        glm::vec3 defaultLookAt { 0.0f };
        glm::vec3 defaultRotations { 0.0f };
        float defaultDistance { 5.0f };
    };
    std::vector<Viewpoint> m_views;
    size_t m_activeViewIndex { 0 };
    std::unique_ptr<Trackball> m_trackball;
    GLuint m_lightVao { 0 };
    GLuint m_lightVbo { 0 };
    GLsizei m_lightVertexCount { 0 };
    float m_lightMarkerScale { 0.1f };
    std::vector<BezierSegment> m_lightPathSegments;
    std::vector<PathSample> m_lightPathSamples;
    float m_lightPathTotalLength { 0.0f };
    bool m_lightPathEnabled { false };
    bool m_lightPathShowCurve { false };
    bool m_lightPathShowControlPoints { false };
    bool m_lightPathAimAtTarget { true };
    glm::vec3 m_lightPathTarget { 0.0f, 1.0f, 0.0f };
    float m_lightPathSpeed { 0.6f };
    float m_lightPathDistance { 0.0f };
    int m_lightPathFollowerIndex { 0 };
    GLuint m_lightPathVao { 0 };
    GLuint m_lightPathVbo { 0 };
    GLsizei m_lightPathVertexCount { 0 };
    double m_lastFrameTime { 0.0 };
    float m_lightPathControlPointScale { 0.12f };

    // Projection and view matrices for you to fill in and use
    glm::mat4 m_projectionMatrix = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 30.0f);
    glm::mat4 m_viewMatrix = glm::lookAt(glm::vec3(-1, 1, -1), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 m_modelMatrix { 1.0f };

    void initializeViews();
    Trackball& activeTrackball();
    void resetActiveView();
    void storeActiveViewState();
    void initializeLightGeometry();
    void renderLightMarkers();
    void initializeLightPath();
    void rebuildLightPathSamples();
    void uploadLightPathGeometry();
    glm::vec3 evaluateBezier(const BezierSegment& segment, float t) const;
    glm::vec3 evaluateBezierTangent(const BezierSegment& segment, float t) const;
    void updateLightPath(float deltaTime);
    void renderLightPath();
    void renderLightPathControlPoints();
    void ensureLightPathFollowerValid();
    void resetLights();
    void selectNextLight();
    void selectPreviousLight();
    void renderGui();
    void uploadLightsToShader();
};

void Application::initializeViews()
{
    m_views.clear();

    Viewpoint mainView;
    mainView.name = "Main View";
    mainView.defaultLookAt = glm::vec3(0.0f);
    mainView.defaultRotations = glm::vec3(glm::radians(20.0f), glm::radians(-30.0f), 0.0f);
    mainView.defaultDistance = 5.0f;
    mainView.lookAt = mainView.defaultLookAt;
    mainView.rotations = mainView.defaultRotations;
    mainView.distance = mainView.defaultDistance;

    m_views.push_back(std::move(mainView));
    m_activeViewIndex = 0;

    m_trackball = std::make_unique<Trackball>(&m_window, glm::radians(80.0f), m_views[0].lookAt, m_views[0].distance, m_views[0].rotations.x, m_views[0].rotations.y);
    m_trackball->setCamera(m_views[0].lookAt, m_views[0].rotations, m_views[0].distance);
}

Trackball& Application::activeTrackball()
{
    if (m_views.empty() || !m_trackball)
        initializeViews();

    if (m_activeViewIndex >= m_views.size())
        m_activeViewIndex = m_views.size() - 1;

    return *m_trackball;
}

void Application::resetActiveView()
{
    if (m_views.empty() || !m_trackball)
        return;

    Viewpoint& view = m_views[m_activeViewIndex];
    view.lookAt = view.defaultLookAt;
    view.rotations = view.defaultRotations;
    view.distance = view.defaultDistance;
    m_trackball->setCamera(view.lookAt, view.rotations, view.distance);
}

void Application::storeActiveViewState()
{
    if (m_views.empty() || !m_trackball)
        return;

    Viewpoint& view = m_views[m_activeViewIndex];
    view.lookAt = m_trackball->lookAt();
    view.rotations = m_trackball->rotationEulerAngles();
    view.distance = m_trackball->distanceFromLookAt();
}

void Application::initializeLightGeometry()
{
    if (m_lightVbo != 0)
        glDeleteBuffers(1, &m_lightVbo);
    if (m_lightVao != 0)
        glDeleteVertexArrays(1, &m_lightVao);

    const std::array<glm::vec3, 36> cubeVertices = {
        // Front face
        glm::vec3(-0.5f, -0.5f, 0.5f),
        glm::vec3(0.5f, -0.5f, 0.5f),
        glm::vec3(0.5f, 0.5f, 0.5f),
        glm::vec3(0.5f, 0.5f, 0.5f),
        glm::vec3(-0.5f, 0.5f, 0.5f),
        glm::vec3(-0.5f, -0.5f, 0.5f),
        // Back face
        glm::vec3(-0.5f, -0.5f, -0.5f),
        glm::vec3(-0.5f, 0.5f, -0.5f),
        glm::vec3(0.5f, 0.5f, -0.5f),
        glm::vec3(0.5f, 0.5f, -0.5f),
        glm::vec3(0.5f, -0.5f, -0.5f),
        glm::vec3(-0.5f, -0.5f, -0.5f),
        // Left face
        glm::vec3(-0.5f, -0.5f, -0.5f),
        glm::vec3(-0.5f, -0.5f, 0.5f),
        glm::vec3(-0.5f, 0.5f, 0.5f),
        glm::vec3(-0.5f, 0.5f, 0.5f),
        glm::vec3(-0.5f, 0.5f, -0.5f),
        glm::vec3(-0.5f, -0.5f, -0.5f),
        // Right face
        glm::vec3(0.5f, -0.5f, -0.5f),
        glm::vec3(0.5f, 0.5f, -0.5f),
        glm::vec3(0.5f, 0.5f, 0.5f),
        glm::vec3(0.5f, 0.5f, 0.5f),
        glm::vec3(0.5f, -0.5f, 0.5f),
        glm::vec3(0.5f, -0.5f, -0.5f),
        // Top face
        glm::vec3(-0.5f, 0.5f, 0.5f),
        glm::vec3(0.5f, 0.5f, 0.5f),
        glm::vec3(0.5f, 0.5f, -0.5f),
        glm::vec3(0.5f, 0.5f, -0.5f),
        glm::vec3(-0.5f, 0.5f, -0.5f),
        glm::vec3(-0.5f, 0.5f, 0.5f),
        // Bottom face
        glm::vec3(-0.5f, -0.5f, 0.5f),
        glm::vec3(-0.5f, -0.5f, -0.5f),
        glm::vec3(0.5f, -0.5f, -0.5f),
        glm::vec3(0.5f, -0.5f, -0.5f),
        glm::vec3(0.5f, -0.5f, 0.5f),
        glm::vec3(-0.5f, -0.5f, 0.5f)
    };

    m_lightVertexCount = static_cast<GLsizei>(cubeVertices.size());

    glGenVertexArrays(1, &m_lightVao);
    glGenBuffers(1, &m_lightVbo);

    glBindVertexArray(m_lightVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_lightVbo);
    glBufferData(GL_ARRAY_BUFFER, cubeVertices.size() * sizeof(glm::vec3), cubeVertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void*>(0));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

glm::vec3 Application::evaluateBezier(const BezierSegment& segment, float t) const
{
    float u = 1.0f - t;
    float u2 = u * u;
    float t2 = t * t;
    float u3 = u2 * u;
    float t3 = t2 * t;
    return u3 * segment.p0 + 3.0f * u2 * t * segment.p1 + 3.0f * u * t2 * segment.p2 + t3 * segment.p3;
}

glm::vec3 Application::evaluateBezierTangent(const BezierSegment& segment, float t) const
{
    float u = 1.0f - t;
    // Derivative of cubic Bezier
    glm::vec3 derivative = 3.0f * u * u * (segment.p1 - segment.p0)
        + 6.0f * u * t * (segment.p2 - segment.p1)
        + 3.0f * t * t * (segment.p3 - segment.p2);
    if (glm::dot(derivative, derivative) < 1e-6f)
        derivative = glm::vec3(0.0f, 1.0f, 0.0f);
    return derivative;
}

void Application::initializeLightPath()
{
    m_lightPathSegments.clear();
    m_lightPathSegments.push_back(BezierSegment {
        glm::vec3(2.5f, 1.5f, -1.5f),
        glm::vec3(3.5f, 2.5f, -0.5f),
        glm::vec3(2.5f, 1.0f, 1.5f),
        glm::vec3(1.5f, 1.5f, 2.5f) });
    m_lightPathSegments.push_back(BezierSegment {
        glm::vec3(1.5f, 1.5f, 2.5f),
        glm::vec3(0.5f, 2.5f, 3.5f),
        glm::vec3(-1.5f, 1.0f, 2.5f),
        glm::vec3(-2.5f, 1.5f, 0.0f) });
    m_lightPathSegments.push_back(BezierSegment {
        glm::vec3(-2.5f, 1.5f, 0.0f),
        glm::vec3(-3.0f, 0.5f, -2.0f),
        glm::vec3(-0.5f, 1.0f, -3.0f),
        glm::vec3(2.5f, 1.5f, -1.5f) });

    rebuildLightPathSamples();
    uploadLightPathGeometry();
    if (!m_lights.empty() && !m_lightPathSamples.empty()) {
        m_lights.front().position = m_lightPathSamples.front().position;
        if (m_lightPathAimAtTarget) {
            glm::vec3 toTarget = m_lightPathTarget - m_lights.front().position;
            if (glm::dot(toTarget, toTarget) > 1e-6f)
                m_lights.front().direction = glm::normalize(toTarget);
        }
    }
}

void Application::rebuildLightPathSamples()
{
    m_lightPathSamples.clear();
    m_lightPathTotalLength = 0.0f;
    if (m_lightPathSegments.empty())
        return;

    const int samplesPerSegment = 64;
    glm::vec3 previousPosition { 0.0f };
    bool hasPrevious = false;

    for (size_t segmentIndex = 0; segmentIndex < m_lightPathSegments.size(); ++segmentIndex) {
        const BezierSegment& segment = m_lightPathSegments[segmentIndex];
        for (int i = 0; i <= samplesPerSegment; ++i) {
            if (segmentIndex > 0 && i == 0)
                continue;
            float t = static_cast<float>(i) / static_cast<float>(samplesPerSegment);
            glm::vec3 position = evaluateBezier(segment, t);
            glm::vec3 tangent = glm::normalize(evaluateBezierTangent(segment, t));
            if (hasPrevious)
                m_lightPathTotalLength += glm::length(position - previousPosition);
            else
                hasPrevious = true;

            m_lightPathSamples.push_back({ position, tangent, m_lightPathTotalLength });
            previousPosition = position;
        }
    }

    if (!m_lightPathSamples.empty()) {
        // Ensure loop closure
        m_lightPathTotalLength += glm::length(m_lightPathSamples.front().position - m_lightPathSamples.back().position);
        m_lightPathSamples.push_back({ m_lightPathSamples.front().position, m_lightPathSamples.front().tangent, m_lightPathTotalLength });
    }

    if (m_lightPathTotalLength > 0.0f) {
        m_lightPathDistance = std::fmod(m_lightPathDistance, m_lightPathTotalLength);
        if (m_lightPathDistance < 0.0f)
            m_lightPathDistance += m_lightPathTotalLength;
    } else {
        m_lightPathDistance = 0.0f;
    }
}

void Application::uploadLightPathGeometry()
{
    if (m_lightPathSamples.size() < 2) {
        m_lightPathVertexCount = 0;
        return;
    }

    std::vector<glm::vec3> lineVertices;
    lineVertices.reserve(m_lightPathSamples.size());
    for (const PathSample& sample : m_lightPathSamples)
        lineVertices.push_back(sample.position);

    if (m_lightPathVao == 0)
        glGenVertexArrays(1, &m_lightPathVao);
    if (m_lightPathVbo == 0)
        glGenBuffers(1, &m_lightPathVbo);

    glBindVertexArray(m_lightPathVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_lightPathVbo);
    glBufferData(GL_ARRAY_BUFFER, lineVertices.size() * sizeof(glm::vec3), lineVertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void*>(0));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_lightPathVertexCount = static_cast<GLsizei>(lineVertices.size());
}

void Application::ensureLightPathFollowerValid()
{
    if (m_lights.empty()) {
        m_lightPathFollowerIndex = 0;
        return;
    }
    if (m_lightPathFollowerIndex < 0)
        m_lightPathFollowerIndex = 0;
    if (static_cast<size_t>(m_lightPathFollowerIndex) >= m_lights.size())
        m_lightPathFollowerIndex = static_cast<int>(m_lights.size() - 1);
}

void Application::updateLightPath(float deltaTime)
{
    if (!m_lightPathEnabled || m_lightPathSamples.size() < 2 || m_lightPathTotalLength <= 0.0f)
        return;
    if (m_lights.empty())
        return;

    ensureLightPathFollowerValid();
    Light& follower = m_lights[static_cast<size_t>(m_lightPathFollowerIndex)];

    m_lightPathDistance += m_lightPathSpeed * deltaTime;
    if (m_lightPathTotalLength > 0.0f) {
        m_lightPathDistance = std::fmod(m_lightPathDistance, m_lightPathTotalLength);
        if (m_lightPathDistance < 0.0f)
            m_lightPathDistance += m_lightPathTotalLength;
    }

    auto it = std::lower_bound(
        m_lightPathSamples.begin(), m_lightPathSamples.end(),
        m_lightPathDistance,
        [](const PathSample& sample, float distance) {
            return sample.cumulativeLength < distance;
        });

    if (it == m_lightPathSamples.begin())
        it = m_lightPathSamples.begin() + 1;
    if (it == m_lightPathSamples.end())
        it = m_lightPathSamples.end() - 1;

    const PathSample& nextSample = *it;
    const PathSample& prevSample = *(it - 1);
    const float segmentLength = nextSample.cumulativeLength - prevSample.cumulativeLength;
    float localT = 0.0f;
    if (segmentLength > 1e-6f)
        localT = (m_lightPathDistance - prevSample.cumulativeLength) / segmentLength;

    glm::vec3 position = glm::mix(prevSample.position, nextSample.position, localT);
    glm::vec3 tangent = glm::normalize(glm::mix(prevSample.tangent, nextSample.tangent, localT));

    follower.position = position;
    if (m_lightPathAimAtTarget) {
        glm::vec3 toTarget = m_lightPathTarget - position;
        if (glm::dot(toTarget, toTarget) > 1e-6f)
            follower.direction = glm::normalize(toTarget);
    } else {
        follower.direction = glm::dot(tangent, tangent) > 1e-6f ? glm::normalize(-tangent) : follower.direction;
    }
}

void Application::renderLightPath()
{
    if (!m_lightPathShowCurve || m_lightPathVao == 0 || m_lightPathVertexCount < 2)
        return;

    m_lightShader.bind();
    glBindVertexArray(m_lightPathVao);

    const glm::mat4 mvp = m_projectionMatrix * m_viewMatrix;
    glUniformMatrix4fv(m_lightShader.getUniformLocation("mvpMatrix"), 1, GL_FALSE, glm::value_ptr(mvp));
    const glm::vec3 pathColor { 0.95f, 0.55f, 0.15f };
    glUniform3fv(m_lightShader.getUniformLocation("markerColor"), 1, glm::value_ptr(pathColor));

    glLineWidth(2.0f);
    glDrawArrays(GL_LINE_STRIP, 0, m_lightPathVertexCount);
    glLineWidth(1.0f);

    glBindVertexArray(0);

    if (m_lightPathShowControlPoints)
        renderLightPathControlPoints();
}

void Application::renderLightPathControlPoints()
{
    if (m_lightVao == 0)
        return;

    std::vector<glm::vec3> cornerPoints;
    std::vector<glm::vec3> handlePoints;
    cornerPoints.reserve(m_lightPathSegments.size());
    handlePoints.reserve(m_lightPathSegments.size() * 2);

    for (size_t i = 0; i < m_lightPathSegments.size(); ++i) {
        const BezierSegment& seg = m_lightPathSegments[i];
        if (i == 0)
            cornerPoints.push_back(seg.p0);
        handlePoints.push_back(seg.p1);
        handlePoints.push_back(seg.p2);
        cornerPoints.push_back(seg.p3);
    }

    m_lightShader.bind();
    glBindVertexArray(m_lightVao);
    const GLint mvpLocation = m_lightShader.getUniformLocation("mvpMatrix");
    const GLint colorLocation = m_lightShader.getUniformLocation("markerColor");

    for (const glm::vec3& pt : cornerPoints) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), pt);
        model = glm::scale(model, glm::vec3(m_lightPathControlPointScale));
        const glm::mat4 mvp = m_projectionMatrix * m_viewMatrix * model;
        const glm::vec3 color { 0.2f, 0.7f, 1.0f };
        glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform3fv(colorLocation, 1, glm::value_ptr(color));
        glDrawArrays(GL_TRIANGLES, 0, m_lightVertexCount);
    }

    for (const glm::vec3& pt : handlePoints) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), pt);
        model = glm::scale(model, glm::vec3(m_lightPathControlPointScale * 0.8f));
        const glm::mat4 mvp = m_projectionMatrix * m_viewMatrix * model;
        const glm::vec3 color { 1.0f, 0.3f, 0.6f };
        glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform3fv(colorLocation, 1, glm::value_ptr(color));
        glDrawArrays(GL_TRIANGLES, 0, m_lightVertexCount);
    }

    glBindVertexArray(0);
}

void Application::renderLightMarkers()
{
    if (m_lights.empty() || m_lightVao == 0)
        return;

    m_lightShader.bind();
    glBindVertexArray(m_lightVao);

    const GLint mvpLocation = m_lightShader.getUniformLocation("mvpMatrix");
    const GLint colorLocation = m_lightShader.getUniformLocation("markerColor");

    for (size_t i = 0; i < m_lights.size(); ++i) {
        const Light& light = m_lights[i];
        glm::mat4 model = glm::translate(glm::mat4(1.0f), light.position);
        model = glm::scale(model, glm::vec3(m_lightMarkerScale));
        const glm::mat4 mvp = m_projectionMatrix * m_viewMatrix * model;

        glm::vec3 color = (i == m_selectedLightIndex) ? glm::vec3(1.0f, 0.8f, 0.0f) : glm::vec3(1.0f, 1.0f, 0.0f);

        glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform3fv(colorLocation, 1, glm::value_ptr(color));
        glDrawArrays(GL_TRIANGLES, 0, m_lightVertexCount);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Application::resetLights()
{
    m_lights.clear();
    m_lights.emplace_back();
    m_lights.back().position = glm::vec3(0.0f, 0.0f, 3.0f);
    m_lights.back().color = glm::vec3(1.0f);
    m_selectedLightIndex = 0;
    m_lightPathFollowerIndex = 0;
    if (!m_lightPathSamples.empty()) {
        m_lights.back().position = m_lightPathSamples.front().position;
        if (m_lightPathAimAtTarget) {
            glm::vec3 toTarget = m_lightPathTarget - m_lights.back().position;
            if (glm::dot(toTarget, toTarget) > 1e-6f)
                m_lights.back().direction = glm::normalize(toTarget);
        }
    }
}

void Application::selectNextLight()
{
    if (m_lights.empty())
        return;
    m_selectedLightIndex = (m_selectedLightIndex + 1) % m_lights.size();
}

void Application::selectPreviousLight()
{
    if (m_lights.empty())
        return;
    if (m_selectedLightIndex == 0)
        m_selectedLightIndex = m_lights.size() - 1;
    else
        --m_selectedLightIndex;
}

void Application::renderGui()
{
    activeTrackball();
    storeActiveViewState();

    ImGui::Begin("Shading & Lighting");
    static const char* shadingModes[] = { "Unlit", "Lambert", "Phong" };
    int shadingIndex = static_cast<int>(m_shadingModel);
    if (ImGui::Combo("Shading Model", &shadingIndex, shadingModes, IM_ARRAYSIZE(shadingModes)))
        m_shadingModel = static_cast<ShadingModel>(shadingIndex);
    ImGui::Checkbox("Use material if no texture", &m_useMaterial);
    ImGui::ColorEdit3("Custom diffuse colour", glm::value_ptr(m_customDiffuseColor));
    ImGui::ColorEdit3("Specular colour", glm::value_ptr(m_specularColor));
    ImGui::SliderFloat("Specular strength", &m_specularStrength, 0.0f, 5.0f);
    ImGui::SliderFloat("Specular shininess", &m_specularShininess, 1.0f, 256.0f);

    ImGui::Separator();
    ImGui::Text("Bezier Light Tour");
    ImGui::Checkbox("Enable light tour", &m_lightPathEnabled);
    ImGui::Checkbox("Show light path curve", &m_lightPathShowCurve);
    ImGui::Checkbox("Show control points", &m_lightPathShowControlPoints);
    ImGui::SliderFloat("Tour speed", &m_lightPathSpeed, 0.0f, 5.0f);
    ensureLightPathFollowerValid();
    if (!m_lights.empty()) {
        int followerIndex = m_lightPathFollowerIndex;
        if (ImGui::SliderInt("Tour light index", &followerIndex, 0, static_cast<int>(m_lights.size()) - 1))
            m_lightPathFollowerIndex = followerIndex;
    } else {
        ImGui::TextUnformatted("Add a light to enable the tour.");
    }
    ImGui::Checkbox("Aim tour light at target", &m_lightPathAimAtTarget);
    if (!m_lightPathAimAtTarget)
        ImGui::BeginDisabled();
    ImGui::DragFloat3("Tour target", glm::value_ptr(m_lightPathTarget), 0.05f);
    if (!m_lightPathAimAtTarget)
        ImGui::EndDisabled();

    bool pathModified = false;
    if (ImGui::TreeNode("Control Points")) {
        for (size_t i = 0; i < m_lightPathSegments.size(); ++i) {
            BezierSegment& seg = m_lightPathSegments[i];
            const std::string header = fmt::format("Segment {}", i);
            if (ImGui::TreeNode(header.c_str())) {
                if (i == 0) {
                    glm::vec3 p0 = seg.p0;
                    if (ImGui::DragFloat3("P0", glm::value_ptr(p0), 0.05f)) {
                        seg.p0 = p0;
                        const size_t prev = (i + m_lightPathSegments.size() - 1) % m_lightPathSegments.size();
                        m_lightPathSegments[prev].p3 = p0;
                        pathModified = true;
                    }
                }

                glm::vec3 p1 = seg.p1;
                if (ImGui::DragFloat3("P1", glm::value_ptr(p1), 0.05f)) {
                    seg.p1 = p1;
                    pathModified = true;
                }

                glm::vec3 p2 = seg.p2;
                if (ImGui::DragFloat3("P2", glm::value_ptr(p2), 0.05f)) {
                    seg.p2 = p2;
                    pathModified = true;
                }

                glm::vec3 p3 = seg.p3;
                if (ImGui::DragFloat3("P3", glm::value_ptr(p3), 0.05f)) {
                    seg.p3 = p3;
                    const size_t next = (i + 1) % m_lightPathSegments.size();
                    m_lightPathSegments[next].p0 = p3;
                    pathModified = true;
                }

                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }

    if (pathModified) {
        rebuildLightPathSamples();
        uploadLightPathGeometry();
        ensureLightPathFollowerValid();
        if (!m_lights.empty() && !m_lightPathSamples.empty()) {
            const bool originalEnabled = m_lightPathEnabled;
            m_lightPathEnabled = true;
            updateLightPath(0.0f);
            m_lightPathEnabled = originalEnabled;
        }
    }

    ImGui::Separator();
    ImGui::Text("Viewpoints");

    if (ImGui::Button("Add View")) {
        Trackball& currentCamera = activeTrackball();
        Viewpoint view;
        view.name = "View " + std::to_string(m_views.size());
        view.lookAt = currentCamera.lookAt();
        view.rotations = currentCamera.rotationEulerAngles();
        view.distance = currentCamera.distanceFromLookAt();
        view.defaultLookAt = view.lookAt;
        view.defaultRotations = view.rotations;
        view.defaultDistance = view.distance;
        m_views.push_back(std::move(view));
        m_activeViewIndex = m_views.size() - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        resetActiveView();
    }
    ImGui::SameLine();
    const bool canRemoveView = m_views.size() > 1;
    if (!canRemoveView)
        ImGui::BeginDisabled();
    if (ImGui::Button("Remove View")) {
        m_views.erase(m_views.begin() + static_cast<std::ptrdiff_t>(m_activeViewIndex));
        if (m_views.empty()) {
            initializeViews();
        } else {
            m_activeViewIndex = std::min(m_activeViewIndex, m_views.size() - 1);
            const Viewpoint& selected = m_views[m_activeViewIndex];
            m_trackball->setCamera(selected.lookAt, selected.rotations, selected.distance);
        }
    }
    if (!canRemoveView)
        ImGui::EndDisabled();

    if (ImGui::BeginListBox("Active View")) {
        for (size_t i = 0; i < m_views.size(); ++i) {
            const bool isSelected = (i == m_activeViewIndex);
            if (ImGui::Selectable(m_views[i].name.c_str(), isSelected)) {
                m_activeViewIndex = i;
                const Viewpoint& selected = m_views[m_activeViewIndex];
                m_trackball->setCamera(selected.lookAt, selected.rotations, selected.distance);
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    {
        Trackball& cam = activeTrackball();
        const glm::vec3 rot = cam.rotationEulerAngles();
        ImGui::Text("LookAt: (%.2f, %.2f, %.2f)", cam.lookAt().x, cam.lookAt().y, cam.lookAt().z);
        ImGui::Text("Distance: %.2f", cam.distanceFromLookAt());
        ImGui::Text("Rotation: (%.1f, %.1f)", glm::degrees(rot.x), glm::degrees(rot.y));
        ImGui::TextUnformatted("Controls: LMB orbit, RMB pan, scroll zoom.");
    }

    ImGui::Separator();
    ImGui::Text("Lights");

    if (ImGui::Button("Add Light")) {
        Light newLight;
        Trackball& cam = activeTrackball();
        newLight.position = cam.position();
        newLight.color = glm::vec3(1.0f);
        const glm::vec3 toOrigin = -newLight.position;
        const float len = glm::length(toOrigin);
        if (len > 1e-6f)
            newLight.direction = toOrigin / len;
        else
            newLight.direction = glm::vec3(0.0f, -1.0f, 0.0f);
        m_lights.push_back(newLight);
        m_selectedLightIndex = m_lights.size() - 1;
        ensureLightPathFollowerValid();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Lights")) {
        resetLights();
    }

    if (!m_lights.empty()) {
        if (m_selectedLightIndex >= m_lights.size())
            m_selectedLightIndex = m_lights.size() - 1;
        ensureLightPathFollowerValid();

        if (ImGui::BeginListBox("Light List")) {
            for (size_t i = 0; i < m_lights.size(); ++i) {
                const bool isSelected = (i == m_selectedLightIndex);
                const std::string label = "Light " + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), isSelected))
                    m_selectedLightIndex = i;
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }

        if (ImGui::Button("Previous Light")) {
            selectPreviousLight();
        }
        ImGui::SameLine();
        if (ImGui::Button("Next Light")) {
            selectNextLight();
        }

        bool canRemove = m_lights.size() > 1;
        if (!canRemove)
            ImGui::BeginDisabled();
        if (ImGui::Button("Remove Selected Light")) {
            m_lights.erase(m_lights.begin() + static_cast<std::ptrdiff_t>(m_selectedLightIndex));
            if (m_lights.empty())
                m_selectedLightIndex = 0;
            else if (m_selectedLightIndex >= m_lights.size())
                m_selectedLightIndex = m_lights.size() - 1;
            ensureLightPathFollowerValid();
        }
        if (!canRemove)
            ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Text("Selected Light Properties");
        Light& selectedLight = m_lights[m_selectedLightIndex];
        ImGui::DragFloat3("Position", glm::value_ptr(selectedLight.position), 0.05f);
        ImGui::ColorEdit3("Color", glm::value_ptr(selectedLight.color));
        ImGui::Checkbox("Peels Depth", &selectedLight.peelsDepth);

        ImGui::Separator();
        ImGui::Text("Spotlight Settings");
        ImGui::Checkbox("Is Spotlight", &selectedLight.isSpotlight);
        ImGui::DragFloat3("Direction", glm::value_ptr(selectedLight.direction), 0.01f, -1.0f, 1.0f, "%.3f");
        if (ImGui::Button("Normalise Direction")) {
            const float len = glm::length(selectedLight.direction);
            if (len > 1e-4f)
                selectedLight.direction /= len;
        }
        ImGui::SliderFloat("Spot Cos Cutoff", &selectedLight.spotCosCutoff, 0.0f, 1.0f);
        ImGui::SliderFloat("Spot Softness", &selectedLight.spotSoftness, 0.0f, 1.0f);
    }

    ImGui::End();
}

void Application::uploadLightsToShader()
{
    constexpr int MAX_LIGHTS = 8;
    using Vec3Array = std::array<glm::vec3, MAX_LIGHTS>;
    using FloatArray = std::array<float, MAX_LIGHTS>;
    using IntArray = std::array<int, MAX_LIGHTS>;

    Vec3Array positions {};
    Vec3Array colors {};
    Vec3Array directions {};
    FloatArray cosCutoff {};
    FloatArray softness {};
    IntArray spotFlags {};

    const int count = static_cast<int>(std::min(m_lights.size(), static_cast<size_t>(MAX_LIGHTS)));

    for (int i = 0; i < count; ++i) {
        const Light& light = m_lights[i];
        positions[i] = light.position;
        colors[i] = light.color;
        glm::vec3 dir = light.direction;
        const float len = glm::length(dir);
        if (len > 1e-4f)
            dir /= len;
        else
            dir = glm::vec3(0.0f, -1.0f, 0.0f);
        directions[i] = dir;
        cosCutoff[i] = glm::clamp(light.spotCosCutoff, 0.0f, 1.0f);
        softness[i] = glm::clamp(light.spotSoftness, 0.0f, 1.0f);
        spotFlags[i] = light.isSpotlight ? 1 : 0;
    }

    glUniform1i(m_defaultShader.getUniformLocation("numLights"), count);

    if (count == 0)
        return;

    glUniform3fv(m_defaultShader.getUniformLocation("lightPositions"), count, glm::value_ptr(positions[0]));
    glUniform3fv(m_defaultShader.getUniformLocation("lightColors"), count, glm::value_ptr(colors[0]));
    glUniform1iv(m_defaultShader.getUniformLocation("lightIsSpotlight"), count, spotFlags.data());
    glUniform3fv(m_defaultShader.getUniformLocation("lightDirections"), count, glm::value_ptr(directions[0]));
    glUniform1fv(m_defaultShader.getUniformLocation("lightSpotCosCutoff"), count, cosCutoff.data());
    glUniform1fv(m_defaultShader.getUniformLocation("lightSpotSoftness"), count, softness.data());
}

int main()
{
    Application app;
    app.update();

    return 0;
}
