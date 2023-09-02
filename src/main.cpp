#include <SDL2/SDL.h>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream>
#include <vector>
#include <cassert>
#include "color.h"
#include "print.h"
#include "framebuffer.h"
#include "uniforms.h"
#include "shaders.h"
#include "fragment.h"
#include "vertex.h"
#include "triangle.h"
#include "camera.h"
#include "ObjLoader.h"


SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
Color currentColor;

bool init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cout << "Error: SDL_Init failed." << std::endl;
        return false;
    }

    window = SDL_CreateWindow("SDL_SpaceShip", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cout << "Error: Could not create SDL window." << std::endl;
        SDL_Quit();
        return false;
    }


    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cout << "Error: Could not create SDL renderer." << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    return true;
}

void setColor(const Color& color) {
    currentColor = color;
}

// Step 1: Vertex Shader
std::vector<Vertex> vertexShaderStep(const std::vector<glm::vec3>& VBO, const Uniforms& uniforms) {
    std::vector<Vertex> transformedVertices(VBO.size() / 2);
    for (size_t i = 0; i < VBO.size() / 2; ++i) {
        Vertex vertex = { VBO[i * 2], VBO[i * 2 + 1] };
        transformedVertices[i] = vertexShader(vertex, uniforms);
    }
    return transformedVertices;
}

// Step 2: Primitive Assembly
std::vector<std::vector<Vertex>> primitiveAssemblyStep(const std::vector<Vertex>& transformedVertices) {
    std::vector<std::vector<Vertex>> assembledVertices(transformedVertices.size() / 3);
    for (size_t i = 0; i < transformedVertices.size() / 3; ++i) {
        Vertex edge1 = transformedVertices[3 * i];
        Vertex edge2 = transformedVertices[3 * i + 1];
        Vertex edge3 = transformedVertices[3 * i + 2];
        assembledVertices[i] = { edge1, edge2, edge3 };
    }
    return assembledVertices;
}

// Step 3: Rasterization
std::vector<Fragment> rasterizationStep(const std::vector<std::vector<Vertex>>& assembledVertices) {
    std::vector<Fragment> concurrentFragments;
    for (size_t i = 0; i < assembledVertices.size(); ++i) {
        std::vector<Fragment> rasterizedTriangle = triangle(
            assembledVertices[i][0],
            assembledVertices[i][1],
            assembledVertices[i][2]
        );
        std::lock_guard<std::mutex> lock(std::mutex);
        for (const auto& fragment : rasterizedTriangle) {
            concurrentFragments.push_back(fragment);
        }
    }
    return concurrentFragments;
}

// Step 4: Fragment Shader
void fragmentShaderStep( std::vector<Fragment>& concurrentFragments) {
    for (size_t i = 0; i < concurrentFragments.size(); ++i) {
        const Fragment& fragment = fragmentShader(concurrentFragments[i]);
        // Apply the fragment shader to compute the final color
        point(fragment); // Be aware of potential race conditions here
    }
}

// Combined function
void render(const std::vector<glm::vec3>& VBO, const Uniforms& uniforms) {
    // Step 1: Vertex Shader
    std::vector<Vertex> transformedVertices = vertexShaderStep(VBO, uniforms);

    // Step 2: Primitive Assembly
    std::vector<std::vector<Vertex>> assembledVertices = primitiveAssemblyStep(transformedVertices);

    // Step 3: Rasterization
    std::vector<Fragment> concurrentFragments = rasterizationStep(assembledVertices);

    // Step 4: Fragment Shader
    fragmentShaderStep(concurrentFragments);
}


glm::mat4 createViewportMatrix(size_t screenWidth, size_t screenHeight) {
    glm::mat4 viewport = glm::mat4(1.0f);

    // Scale
    viewport = glm::scale(viewport, glm::vec3(screenWidth / 2.0f, screenHeight / 2.0f, 0.5f));

    // Translate
    viewport = glm::translate(viewport, glm::vec3(1.0f, 1.0f, 0.5f));

    return viewport;
}

int main(int argc, char* argv[]) {
    if (!init()) {
        return 1;
    }

    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals;
    std::vector<Face> faces;
    std::vector<glm::vec3> vertexBufferObject; // This will contain both vertices and normals

    std::string filePath = "src/SpaceshipJUA21440.obj";

    if (!loadOBJ(filePath, vertices, normals, faces)) {
            std::cout << "Error: Could not load OBJ file." << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    for (const auto& face : faces)
    {
        for (int i = 0; i < 3; ++i)
        {
            // Get the vertex position
            glm::vec3 vertexPosition = vertices[face.vertexIndices[i]];

            // Get the normal for the current vertex
            glm::vec3 vertexNormal = normals[face.normalIndices[i]];

            // Add the vertex position and normal to the vertex array
            vertexBufferObject.push_back(vertexPosition);
            vertexBufferObject.push_back(vertexNormal);
        }
    }

    Uniforms uniforms;

    glm::mat4 model = glm::mat4(1);
    glm::mat4 view = glm::mat4(1);
    glm::mat4 projection = glm::mat4(1);

    glm::vec3 translationVector(0.0f, 0.0f, 0.0f);
    float a = 45.0f;
    glm::vec3 rotationAxis(0.0f, 1.0f, 0.0f); // Rotate around the Y-axis
    glm::vec3 scaleFactor(1.0f, 1.0f, 1.0f);

    glm::mat4 translation = glm::translate(glm::mat4(1.0f), translationVector);
    glm::mat4 scale = glm::scale(glm::mat4(1.0f), scaleFactor);

    // Initialize a Camera object
    Camera camera;
    camera.cameraPosition = glm::vec3(0.0f, 0.0f, 20.0f);
    camera.targetPosition = glm::vec3(0.0f, 0.0f, 0.0f);
    camera.upVector = glm::vec3(0.0f, 1.0f, 0.0f);

    // Projection matrix
    float fovRadians = glm::radians(45.0f);
    float aspectRatio = static_cast<float>(SCREEN_WIDTH) / static_cast<float>(SCREEN_HEIGHT); // Assuming a screen resolution of 800x600
    float nearClip = 0.1f;
    float farClip = 100.0f;
    uniforms.projection = glm::perspective(fovRadians, aspectRatio, nearClip, farClip);

    uniforms.viewport = createViewportMatrix(SCREEN_WIDTH, SCREEN_HEIGHT);
    Uint32 frameStart, frameTime;
    std::string title = "FPS: ";

    bool running = true;
    while (running) {
        frameStart = SDL_GetTicks();

        // Calculate frames per second and update window title
        if (frameTime > 0) {
            std::ostringstream titleStream;
            titleStream << "FPS: " << 1000.0 / frameTime;  // Milliseconds to seconds
            SDL_SetWindowTitle(window, titleStream.str().c_str()); // Use the global 'window' pointer
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        a += 1.0;
        glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(a), rotationAxis);

        // Calculate the model matrix
        uniforms.model = translation * rotation * scale;

        // Create the view matrix using the Camera object
        uniforms.view = glm::lookAt(
            camera.cameraPosition, // The position of the camera
            camera.targetPosition, // The point the camera is looking at
            camera.upVector        // The up vector defining the camera's orientation
        );

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        clearFramebuffer();

        setColor(Color(255, 255, 0));
        render(vertexBufferObject, uniforms);

        renderBuffer(renderer);

        frameTime = SDL_GetTicks() - frameStart;


    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
