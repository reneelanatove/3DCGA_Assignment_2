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
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
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
#include <optional>
#include <vector>
#include <random>

namespace {

struct WindmillParameters {
    glm::vec3 baseSize { 2.5f, 1.0f, 2.5f };
    glm::vec3 towerSize { 0.7f, 3.2f, 0.7f };
    glm::vec3 hubSize { 0.55f, 0.55f, 0.55f };
    float hubForwardOffset { 0.4f };
    float hubVerticalOffset { 0.0f };
    float armLength { 2.7f };
    float armWidth { 0.35f };
    float armThickness { 0.12f };
    glm::vec3 structureColor { 0.88f, 0.88f, 0.86f };
    float rotationSpeedDegPerSec { 45.0f };
};

struct WindmillMeshes {
    Mesh body;
    Mesh rotor;
};

Mesh createBoxMesh(const glm::vec3& size)
{
    Mesh mesh;
    mesh.vertices.reserve(24);
    mesh.triangles.reserve(12);

    const glm::vec3 half = size * 0.5f;
    const std::array<glm::vec3, 8> corners = {
        glm::vec3(-half.x, -half.y, -half.z),
        glm::vec3(half.x, -half.y, -half.z),
        glm::vec3(half.x, half.y, -half.z),
        glm::vec3(-half.x, half.y, -half.z),
        glm::vec3(-half.x, -half.y, half.z),
        glm::vec3(half.x, -half.y, half.z),
        glm::vec3(half.x, half.y, half.z),
        glm::vec3(-half.x, half.y, half.z)
    };

    const std::array<glm::vec3, 6> normals = {
        glm::vec3(0.0f, 0.0f, 1.0f),   // Front
        glm::vec3(0.0f, 0.0f, -1.0f),  // Back
        glm::vec3(-1.0f, 0.0f, 0.0f),  // Left
        glm::vec3(1.0f, 0.0f, 0.0f),   // Right
        glm::vec3(0.0f, 1.0f, 0.0f),   // Top
        glm::vec3(0.0f, -1.0f, 0.0f)   // Bottom
    };

    const std::array<std::array<unsigned, 4>, 6> faceIndices = {{
        { 4, 5, 6, 7 }, // Front
        { 1, 0, 3, 2 }, // Back
        { 0, 4, 7, 3 }, // Left
        { 5, 1, 2, 6 }, // Right
        { 3, 7, 6, 2 }, // Top
        { 0, 1, 5, 4 }  // Bottom
    }};

    for (std::size_t face = 0; face < faceIndices.size(); ++face) {
        const glm::vec3 normal = normals[face];
        const auto& indices = faceIndices[face];
        const std::size_t baseIndex = mesh.vertices.size();
        for (std::size_t i = 0; i < 4; ++i) {
            Vertex vertex;
            vertex.position = corners[indices[i]];
            vertex.normal = normal;
            vertex.texCoord = glm::vec2(0.0f);
            mesh.vertices.push_back(vertex);
        }
        mesh.triangles.emplace_back(glm::uvec3(baseIndex + 0, baseIndex + 1, baseIndex + 2));
        mesh.triangles.emplace_back(glm::uvec3(baseIndex + 0, baseIndex + 2, baseIndex + 3));
    }

    mesh.material.kd = glm::vec3(0.8f);
    mesh.material.ks = glm::vec3(0.2f);
    mesh.material.shininess = 32.0f;

    return mesh;
}

void appendTransformedMesh(Mesh& destination, const Mesh& source, const glm::mat4& transform)
{
    const std::size_t vertexOffset = destination.vertices.size();
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    const unsigned int indexOffset = static_cast<unsigned int>(vertexOffset);

    destination.vertices.reserve(destination.vertices.size() + source.vertices.size());
    destination.triangles.reserve(destination.triangles.size() + source.triangles.size());

    for (const Vertex& vertex : source.vertices) {
        Vertex transformedVertex = vertex;
        const glm::vec4 position = transform * glm::vec4(vertex.position, 1.0f);
        transformedVertex.position = glm::vec3(position);
        const glm::vec3 transformedNormal = normalMatrix * vertex.normal;
        const float normalLengthSq = glm::dot(transformedNormal, transformedNormal);
        if (normalLengthSq > 1e-10f)
            transformedVertex.normal = transformedNormal / std::sqrt(normalLengthSq);
        else
            transformedVertex.normal = vertex.normal;
        destination.vertices.push_back(transformedVertex);
    }

    for (const glm::uvec3& triangle : source.triangles) {
        destination.triangles.emplace_back(glm::uvec3(
            indexOffset + triangle.x,
            indexOffset + triangle.y,
            indexOffset + triangle.z));
    }
}

glm::vec3 computeHubPosition(const WindmillParameters& params)
{
    const float towerBaseY = params.baseSize.y;
    return glm::vec3(0.0f, towerBaseY + params.towerSize.y + params.hubVerticalOffset, params.hubForwardOffset);
}

WindmillMeshes buildWindmillMeshes(const WindmillParameters& params)
{
    WindmillMeshes result;
    result.body.vertices.reserve(24 * 4);
    result.body.triangles.reserve(12 * 4);
    result.rotor.vertices.reserve(24 * 5);
    result.rotor.triangles.reserve(12 * 5);

    const glm::mat4 identity(1.0f);

    const Mesh baseMesh = createBoxMesh(params.baseSize);
    const glm::mat4 baseTransform = glm::translate(identity, glm::vec3(0.0f, params.baseSize.y * 0.5f, 0.0f));
    appendTransformedMesh(result.body, baseMesh, baseTransform);

    const Mesh towerMesh = createBoxMesh(params.towerSize);
    const float towerBaseY = params.baseSize.y;
    const glm::mat4 towerTransform = glm::translate(identity, glm::vec3(0.0f, towerBaseY + params.towerSize.y * 0.5f, 0.0f));
    appendTransformedMesh(result.body, towerMesh, towerTransform);

    const Mesh hubMesh = createBoxMesh(params.hubSize);
    appendTransformedMesh(result.rotor, hubMesh, identity);

    const Mesh armMesh = createBoxMesh(glm::vec3(params.armLength, params.armWidth, params.armThickness));
    for (int i = 0; i < 4; ++i) {
        const float angle = glm::radians(90.0f * static_cast<float>(i));
        const glm::mat4 armTransform = glm::rotate(identity, angle, glm::vec3(0.0f, 0.0f, 1.0f))
            * glm::translate(identity, glm::vec3(params.armLength * 0.5f, 0.0f, 0.0f));
        appendTransformedMesh(result.rotor, armMesh, armTransform);
    }

    result.body.material.kd = params.structureColor;
    result.body.material.ks = glm::vec3(0.2f);
    result.body.material.shininess = 32.0f;

    result.rotor.material.kd = params.structureColor;
    result.rotor.material.ks = glm::vec3(0.2f);
    result.rotor.material.shininess = 32.0f;

    return result;
}

} // namespace

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
        m_meshes = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/lucy_scene.obj");
        try {
            ShaderBuilder defaultBuilder;
            defaultBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/shader_vert.glsl");
            defaultBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shader_frag.glsl");
            m_defaultShader = defaultBuilder.build();

            ShaderBuilder shadowBuilder;
            shadowBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/shadow_vert.glsl");
            shadowBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shadow_frag.glsl");
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

        initializeShadowTexture();
        initializeViews();
        initializeLightGeometry();
        initializeWindArrowGeometry();
        initializeWindParticleGeometry();
        updateWindOrientation();
        updateWindGusts(0.0f);
        uploadWindParticles();
        resetLights();
        initializeLightPath();
        sanitizeWindmillParams();
        rebuildWindmillMesh();
        updateDayNightCycle(0.0f);
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
        if (m_windArrowVbo != 0)
            glDeleteBuffers(1, &m_windArrowVbo);
        if (m_windArrowVao != 0)
            glDeleteVertexArrays(1, &m_windArrowVao);
        if (m_windParticleVbo != 0)
            glDeleteBuffers(1, &m_windParticleVbo);
        if (m_windParticleVao != 0)
            glDeleteVertexArrays(1, &m_windParticleVao);
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

            updateDayNightCycle(deltaTime);
            updateWindGusts(deltaTime);
            Trackball& camera = activeTrackball();
            updateCameraPath(deltaTime, camera);
            updateLightPath(deltaTime);
            updateWindParticles(deltaTime);
            const float rotationSpeedRad = glm::radians(m_windmillParams.rotationSpeedDegPerSec * m_windStrength);
            if (rotationSpeedRad != 0.0f) {
                m_windmillRotationAngle += rotationSpeedRad * deltaTime;
                const float fullTurn = glm::two_pi<float>();
                if (fullTurn > 0.0f) {
                    m_windmillRotationAngle = std::fmod(m_windmillRotationAngle, fullTurn);
                    if (m_windmillRotationAngle < 0.0f)
                        m_windmillRotationAngle += fullTurn;
                }
            }

            renderGui();

            // === Shadow Pass ===
            glEnable(GL_DEPTH_TEST);

            glm::mat4 lightProj;
            if (m_lights[0].isSpotlight) lightProj = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 100.0f);
            else lightProj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f);

            glm::mat4 lightView = glm::lookAt(m_lights[0].position, m_lights[0].direction, glm::vec3(0.0f, 1.0f, 0.0f));

            glm::mat4 lightMVP = lightProj * lightView;

            // Bind the off-screen framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);

            // Clear the shadow map and set needed options
            glClearDepth(1.0);
            glClear(GL_DEPTH_BUFFER_BIT);

            for (GPUMesh& mesh : m_meshes) {
                // Bind the shader
                m_shadowShader.bind();
                // Set viewport size
                const int SHADOWTEX_WIDTH = 1024;
                const int SHADOWTEX_HEIGHT = 1024;
                glViewport(0, 0, SHADOWTEX_WIDTH, SHADOWTEX_HEIGHT);

                // .... HERE YOU MUST ADD THE CORRECT UNIFORMS FOR RENDERING THE SHADOW MAP
                glUniformMatrix4fv(m_shadowShader.getUniformLocation("mvp"), 1, GL_FALSE, glm::value_ptr(lightMVP));

                // Execute draw command
                mesh.draw(m_shadowShader);
            }


            // Unbind the off-screen framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            if (m_windmillDirty)
                rebuildWindmillMesh();
			// === Main Render Pass ===
			glViewport(0, 0, m_window.getFrameBufferSize().x, m_window.getFrameBufferSize().y);

            // Clear the screen
            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // ...
            glEnable(GL_DEPTH_TEST);

            if (!m_views.empty()) {
                Viewpoint& viewState = m_views[m_activeViewIndex];
                viewState.lookAt = camera.lookAt();
                viewState.rotations = camera.rotationEulerAngles();
                viewState.distance = camera.distanceFromLookAt();
            }
            m_viewMatrix = camera.viewMatrix();
            m_projectionMatrix = camera.projectionMatrix();

            auto drawMeshWithModel = [&](GPUMesh& mesh, const glm::mat4& modelMatrix) {
                const glm::mat4 localMvp = m_projectionMatrix * m_viewMatrix * modelMatrix;
                // Normals need the inverse transpose to handle non-uniform scaling correctly.
                const glm::mat3 localNormal = glm::inverseTranspose(glm::mat3(modelMatrix));



                mesh.setMaterial(m_gpuMaterial);
                m_defaultShader.bind();
                glUniformMatrix4fv(m_defaultShader.getUniformLocation("mvpMatrix"), 1, GL_FALSE, glm::value_ptr(localMvp));
                glUniformMatrix4fv(m_defaultShader.getUniformLocation("modelMatrix"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
                glUniformMatrix3fv(m_defaultShader.getUniformLocation("normalModelMatrix"), 1, GL_FALSE, glm::value_ptr(localNormal));
                if (mesh.hasTextureCoords()) {
                    m_texture.bind(GL_TEXTURE0);
                    glUniform1i(m_defaultShader.getUniformLocation("colorMap"), 0);
                    glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_TRUE);
                    glUniform1i(m_defaultShader.getUniformLocation("useMaterial"), m_useMaterial);
                } else {
                    glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_FALSE);
                    glUniform1i(m_defaultShader.getUniformLocation("useMaterial"), m_useMaterial);
                }
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, m_texShadow);
                glUniform1i(m_defaultShader.getUniformLocation("shadowMap"), 1);
				glUniform1i(m_defaultShader.getUniformLocation("shadowsEnabled"), m_shadows);
				glUniform1i(m_defaultShader.getUniformLocation("pcf"), m_pcf);
				glUniformMatrix4fv(m_defaultShader.getUniformLocation("lightMVP"), 1, GL_FALSE, glm::value_ptr(lightMVP));
                glUniform1i(m_defaultShader.getUniformLocation("shadingMode"), static_cast<int>(m_shadingModel));
                glUniform3fv(m_defaultShader.getUniformLocation("customDiffuseColor"), 1, glm::value_ptr(m_customDiffuseColor));
                glUniform3fv(m_defaultShader.getUniformLocation("viewPosition"), 1, glm::value_ptr(camera.position()));
                glUniform3fv(m_defaultShader.getUniformLocation("specularColor"), 1, glm::value_ptr(m_specularColor));
                glUniform1f(m_defaultShader.getUniformLocation("specularStrength"), m_specularStrength);
                glUniform1f(m_defaultShader.getUniformLocation("specularShininess"), m_specularShininess);

                uploadLightsToShader();
                mesh.draw(m_defaultShader);
                glUniform3fv(m_defaultShader.getUniformLocation("ambientLight"), 1, glm::value_ptr(m_currentAmbientColor));
                glUniform3fv(m_defaultShader.getUniformLocation("sunDirection"), 1, glm::value_ptr(m_currentSunDirection));
                glUniform3fv(m_defaultShader.getUniformLocation("sunColor"), 1, glm::value_ptr(m_currentSunColor));
                glUniform1f(m_defaultShader.getUniformLocation("sunIntensity"), m_currentSunIntensity);
            };

            for (GPUMesh& mesh : m_meshes)
                drawMeshWithModel(mesh, m_modelMatrix);

            if (m_windmillBodyMesh)
                drawMeshWithModel(*m_windmillBodyMesh, glm::mat4(1.0f));

            if (m_windmillRotorMesh) {
                glm::mat4 rotorModel = glm::translate(glm::mat4(1.0f), m_windmillHubPosition);
                rotorModel = rotorModel * glm::rotate(glm::mat4(1.0f), m_windmillRotationAngle, glm::vec3(0.0f, 0.0f, 1.0f));
                drawMeshWithModel(*m_windmillRotorMesh, m_windmillOrientation * rotorModel);
            }

            // Processes input and swaps the window buffer
            renderLightPath();
            renderWindParticles();
            renderWindArrow();
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

    struct Particle {
        glm::vec3 position { 0.0f };
        glm::vec3 velocity { 0.0f };
        float lifetime { 1.0f };
        float age { 0.0f };
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
	bool m_useNormalMap{ false };
    bool m_shadows{ false };
    bool m_pcf{ false };
    ShadingModel m_shadingModel { ShadingModel::Lambert };
    WindmillParameters m_windmillParams;
    std::optional<GPUMesh> m_windmillBodyMesh;
    std::optional<GPUMesh> m_windmillRotorMesh;
    bool m_windmillDirty { true };
    glm::vec3 m_windmillHubPosition { 0.0f };
    float m_windmillRotationAngle { 0.0f };
    glm::vec3 m_customDiffuseColor { 0.8f, 0.4f, 0.2f };
    glm::vec3 m_specularColor { 1.0f, 1.0f, 1.0f };
    float m_specularStrength { 1.0f };
    float m_specularShininess { 0.0f };
    bool m_dayNightEnabled { true };
    bool m_dayNightAutoAdvance { true };
    float m_dayNightCycleDuration { 60.0f };
    float m_timeOfDay01 { 0.25f };
    glm::vec3 m_dayAmbientColor { 0.35f, 0.33f, 0.38f };
    glm::vec3 m_nightAmbientColor { 0.03f, 0.05f, 0.12f };
    glm::vec3 m_daySunColor { 1.0f, 0.95f, 0.85f };
    glm::vec3 m_nightSunColor { 0.2f, 0.25f, 0.4f };
    glm::vec3 m_currentAmbientColor { 0.3f, 0.3f, 0.3f };
    glm::vec3 m_currentSunDirection { 0.0f, -1.0f, 0.2f };
    glm::vec3 m_currentSunColor { 0.9f, 0.85f, 0.8f };
    float m_currentSunIntensity { 1.0f };
    float m_windDirectionAngleDeg { 0.0f };
    glm::vec3 m_windDirection { 0.0f, 0.0f, 1.0f };
    bool m_windGustsEnabled { true };
    float m_windStrength { 1.0f };
    float m_windBaseStrength { 0.7f };
    float m_windGustAmplitude { 0.6f };
    float m_windGustFrequency { 0.18f };
    float m_windSecondaryFrequency { 0.05f };
    float m_windTime { 0.0f };
    glm::mat4 m_windmillOrientation { 1.0f };
    GLuint m_windArrowVao { 0 };
    GLuint m_windArrowVbo { 0 };
    GLsizei m_windArrowVertexCount { 0 };
    float m_windArrowScale { 1.8f };
    std::vector<Particle> m_windParticles;
    GLuint m_windParticleVao { 0 };
    GLuint m_windParticleVbo { 0 };
    GLsizei m_windParticleCount { 0 };
    float m_windParticleSpawnRate { 45.0f };
    float m_windParticleLifetime { 3.5f };
    float m_windParticleSize { 8.0f };
    float m_particleSpawnAccumulator { 0.0f };
    size_t m_maxWindParticles { 400 };
    std::mt19937 m_rng { 1337u };
    std::uniform_real_distribution<float> m_uniform01 { 0.0f, 1.0f };
    std::vector<Light> m_lights;
    size_t m_selectedLightIndex { 0 };

    Material m_gpuMaterial {
        glm::vec3(0.8f, 0.8f, 0.8f),    // kd
        glm::vec3(0.5f, 0.5f, 0.5f),    // ks
        32.0f,                          // shininess
        1.0f,                           // transparency
        nullptr                         // kdTexture
    };

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
	GLuint m_texShadow { 0 };
    GLuint m_texNormal { 0 };
    GLuint m_framebuffer{ 0 };
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
    bool m_cameraPathEnabled { false };
    bool m_cameraPathAimAtTarget { true };
    float m_cameraPathSpeed { 0.6f };
    float m_cameraPathDistance { 0.0f };
    float m_cameraPathLookAhead { 2.0f };
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
    void initializeShadowTexture();
    void initializeNormalTexture();
    void resetActiveView();
    void storeActiveViewState();
    void initializeLightGeometry();
    void renderLightMarkers();
    void initializeLightPath();
    void rebuildLightPathSamples();
    void uploadLightPathGeometry();
    float wrapPathDistance(float distance) const;
    bool samplePathAtDistance(float distance, glm::vec3& position, glm::vec3& tangent) const;
    glm::vec3 evaluateBezier(const BezierSegment& segment, float t) const;
    glm::vec3 evaluateBezierTangent(const BezierSegment& segment, float t) const;
    void updateLightPath(float deltaTime);
    void updateCameraPath(float deltaTime, Trackball& camera);
    void renderLightPath();
    void renderLightPathControlPoints();
    void ensureLightPathFollowerValid();
    void resetLights();
    void selectNextLight();
    void selectPreviousLight();
    void renderGui();
    void uploadLightsToShader();
    void rebuildWindmillMesh();
    void sanitizeWindmillParams();
    void updateDayNightCycle(float deltaTime);
    void updateWindOrientation();
    void initializeWindArrowGeometry();
    void renderWindArrow();
    void initializeWindParticleGeometry();
    void updateWindGusts(float deltaTime);
    void updateWindParticles(float deltaTime);
    void uploadWindParticles();
    void renderWindParticles();
    Particle spawnWindParticle();
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

void Application::initializeNormalTexture() {
    glGenTextures(1, &m_texNormal);

    const int NORMALTEX_WIDTH = 1024;
    const int NORMALTEX_HEIGHT = 1024;
    

}

void Application::initializeShadowTexture() {

    // === Create Shadow Texture ===
    glGenTextures(1, &m_texShadow);

    const int SHADOWTEX_WIDTH = 1024;
    const int SHADOWTEX_HEIGHT = 1024;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texShadow);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SHADOWTEX_WIDTH, SHADOWTEX_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

    // Set behaviour for when texture coordinates are outside the [0, 1] range.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Set interpolation for texture sampling (GL_NEAREST for no interpolation).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindTexture(GL_TEXTURE_2D, 0);

    // === Create framebuffer for extra texture ===
    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_texShadow, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

void Application::initializeWindArrowGeometry()
{
    if (m_windArrowVbo != 0)
        glDeleteBuffers(1, &m_windArrowVbo);
    if (m_windArrowVao != 0)
        glDeleteVertexArrays(1, &m_windArrowVao);

    const std::array<glm::vec3, 6> arrowVertices = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.12f, 0.0f, 0.85f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(-0.12f, 0.0f, 0.85f)
    };

    glGenVertexArrays(1, &m_windArrowVao);
    glGenBuffers(1, &m_windArrowVbo);

    glBindVertexArray(m_windArrowVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_windArrowVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(arrowVertices.size() * sizeof(glm::vec3)), arrowVertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void*>(0));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_windArrowVertexCount = static_cast<GLsizei>(arrowVertices.size());
}

