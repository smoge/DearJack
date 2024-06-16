#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <atomic>
#include <cmath>   // For sin()
#include <cstdio>  // For fprintf and stderr
#include <cstdlib> // For exit
#include <jack/jack.h>
#include <thread>  // For std::thread

static void glfw_error_callback(int error, const char *description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

jack_client_t *client = nullptr;
jack_port_t *output_port = nullptr;
double phase = 0.0;
std::atomic<double> frequency(440.0); // Controls the frequency of the sine wave
std::atomic<bool> keep_running(true); // Control variable for the audio thread

int process(jack_nframes_t nframes, void *arg) {
    (void)arg; // Avoid unused parameter warning
    double sample_rate = jack_get_sample_rate(client);
    double phase_increment = 2.0 * M_PI * frequency.load() / sample_rate;
    jack_default_audio_sample_t *out =
        (jack_default_audio_sample_t *)jack_port_get_buffer(output_port, nframes);
    for (jack_nframes_t i = 0; i < nframes; i++) {
        out[i] = sin(phase); // Generate sine wave
        phase += phase_increment;
        if (phase >= 2.0 * M_PI) {
            phase -= 2.0 * M_PI;
        }
    }
    return 0;
}

void jack_shutdown(void *arg) {
    (void)arg; // Avoid unused parameter warning
    exit(1);
}

void audio_thread_function() {
    // Initialize Jack client
    client = jack_client_open("DearJack", JackNullOption, nullptr);
    if (client == nullptr) {
        fprintf(stderr, "jack_client_open() failed\n");
        return;
    }

    jack_set_process_callback(client, process, nullptr);
    jack_on_shutdown(client, jack_shutdown, nullptr);

    output_port = jack_port_register(client, "output", JACK_DEFAULT_AUDIO_TYPE,
                                     JackPortIsOutput, 0);
    if (output_port == nullptr) {
        fprintf(stderr, "no more JACK ports available\n");
        jack_client_close(client);
        return;
    }

    if (jack_activate(client)) {
        fprintf(stderr, "cannot activate client");
        jack_client_close(client);
        return;
    }

    // Keep the thread running to handle audio processing
    while (keep_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    jack_client_close(client);
}

int main(int, char **) {
    // Start the audio thread
    std::thread audio_thread(audio_thread_function);

    // Setup GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return -1;

    // Setup window
    const char *glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow *window =
        glfwCreateWindow(1280, 720, "Dear ImGui Example", NULL, NULL);
    if (window == NULL)
        return -1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Poll and handle events
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Show a window with a slider to control the sine wave frequency
        {
            ImGui::Begin("Hello, world!");
            ImGui::Text("This is some useful text.");
            static float freq = 440.0f; // Slider value to control frequency
            if (ImGui::SliderFloat("Frequency", &freq, 20.0f, 2000.0f)) {
                frequency.store(static_cast<double>(freq));
            }
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

    glfwDestroyWindow(window);
    glfwTerminate();

    // Signal the audio thread to exit
    keep_running.store(false);
    if (audio_thread.joinable()) {
        audio_thread.join();
    }

    return 0;
}
