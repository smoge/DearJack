// File: audio_engine.cpp

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <jack/jack.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

// Constants
constexpr double TWO_PI = 2.0 * M_PI;
constexpr float DEFAULT_FREQUENCY = 440.0f; // Default frequency in Hz
constexpr float DEFAULT_AMPLITUDE = 0.5f;   // Default amplitude

// GLFW error callback function
static void glfw_error_callback(int error, const char *description) {
    std::fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

// DSP base class
class DSP {
public:
    virtual ~DSP() = default;
    virtual void process_audio(jack_nframes_t nframes, float **inputs, float **outputs, double sample_rate) = 0;
    virtual int get_num_inputs() const = 0;
    virtual int get_num_outputs() const = 0;
    virtual void set_parameter(const std::string &name, float value) = 0;
    virtual float get_parameter(const std::string &name) const = 0;
    virtual std::vector<std::string> get_parameter_names() const = 0;
};

// SinOsc DSP implementation
class SinOsc : public DSP {
public:
    SinOsc() : phase(0.0), frequency(DEFAULT_FREQUENCY), amplitude(DEFAULT_AMPLITUDE) {}

    void set_parameter(const std::string &name, float value) override {
        if (name == "frequency") {
            frequency.store(value);
        } else if (name == "amplitude") {
            amplitude.store(value);
        }
    }

    float get_parameter(const std::string &name) const override {
        if (name == "frequency") {
            return frequency.load();
        } else if (name == "amplitude") {
            return amplitude.load();
        }
        return 0.0f;
    }

    std::vector<std::string> get_parameter_names() const override {
        return {"frequency", "amplitude"};
    }

    void process_audio(jack_nframes_t nframes, float **inputs, float **outputs, double sample_rate) override {
        double phase_increment = TWO_PI * frequency.load() / sample_rate;
        float amp = amplitude.load();
        for (jack_nframes_t i = 0; i < nframes; ++i) {
            outputs[0][i] = amp * std::sin(phase);
            phase += phase_increment;
            if (phase >= TWO_PI) {
                phase -= TWO_PI;
            }
        }
    }

    int get_num_inputs() const override { return 0; }
    int get_num_outputs() const override { return 1; }

private:
    double phase;
    std::atomic<double> frequency;
    std::atomic<float> amplitude;
};

// SquareWave DSP implementation
class SquareWave : public DSP {
public:
    SquareWave() : phase(0.0), frequency(DEFAULT_FREQUENCY) {}

    void set_parameter(const std::string &name, float value) override {
        if (name == "frequency") {
            frequency.store(value);
        }
    }

    float get_parameter(const std::string &name) const override {
        if (name == "frequency") {
            return frequency.load();
        }
        return 0.0f;
    }

    std::vector<std::string> get_parameter_names() const override {
        return {"frequency"};
    }

    void process_audio(jack_nframes_t nframes, float **inputs, float **outputs, double sample_rate) override {
        double phase_increment = TWO_PI * frequency.load() / sample_rate;
        for (jack_nframes_t i = 0; i < nframes; ++i) {
            outputs[0][i] = (phase < M_PI) ? 1.0f : -1.0f;
            phase += phase_increment;
            if (phase >= TWO_PI) {
                phase -= TWO_PI;
            }
        }
    }

    int get_num_inputs() const override { return 0; }
    int get_num_outputs() const override { return 1; }

private:
    double phase;
    std::atomic<double> frequency;
};

// SawWave DSP implementation
class SawWave : public DSP {
public:
    SawWave() : phase(0.0), frequency(DEFAULT_FREQUENCY) {}

    void set_parameter(const std::string &name, float value) override {
        if (name == "frequency") {
            frequency.store(value);
        }
    }

    float get_parameter(const std::string &name) const override {
        if (name == "frequency") {
            return frequency.load();
        }
        return 0.0f;
    }

    std::vector<std::string> get_parameter_names() const override {
        return {"frequency"};
    }

    void process_audio(jack_nframes_t nframes, float **inputs, float **outputs, double sample_rate) override {
        double phase_increment = TWO_PI * frequency.load() / sample_rate;
        for (jack_nframes_t i = 0; i < nframes; ++i) {
            outputs[0][i] = 2.0f * (phase / TWO_PI) - 1.0f;
            phase += phase_increment;
            if (phase >= TWO_PI) {
                phase -= TWO_PI;
            }
        }
    }

    int get_num_inputs() const override { return 0; }
    int get_num_outputs() const override { return 1; }

private:
    double phase;
    std::atomic<double> frequency;
};

// DSPFactory class
class DSPFactory {
public:
    using DSPCreator = std::function<std::unique_ptr<DSP>()>;

    static DSPFactory &instance() {
        static DSPFactory instance;
        return instance;
    }

    void register_dsp(const std::string &name, DSPCreator creator) {
        creators[name] = creator;
    }

    std::unique_ptr<DSP> create_dsp(const std::string &name) {
        auto it = creators.find(name);
        if (it != creators.end()) {
            return it->second();
        }
        throw std::runtime_error("Unknown DSP type: " + name);
    }

private:
    DSPFactory() = default;
    std::unordered_map<std::string, DSPCreator> creators;
};

// JackClient template class
template <typename DSPType>
class JackClient {
public:
    JackClient(const char *client_name);
    ~JackClient();

    DSPType *get_dsp() { return &dsp; }
    const char *get_name() const { return name.c_str(); }

private:
    static int process(jack_nframes_t nframes, void *arg);
    static void jack_shutdown(void *arg);

    void process_audio(jack_nframes_t nframes);

    jack_client_t *client = nullptr;
    std::vector<jack_port_t *> input_ports;
    std::vector<jack_port_t *> output_ports;
    DSPType dsp;
    std::string name;
};

template <typename DSPType>
JackClient<DSPType>::JackClient(const char *client_name) : name(client_name) {
    client = jack_client_open(name.c_str(), JackNullOption, nullptr);
    if (!client) {
        throw std::runtime_error("Failed to open JACK client");
    }

    if (jack_set_process_callback(client, process, this) != 0) {
        jack_client_close(client);
        throw std::runtime_error("Failed to set JACK process callback");
    }

    jack_on_shutdown(client, jack_shutdown, this);

    int num_inputs = dsp.get_num_inputs();
    int num_outputs = dsp.get_num_outputs();

    for (int i = 0; i < num_inputs; ++i) {
        input_ports.push_back(
            jack_port_register(client, ("input" + std::to_string(i)).c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0));
    }

    for (int i = 0; i < num_outputs; ++i) {
        output_ports.push_back(
            jack_port_register(client, ("output" + std::to_string(i)).c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0));
    }

    if (jack_activate(client)) {
        jack_client_close(client);
        throw std::runtime_error("Failed to activate JACK client");
    }
}

template <typename DSPType>
JackClient<DSPType>::~JackClient() {
    if (client) {
        jack_client_close(client);
    }
}

template <typename DSPType>
int JackClient<DSPType>::process(jack_nframes_t nframes, void *arg) {
    auto *self = static_cast<JackClient<DSPType> *>(arg);
    self->process_audio(nframes);
    return 0;
}

template <typename DSPType>
void JackClient<DSPType>::jack_shutdown(void *arg) {
    // Add necessary cleanup here
    auto *self = static_cast<JackClient<DSPType>*>(arg);
    self->client = nullptr;
    std::cerr << "JACK client has been shut down" << std::endl;
}

template <typename DSPType>
void JackClient<DSPType>::process_audio(jack_nframes_t nframes) {
    double sample_rate = jack_get_sample_rate(client);

    std::vector<float *> inputs(input_ports.size());
    std::vector<float *> outputs(output_ports.size());

    for (size_t i = 0; i < input_ports.size(); ++i) {
        inputs[i] = static_cast<float *>(jack_port_get_buffer(input_ports[i], nframes));
    }

    for (size_t i = 0; i < output_ports.size(); ++i) {
        outputs[i] = static_cast<float *>(jack_port_get_buffer(output_ports[i], nframes));
    }

    dsp.process_audio(nframes, inputs.data(), outputs.data(), sample_rate);
}

// Render GUI for a JackClient
template <typename DSPType>
void render_client_gui(JackClient<DSPType> *client) {
    ImGui::Begin(client->get_name());
    ImGui::Text("Simple DSP");
    DSPType *dsp = client->get_dsp();
    for (const auto &param : dsp->get_parameter_names()) {
        float value = dsp->get_parameter(param);
        if (ImGui::SliderFloat(param.c_str(), &value, 0.0f, 1.0f)) {
            dsp->set_parameter(param, value);
        }
    }
    ImGui::End();
}

int main(int, char **) {
    // Register DSP types
    DSPFactory::instance().register_dsp(
        "SinOsc", []() { return std::make_unique<SinOsc>(); });
    DSPFactory::instance().register_dsp(
        "SquareWave", []() { return std::make_unique<SquareWave>(); });
    DSPFactory::instance().register_dsp(
        "SawWave", []() { return std::make_unique<SawWave>(); });

    // Separate vectors for each DSP type
    std::vector<std::unique_ptr<JackClient<SinOsc>>> sin_osc_clients;
    std::vector<std::unique_ptr<JackClient<SquareWave>>> square_wave_clients;
    std::vector<std::unique_ptr<JackClient<SawWave>>> saw_wave_clients;
    std::string selected_dsp_type = "SinOsc"; // Default DSP type

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }

    const char *glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow *window = glfwCreateWindow(1280, 720, "Audio Engine", nullptr, nullptr);
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

        // Add buttons to add/remove JackClients
        if (ImGui::Button("Add JackClient")) {
            static int client_count = 1;
            std::string client_name = "DearJack" + std::to_string(client_count++);
            if (selected_dsp_type == "SinOsc") {
                sin_osc_clients.push_back(std::make_unique<JackClient<SinOsc>>(client_name.c_str()));
            } else if (selected_dsp_type == "SquareWave") {
                square_wave_clients.push_back(std::make_unique<JackClient<SquareWave>>(client_name.c_str()));
            } else if (selected_dsp_type == "SawWave") {
                saw_wave_clients.push_back(std::make_unique<JackClient<SawWave>>(client_name.c_str()));
            }
        }
        if (ImGui::Button("Remove Last JackClient")) {
            if (selected_dsp_type == "SinOsc" && !sin_osc_clients.empty()) {
                sin_osc_clients.pop_back();
            } else if (selected_dsp_type == "SquareWave" && !square_wave_clients.empty()) {
                square_wave_clients.pop_back();
            } else if (selected_dsp_type == "SawWave" && !saw_wave_clients.empty()) {
                saw_wave_clients.pop_back();
            }
        }

        // Dropdown to select DSP type
        const char *dsp_types[] = {"SinOsc", "SquareWave", "SawWave"};
        static int current_dsp_type = 0;
        if (ImGui::Combo("DSP Type", &current_dsp_type, dsp_types, IM_ARRAYSIZE(dsp_types))) {
            selected_dsp_type = dsp_types[current_dsp_type];
        }

        // Render GUI for each JackClient
        for (auto &client : sin_osc_clients) {
            render_client_gui(client.get());
        }
        for (auto &client : square_wave_clients) {
            render_client_gui(client.get());
        }
        for (auto &client : saw_wave_clients) {
            render_client_gui(client.get());
        }

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