void Application::initializeWindParticleGeometry()
{
    if (m_windParticleVbo != 0)
        glDeleteBuffers(1, &m_windParticleVbo);
    if (m_windParticleVao != 0)
        glDeleteVertexArrays(1, &m_windParticleVao);

    glGenVertexArrays(1, &m_windParticleVao);
    glGenBuffers(1, &m_windParticleVbo);

    glBindVertexArray(m_windParticleVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_windParticleVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_maxWindParticles * sizeof(glm::vec3)), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void*>(0));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_windParticleCount = 0;
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
    m_cameraPathDistance = wrapPathDistance(m_cameraPathDistance);
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

float Application::wrapPathDistance(float distance) const
{
    if (m_lightPathTotalLength <= 0.0f)
        return 0.0f;
    distance = std::fmod(distance, m_lightPathTotalLength);
    if (distance < 0.0f)
        distance += m_lightPathTotalLength;
    return distance;
}

bool Application::samplePathAtDistance(float distance, glm::vec3& position, glm::vec3& tangent) const
{
    if (m_lightPathSamples.size() < 2)
        return false;

    auto it = std::lower_bound(
        m_lightPathSamples.begin(), m_lightPathSamples.end(),
        distance,
        [](const PathSample& sample, float query) {
            return sample.cumulativeLength < query;
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
        localT = (distance - prevSample.cumulativeLength) / segmentLength;

    position = glm::mix(prevSample.position, nextSample.position, localT);
    glm::vec3 mixedTangent = glm::mix(prevSample.tangent, nextSample.tangent, localT);
    if (glm::dot(mixedTangent, mixedTangent) > 1e-6f)
        tangent = glm::normalize(mixedTangent);
    else
        tangent = prevSample.tangent;

    return true;
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
    m_lightPathDistance = wrapPathDistance(m_lightPathDistance);

    glm::vec3 position { 0.0f };
    glm::vec3 tangent { 0.0f };
    if (!samplePathAtDistance(m_lightPathDistance, position, tangent))
        return;

    follower.position = position;
    if (m_lightPathAimAtTarget) {
        glm::vec3 toTarget = m_lightPathTarget - position;
        if (glm::dot(toTarget, toTarget) > 1e-6f)
            follower.direction = glm::normalize(toTarget);
    } else {
        follower.direction = glm::dot(tangent, tangent) > 1e-6f ? glm::normalize(-tangent) : follower.direction;
    }
}

void Application::updateCameraPath(float deltaTime, Trackball& camera)
{
    if (!m_cameraPathEnabled || m_lightPathSamples.size() < 2 || m_lightPathTotalLength <= 0.0f)
        return;

    m_cameraPathDistance += m_cameraPathSpeed * deltaTime;
    m_cameraPathDistance = wrapPathDistance(m_cameraPathDistance);

    glm::vec3 position { 0.0f };
    glm::vec3 tangent { 0.0f };
    if (!samplePathAtDistance(m_cameraPathDistance, position, tangent))
        return;

    if (glm::dot(tangent, tangent) < 1e-6f)
        tangent = glm::vec3(0.0f, 0.0f, 1.0f);

    glm::vec3 desiredTarget;
    if (m_cameraPathAimAtTarget) {
        desiredTarget = m_lightPathTarget;
    } else {
        desiredTarget = position + glm::normalize(tangent) * m_cameraPathLookAhead;
    }

    glm::vec3 toTarget = desiredTarget - position;
    if (glm::dot(toTarget, toTarget) < 1e-6f) {
        toTarget = glm::vec3(0.0f, 0.0f, 1.0f);
        desiredTarget = position + toTarget;
    }

    const float distance = glm::length(toTarget);
    glm::vec3 forward = toTarget / distance;

    glm::vec3 worldUp { 0.0f, 1.0f, 0.0f };
    if (std::abs(glm::dot(forward, worldUp)) > 0.95f)
        worldUp = glm::vec3(1.0f, 0.0f, 0.0f);

    glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
    glm::vec3 up = glm::normalize(glm::cross(forward, right));

    glm::mat3 rotationMatrix(1.0f);
    rotationMatrix[0] = right;
    rotationMatrix[1] = up;
    rotationMatrix[2] = forward;

    glm::quat orientation = glm::quat_cast(rotationMatrix);
    glm::vec3 eulerAngles = glm::eulerAngles(orientation);

    camera.setCamera(desiredTarget, eulerAngles, distance);
}

void Application::updateDayNightCycle(float deltaTime)
{
    if (!m_dayNightEnabled) {
        m_currentAmbientColor = glm::vec3(0.0f);
        m_currentSunDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        m_currentSunColor = glm::vec3(0.0f);
        m_currentSunIntensity = 0.0f;
        return;
    }

    if (m_dayNightAutoAdvance && m_dayNightCycleDuration > 0.0f) {
        float advance = deltaTime / m_dayNightCycleDuration;
        m_timeOfDay01 = std::fmod(m_timeOfDay01 + advance, 1.0f);
        if (m_timeOfDay01 < 0.0f)
            m_timeOfDay01 += 1.0f;
    } else {
        m_timeOfDay01 = std::clamp(m_timeOfDay01, 0.0f, 1.0f);
    }

    const float sunAngle = glm::two_pi<float>() * m_timeOfDay01 - glm::half_pi<float>();
    glm::vec3 sunDirection = glm::normalize(glm::vec3(std::cos(sunAngle), -std::sin(sunAngle), 0.25f));
    float daylightFactor = glm::clamp(-sunDirection.y, 0.0f, 1.0f);
    float easedDaylight = glm::smoothstep(0.0f, 1.0f, daylightFactor);

    m_currentSunDirection = sunDirection;
    m_currentSunIntensity = easedDaylight;
    m_currentAmbientColor = glm::mix(m_nightAmbientColor, m_dayAmbientColor, easedDaylight);
    m_currentSunColor = glm::mix(m_nightSunColor, m_daySunColor, easedDaylight) * easedDaylight;
}

void Application::updateWindOrientation()
{
    float yawRad = glm::radians(m_windDirectionAngleDeg);
    glm::vec3 direction = glm::vec3(std::sin(yawRad), 0.0f, std::cos(yawRad));
    if (glm::dot(direction, direction) < 1e-6f)
        direction = glm::vec3(0.0f, 0.0f, 1.0f);
    m_windDirection = glm::normalize(direction);

    glm::vec3 facing = -m_windDirection;
    if (glm::dot(facing, facing) < 1e-6f)
        facing = glm::vec3(0.0f, 0.0f, 1.0f);
    constexpr float modelForwardYawOffsetDeg = -90.0f;
    float yaw = std::atan2(facing.x, facing.z) + glm::radians(modelForwardYawOffsetDeg);    
    m_windmillOrientation = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
}

void Application::updateWindGusts(float deltaTime)
{
    if (!m_windGustsEnabled) {
        m_windStrength = std::max(m_windBaseStrength, 0.0f);
        return;
    }

    m_windTime += deltaTime;

    const float primaryPhase = glm::two_pi<float>() * m_windGustFrequency * m_windTime;
    const float secondaryPhase = glm::two_pi<float>() * m_windSecondaryFrequency * (m_windTime + 2.37f);
    const float blend = 0.5f * std::sin(primaryPhase) + 0.5f * std::sin(secondaryPhase);
    const float gustNormalized = 0.5f * (blend + 1.0f); // 0..1
    const float targetStrength = std::max(0.0f, m_windBaseStrength + m_windGustAmplitude * gustNormalized);

    if (deltaTime > 0.0f) {
        const float smoothing = std::clamp(deltaTime * 2.0f, 0.0f, 1.0f);
        m_windStrength = glm::mix(m_windStrength, targetStrength, smoothing);
    } else {
        m_windStrength = targetStrength;
    }

    m_windStrength = std::clamp(m_windStrength, 0.05f, 4.0f);
}

Application::Particle Application::spawnWindParticle()
{
    Particle particle;
    particle.age = 0.0f;
    particle.lifetime = m_windParticleLifetime * (0.6f + 0.8f * m_uniform01(m_rng));

    const float radius = 0.9f;
    const float angle = glm::two_pi<float>() * m_uniform01(m_rng);
    const float distance = std::sqrt(m_uniform01(m_rng)) * radius;
    const glm::vec3 offset(distance * std::cos(angle), 0.0f, distance * std::sin(angle));
    const float groundHeight = m_windmillParams.baseSize.y;
    particle.position = glm::vec3(0.0f, groundHeight + 0.15f, 0.0f) + offset;

    glm::vec3 lateral = glm::cross(m_windDirection, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::dot(lateral, lateral) < 1e-6f)
        lateral = glm::vec3(1.0f, 0.0f, 0.0f);
    lateral = glm::normalize(lateral);

    const float lateralScale = (m_uniform01(m_rng) - 0.5f) * 0.8f;
    const glm::vec3 gustVelocity = m_windDirection * (1.2f + 1.6f * m_windStrength);
    const glm::vec3 upward { 0.0f, 0.35f + 0.35f * m_uniform01(m_rng), 0.0f };
    particle.velocity = gustVelocity + lateral * lateralScale + upward;

    return particle;
}

void Application::updateWindParticles(float deltaTime)
{
    if (deltaTime <= 0.0f)
        return;

    const float spawnRate = std::max(0.0f, m_windParticleSpawnRate * m_windStrength);
    m_particleSpawnAccumulator += spawnRate * deltaTime;

    const size_t spawnCount = static_cast<size_t>(m_particleSpawnAccumulator);
    m_particleSpawnAccumulator -= static_cast<float>(spawnCount);

    for (size_t i = 0; i < spawnCount && m_windParticles.size() < m_maxWindParticles; ++i)
        m_windParticles.push_back(spawnWindParticle());

    const glm::vec3 upwardAcceleration { 0.0f, 0.25f, 0.0f };
    const float alignmentRate = std::clamp(deltaTime * 0.5f, 0.0f, 1.0f);
    const glm::vec3 targetVelocity = m_windDirection * (1.5f * m_windStrength);

    for (Particle& particle : m_windParticles) {
        particle.age += deltaTime;
        particle.velocity += upwardAcceleration * deltaTime;
        particle.velocity = glm::mix(particle.velocity, targetVelocity, alignmentRate);
        particle.position += particle.velocity * deltaTime;
    }

    m_windParticles.erase(
        std::remove_if(
            m_windParticles.begin(),
            m_windParticles.end(),
            [](const Particle& particle) {
                return particle.age >= particle.lifetime;
            }),
        m_windParticles.end());

    uploadWindParticles();
}

void Application::uploadWindParticles()
{
    if (m_windParticleVbo == 0) {
        m_windParticleCount = 0;
        return;
    }

    if (m_windParticles.empty()) {
        m_windParticleCount = 0;
        return;
    }

    std::vector<glm::vec3> positions;
    positions.reserve(m_windParticles.size());
    for (const Particle& particle : m_windParticles)
        positions.push_back(particle.position);

    glBindBuffer(GL_ARRAY_BUFFER, m_windParticleVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(positions.size() * sizeof(glm::vec3)),
        positions.data(),
        GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_windParticleCount = static_cast<GLsizei>(positions.size());
}

void Application::renderWindParticles()
{
    if (m_windParticleVao == 0 || m_windParticleCount == 0)
        return;

    m_lightShader.bind();
    glBindVertexArray(m_windParticleVao);

    const glm::mat4 mvp = m_projectionMatrix * m_viewMatrix;
    glUniformMatrix4fv(m_lightShader.getUniformLocation("mvpMatrix"), 1, GL_FALSE, glm::value_ptr(mvp));
    const glm::vec3 particleColor { 0.8f, 0.85f, 0.9f };
    glUniform3fv(m_lightShader.getUniformLocation("markerColor"), 1, glm::value_ptr(particleColor));

    const float clampedSize = std::max(1.0f, m_windParticleSize);
    glPointSize(clampedSize);
    glDrawArrays(GL_POINTS, 0, m_windParticleCount);
    glPointSize(1.0f);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Application::renderWindArrow()
{
    if (m_windArrowVao == 0 || m_windArrowVertexCount == 0)
        return;

    m_lightShader.bind();
    glBindVertexArray(m_windArrowVao);

    const float windYaw = std::atan2(m_windDirection.x, m_windDirection.z);
    const float stackHeight = m_windmillParams.baseSize.y + m_windmillParams.towerSize.y + 0.8f;
    const float strength = glm::clamp(m_windStrength, 0.1f, 3.5f);
    const float scaleFactor = m_windArrowScale * (0.7f + 0.3f * strength);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, stackHeight, 0.0f));
    model = model * glm::rotate(glm::mat4(1.0f), windYaw, glm::vec3(0.0f, 1.0f, 0.0f));
    model = model * glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));

    const glm::mat4 mvp = m_projectionMatrix * m_viewMatrix * model;
    glUniformMatrix4fv(m_lightShader.getUniformLocation("mvpMatrix"), 1, GL_FALSE, glm::value_ptr(mvp));

    const glm::vec3 calmColor { 0.25f, 0.65f, 1.0f };
    const glm::vec3 gustColor { 1.0f, 0.85f, 0.2f };
    const float gustBlend = glm::clamp((strength - 1.0f) / 2.0f, 0.0f, 1.0f);
    const glm::vec3 arrowColor = glm::mix(calmColor, gustColor, gustBlend);
    glUniform3fv(m_lightShader.getUniformLocation("markerColor"), 1, glm::value_ptr(arrowColor));

    glLineWidth(3.0f);
    glDrawArrays(GL_LINES, 0, m_windArrowVertexCount);
    glLineWidth(1.0f);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
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

    auto refreshCameraPath = [this]() {
        if (!m_cameraPathEnabled)
            return;
        Trackball& controlledCamera = activeTrackball();
        updateCameraPath(0.0f, controlledCamera);
    };

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
    ImGui::Text("Day / Night Cycle");
    if (ImGui::Checkbox("Enable day-night cycle", &m_dayNightEnabled))
        updateDayNightCycle(0.0f);
    if (ImGui::Checkbox("Auto advance time", &m_dayNightAutoAdvance))
        updateDayNightCycle(0.0f);
    if (ImGui::SliderFloat("Cycle duration (s)", &m_dayNightCycleDuration, 5.0f, 240.0f, "%.1f")) {
        m_dayNightCycleDuration = std::max(m_dayNightCycleDuration, 1.0f);
        updateDayNightCycle(0.0f);
    }

    bool timeModified = false;
    if (m_dayNightAutoAdvance) {
        ImGui::BeginDisabled();
        ImGui::SliderFloat("Time of day", &m_timeOfDay01, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();
    } else {
        timeModified = ImGui::SliderFloat("Time of day", &m_timeOfDay01, 0.0f, 1.0f, "%.2f");
    }
    if (timeModified)
        updateDayNightCycle(0.0f);

    float clockHours = m_timeOfDay01 * 24.0f;
    int displayHour = static_cast<int>(std::floor(clockHours)) % 24;
    int displayMinute = static_cast<int>(std::round((clockHours - std::floor(clockHours)) * 60.0f)) % 60;
    ImGui::Text("Sun intensity: %.2f", static_cast<double>(m_currentSunIntensity));
    ImGui::Text("Approx time: %02d:%02d", displayHour, displayMinute);

    if (ImGui::TreeNode("Cycle colours")) {
        bool paletteChanged = false;
        paletteChanged |= ImGui::ColorEdit3("Day ambient", glm::value_ptr(m_dayAmbientColor));
        paletteChanged |= ImGui::ColorEdit3("Night ambient", glm::value_ptr(m_nightAmbientColor));
        paletteChanged |= ImGui::ColorEdit3("Day sun", glm::value_ptr(m_daySunColor));
        paletteChanged |= ImGui::ColorEdit3("Night sun", glm::value_ptr(m_nightSunColor));
        if (paletteChanged)
            updateDayNightCycle(0.0f);
        ImGui::TreePop();
    }
    ImGui::Text("Ambient: (%.2f, %.2f, %.2f)",
        static_cast<double>(m_currentAmbientColor.x),
        static_cast<double>(m_currentAmbientColor.y),
        static_cast<double>(m_currentAmbientColor.z));

    ImGui::Separator();
    ImGui::Text("Wind");
    if (ImGui::SliderFloat("Wind heading (deg)", &m_windDirectionAngleDeg, -180.0f, 180.0f, "%.0f deg"))
        updateWindOrientation();
    ImGui::Text("Direction (x,z): (%.2f, %.2f)",
        static_cast<double>(m_windDirection.x),
        static_cast<double>(m_windDirection.z));
    ImGui::Checkbox("Enable gusts", &m_windGustsEnabled);
    if (ImGui::SliderFloat("Base strength", &m_windBaseStrength, 0.0f, 2.5f))
        m_windBaseStrength = std::max(m_windBaseStrength, 0.0f);
    if (ImGui::SliderFloat("Gust amplitude", &m_windGustAmplitude, 0.0f, 2.0f))
        m_windGustAmplitude = std::max(m_windGustAmplitude, 0.0f);
    if (ImGui::SliderFloat("Gust frequency", &m_windGustFrequency, 0.02f, 0.6f))
        m_windGustFrequency = std::max(m_windGustFrequency, 0.01f);
    if (ImGui::SliderFloat("Secondary frequency", &m_windSecondaryFrequency, 0.01f, 0.3f))
        m_windSecondaryFrequency = std::max(m_windSecondaryFrequency, 0.005f);
    ImGui::Text("Strength: %.2f", static_cast<double>(m_windStrength));
    if (ImGui::SliderFloat("Arrow scale", &m_windArrowScale, 0.5f, 3.0f))
        m_windArrowScale = std::clamp(m_windArrowScale, 0.2f, 5.0f);
    if (ImGui::SliderFloat("Particle spawn rate", &m_windParticleSpawnRate, 0.0f, 120.0f))
        m_windParticleSpawnRate = std::max(m_windParticleSpawnRate, 0.0f);
    if (ImGui::SliderFloat("Particle lifetime", &m_windParticleLifetime, 0.5f, 6.0f))
        m_windParticleLifetime = std::max(m_windParticleLifetime, 0.1f);
    if (ImGui::SliderFloat("Particle size", &m_windParticleSize, 2.0f, 24.0f))
        m_windParticleSize = std::max(m_windParticleSize, 1.0f);
    ImGui::Text("Active particles: %zu", m_windParticles.size());
    ImGui::TextUnformatted("Arrow and blades point along the wind heading.");

    ImGui::Separator();
    ImGui::Text("Windmill");
    bool windmillChanged = false;
    windmillChanged |= ImGui::DragFloat("Base width", &m_windmillParams.baseSize.x, 0.05f, 0.2f, 10.0f, "%.2f");
    windmillChanged |= ImGui::DragFloat("Base depth", &m_windmillParams.baseSize.z, 0.05f, 0.2f, 10.0f, "%.2f");
    windmillChanged |= ImGui::DragFloat("Base height", &m_windmillParams.baseSize.y, 0.05f, 0.2f, 10.0f, "%.2f");

    windmillChanged |= ImGui::DragFloat("Tower height", &m_windmillParams.towerSize.y, 0.05f, 0.5f, 15.0f, "%.2f");
    windmillChanged |= ImGui::DragFloat("Tower width", &m_windmillParams.towerSize.x, 0.01f, 0.1f, 5.0f, "%.2f");
    windmillChanged |= ImGui::DragFloat("Tower depth", &m_windmillParams.towerSize.z, 0.01f, 0.1f, 5.0f, "%.2f");

    windmillChanged |= ImGui::DragFloat3("Hub size", glm::value_ptr(m_windmillParams.hubSize), 0.01f, 0.05f, 3.0f, "%.2f");
    windmillChanged |= ImGui::DragFloat("Hub forward offset", &m_windmillParams.hubForwardOffset, 0.01f, -2.0f, 2.0f, "%.2f");
    windmillChanged |= ImGui::DragFloat("Hub vertical offset", &m_windmillParams.hubVerticalOffset, 0.01f, -2.0f, 2.0f, "%.2f");

    windmillChanged |= ImGui::DragFloat("Arm length", &m_windmillParams.armLength, 0.05f, 0.2f, 6.0f, "%.2f");
    windmillChanged |= ImGui::DragFloat("Arm width", &m_windmillParams.armWidth, 0.01f, 0.05f, 2.0f, "%.2f");
    windmillChanged |= ImGui::DragFloat("Arm thickness", &m_windmillParams.armThickness, 0.01f, 0.05f, 2.0f, "%.2f");

    windmillChanged |= ImGui::ColorEdit3("Structure colour", glm::value_ptr(m_windmillParams.structureColor));
    float rotationSpeed = m_windmillParams.rotationSpeedDegPerSec;
    if (ImGui::SliderFloat("Rotation speed (deg/s)", &rotationSpeed, -360.0f, 360.0f, "%.1f"))
        m_windmillParams.rotationSpeedDegPerSec = std::clamp(rotationSpeed, -720.0f, 720.0f);

    if (windmillChanged) {
        sanitizeWindmillParams();
        m_windmillDirty = true;
    }

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

    ImGui::Separator();
    ImGui::Text("Camera Bezier Tour");
    bool cameraEnabledBefore = m_cameraPathEnabled;
    if (ImGui::Checkbox("Enable camera tour", &m_cameraPathEnabled)) {
        if (m_cameraPathEnabled && !cameraEnabledBefore)
            m_cameraPathDistance = m_lightPathDistance;
        refreshCameraPath();
    }
    if (ImGui::SliderFloat("Camera tour speed", &m_cameraPathSpeed, 0.0f, 5.0f))
        refreshCameraPath();
    if (ImGui::Checkbox("Aim camera at tour target", &m_cameraPathAimAtTarget))
        refreshCameraPath();
    if (m_cameraPathAimAtTarget) {
        ImGui::BeginDisabled();
        ImGui::SliderFloat("Camera look-ahead", &m_cameraPathLookAhead, 0.1f, 8.0f);
        ImGui::EndDisabled();
    } else {
        if (ImGui::SliderFloat("Camera look-ahead", &m_cameraPathLookAhead, 0.1f, 8.0f))
            refreshCameraPath();
    }
    if (ImGui::Button("Align camera with light tour")) {
        m_cameraPathDistance = wrapPathDistance(m_lightPathDistance);
        refreshCameraPath();
    }

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
        m_cameraPathDistance = wrapPathDistance(m_cameraPathDistance);
        if (!m_lights.empty() && !m_lightPathSamples.empty()) {
            const bool originalEnabled = m_lightPathEnabled;
            m_lightPathEnabled = true;
            updateLightPath(0.0f);
            m_lightPathEnabled = originalEnabled;
        }
        if (m_cameraPathEnabled) {
            Trackball& camRef = activeTrackball();
            updateCameraPath(0.0f, camRef);
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

		ImGui::Separator();
        ImGui::Text("Shadows");
        ImGui::Checkbox("Shadows", &m_shadows);
		ImGui::Checkbox("Soft Shadows (PCF)", &m_pcf);
		ImGui::Checkbox("Show Normal Map", &m_useNormalMap);

		ImGui::Separator();
        ImGui::Text("Material Textures");
        ImGui::Checkbox("Use Material", &m_useMaterial);
        ImGui::ColorEdit3("Kd", glm::value_ptr(m_gpuMaterial.kd));
        ImGui::ColorEdit3("Ks", glm::value_ptr(m_gpuMaterial.ks));
        ImGui::SliderFloat("Shininess", &m_gpuMaterial.shininess, 1.0f, 256.0f);
    }

    ImGui::End();
}

void Application::sanitizeWindmillParams()
{
    constexpr float minDimension = 0.05f;
    auto clampVec3 = [minDimension](glm::vec3& value) {
        value.x = std::max(value.x, minDimension);
        value.y = std::max(value.y, minDimension);
        value.z = std::max(value.z, minDimension);
    };

    clampVec3(m_windmillParams.baseSize);
    clampVec3(m_windmillParams.towerSize);
    clampVec3(m_windmillParams.hubSize);

    m_windmillParams.hubForwardOffset = std::clamp(m_windmillParams.hubForwardOffset, -5.0f, 5.0f);
    m_windmillParams.hubVerticalOffset = std::clamp(m_windmillParams.hubVerticalOffset, -5.0f, 5.0f);

    m_windmillParams.armLength = std::max(m_windmillParams.armLength, minDimension);
    m_windmillParams.armWidth = std::max(m_windmillParams.armWidth, minDimension * 0.5f);
    m_windmillParams.armThickness = std::max(m_windmillParams.armThickness, minDimension * 0.5f);

    m_windmillParams.structureColor.x = std::clamp(m_windmillParams.structureColor.x, 0.0f, 1.0f);
    m_windmillParams.structureColor.y = std::clamp(m_windmillParams.structureColor.y, 0.0f, 1.0f);
    m_windmillParams.structureColor.z = std::clamp(m_windmillParams.structureColor.z, 0.0f, 1.0f);
    m_windmillParams.rotationSpeedDegPerSec = std::clamp(m_windmillParams.rotationSpeedDegPerSec, -720.0f, 720.0f);
}

void Application::rebuildWindmillMesh()
{
    sanitizeWindmillParams();
    const WindmillMeshes cpuMeshes = buildWindmillMeshes(m_windmillParams);
    m_windmillHubPosition = computeHubPosition(m_windmillParams);

    m_windmillBodyMesh.reset();
    m_windmillRotorMesh.reset();

    m_windmillBodyMesh.emplace(cpuMeshes.body);
    m_windmillRotorMesh.emplace(cpuMeshes.rotor);
    m_windmillDirty = false;
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
