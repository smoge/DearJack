#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <jack/jack.h>
#include <memory>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <GL/gl.h>

// GLFW error callback function
static void glfw_error_callback(int error, const char *description) {
    std::fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

class JackClient {
public:
    explicit JackClient(const char *client_name);
    ~JackClient();
    void set_frequency(double freq);

private:
    static int process(jack_nframes_t nframes, void *arg);
    static void jack_shutdown(void *arg);
    
    void process_audio(jack_nframes_t nframes);

    jack_client_t *client = nullptr;
    jack_port_t *output_port = nullptr;
    double phase = 0.0;
    std::atomic<double> frequency = 440.0;
};

JackClient::JackClient(const char *client_name) {
    client = jack_client_open(client_name, JackNullOption, nullptr);
    if (!client) {
        throw std::runtime_error("Failed to open JACK client");
    }

    jack_set_process_callback(client, process, this);
    jack_on_shutdown(client, jack_shutdown, this);
    output_port = jack_port_register(client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (!output_port) {
        jack_client_close(client);
        throw std::runtime_error("Failed to register JACK port");
    }
    if (jack_activate(client)) {
        jack_client_close(client);
        throw std::runtime_error("Failed to activate JACK client");
    }
}

JackClient::~JackClient() {
    if (client) {
        jack_client_close(client);
    }
}

void JackClient::set_frequency(double freq) {
    frequency.store(freq);
}

int JackClient::process(jack_nframes_t nframes, void *arg) {
    auto *self = static_cast<JackClient *>(arg);
    self->process_audio(nframes);
    return 0;
}

void JackClient::jack_shutdown(void *arg) {
    std::exit(1);
}

void JackClient::process_audio(jack_nframes_t nframes) {
    double sample_rate = jack_get_sample_rate(client);
    double phase_increment = 2.0 * M_PI * frequency.load() / sample_rate;
    auto *out = static_cast<jack_default_audio_sample_t *>(jack_port_get_buffer(output_port, nframes));
    for (jack_nframes_t i = 0; i < nframes; ++i) {
        out[i] = std::sin(phase);
        phase += phase_increment;
        if (phase >= 2.0 * M_PI) {
            phase -= 2.0 * M_PI;
        }
    }
}

int main(int, char **) {
    std::unique_ptr<JackClient> jack_client;

    try {
        jack_client = std::make_unique<JackClient>("DearJack");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Error initializing JackClient: %s\n", e.what());
        return 1;
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }

    const char *glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow *window = glfwCreateWindow(1280, 720, "Dear ImGui Example", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Hello, world!");
        ImGui::Text("This is some useful text.");
        static float freq = 440.0f;
        if (ImGui::SliderFloat("Frequency", &freq, 20.0f, 2000.0f)) {
            if (jack_client) {
                jack_client->set_frequency(static_cast<double>(freq));
            }
        }
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
