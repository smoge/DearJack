

#include "imgui.h"
#include "../external/imgui/backends/imgui_impl_glfw.h"

#include "../external/imgui/backends/imgui_impl_opengl3.h"
#include "imgui_impl_opengl3.h"
#include <jack/jack.h>
#include <GLFW/glfw3.h>
#include <cstdio>  // For fprintf and stderr
#include <cstdlib> // For exit

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

jack_client_t* client = nullptr; jack_port_t* output_port = nullptr;

int process(jack_nframes_t nframes, void* arg)
{
    jack_default_audio_sample_t *out = (jack_default_audio_sample_t*)jack_port_get_buffer(output_port, nframes);
    for (jack_nframes_t i = 0; i < nframes; i++) {
        out[i] = 0.0; // Silence
    }
    return 0;
}

void jack_shutdown(void* arg)
{
    exit(1);
}

int main(int, char**)
{
    // Setup GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return -1;

    // Setup window
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Dear ImGui Example", NULL, NULL);
    if (window == NULL)
        return -1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Initialize Jack client
    client = jack_client_open("DearJack", JackNullOption, nullptr);
    if (client == nullptr) {
        fprintf(stderr, "jack_client_open() failed\n");
        return 1;
    }

    jack_set_process_callback(client, process, 0);
    jack_on_shutdown(client, jack_shutdown, 0);

    output_port = jack_port_register(client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (output_port == nullptr) {
        fprintf(stderr, "no more JACK ports available\n");
        return 1;
    }

    if (jack_activate(client)) {
        fprintf(stderr, "cannot activate client");
        return 1;
    }

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        // Poll and handle events
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Show a simple window
        {
            ImGui::Begin("Hello, world!");
            ImGui::Text("This is some useful text.");
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    jack_client_close(client);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
