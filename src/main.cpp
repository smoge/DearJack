#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <jack/jack.h>
#include <memory>
#include <functional>
#include <mutex>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

// Constants
constexpr double TWO_PI = 2.0 * M_PI;
constexpr float DEFAULT_FREQUENCY = 440.0f; // Default frequency in Hz
constexpr float DEFAULT_AMPLITUDE = 0.5f;   // Default amplitude

// GLFW error callback function
void glfw_error_callback(int error, const char *description) noexcept {
    std::fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

// DSP base class
// Abstract base class defining the interface for DSP processors
class DSP {
public:
    // Virtual destructor for safe polymorphic destruction
    virtual ~DSP() = default;

    // Process audio frames
    // @param nframes: Number of frames to process
    // @param inputs: Array of input buffers
    // @param outputs: Array of output buffers
    // @param sample_rate: Sample rate of the audio stream
    virtual void process_audio(jack_nframes_t nframes, float **inputs,
                               float **outputs, double sample_rate) = 0;

    // Get the number of input channels
    // @return: Number of input channels
    virtual int get_num_inputs() const = 0;

    // Get the number of output channels
    // @return: Number of output channels
    virtual int get_num_outputs() const = 0;

    // Set a parameter's value
    // @param name: Name of the parameter
    // @param value: New value for the parameter
    virtual void set_parameter(const std::string &name,
                               const std::variant<float, int, std::string> &value) = 0;

    // Get a parameter's value
    // @param name: Name of the parameter
    // @return: Value of the parameter
    virtual std::variant<float, int, std::string> get_parameter(const std::string &name) const = 0;

    // Get the names of all available parameters
    // @return: Vector containing the names of all parameters
    virtual std::vector<std::string> get_parameter_names() const = 0;
};

// SinOsc DSP implementation
/**
 * Class representing a Sinusoidal Oscillator DSP.
 * 
 * This class implements the DSP interface with methods to set parameters like frequency and amplitude,
 * get parameter values, process audio frames, and retrieve parameter names.
 * The oscillator generates a sine wave based on the frequency and amplitude parameters.
 * 
 * Parameters:
 * - frequency: Frequency of the sine wave in Hz.
 * - amplitude: Amplitude of the sine wave.
 * 
 * Methods:
 * - set_parameter: Set the value of a parameter (frequency or amplitude).
 * - get_parameter: Get the value of a parameter (frequency or amplitude).
 * - get_parameter_names: Get the names of available parameters (frequency, amplitude).
 * - process_audio: Process audio frames by generating a sine wave.
 * - get_num_inputs: Get the number of input channels (always 0 for this oscillator).
 * - get_num_outputs: Get the number of output channels (always 1 for this oscillator).
 * 
 * Member Variables:
 * - phase: Current phase of the oscillator.
 * - frequency: Atomic variable for storing the frequency.
 * - amplitude: Atomic variable for storing the amplitude.
 */
 class SinOsc : public DSP {
public:
    SinOsc()
        : phase(0.0), frequency(DEFAULT_FREQUENCY), amplitude(DEFAULT_AMPLITUDE) {}

    void set_parameter(const std::string &name,
                       const std::variant<float, int, std::string> &value) override {
        if (name == "frequency") {
            frequency.store(std::get<float>(value));
        } else if (name == "amplitude") {
            amplitude.store(std::get<float>(value));
        }
    }

    std::variant<float, int, std::string> get_parameter(const std::string &name) const override {
        if (name == "frequency") {
            return static_cast<float>(frequency.load());
        } else if (name == "amplitude") {
            return static_cast<float>(amplitude.load());
        }
        throw std::runtime_error("Unknown parameter: " + name);
    }

    std::vector<std::string> get_parameter_names() const override {
        return {"frequency", "amplitude"};
    }

    void process_audio(jack_nframes_t nframes, float **inputs, float **outputs,
                       double sample_rate) override {
        double phase_increment = TWO_PI * frequency.load() / sample_rate;
        float amp = amplitude.load();
        double local_phase = phase;

        for (jack_nframes_t i = 0; i < nframes; ++i) {
            outputs[0][i] = amp * std::sin(local_phase);
            local_phase += phase_increment;
            if (local_phase >= TWO_PI) {
                local_phase -= TWO_PI;
            }
        }

        phase = local_phase;
    }

    int get_num_inputs() const override { return 0; }
    int get_num_outputs() const override { return 1; }

private:
    double phase;
    std::atomic<double> frequency;
    std::atomic<float> amplitude;
};



// DSPFactory class
// Thread-safe DSPFactory class
class DSPFactory {
public:
    using DSPCreator = std::function<std::unique_ptr<DSP>()>;

    static DSPFactory& instance() {
        static DSPFactory instance;
        return instance;
    }

    void register_dsp(const std::string &name, DSPCreator creator) {
        std::lock_guard<std::mutex> lock(mutex_);
        creators[name] = std::move(creator);
    }

    std::unique_ptr<DSP> create_dsp(const std::string &name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = creators.find(name);
        if (it != creators.end()) {
            return it->second();
        }
        throw std::runtime_error("Unknown DSP type: " + name);
    }

    std::vector<std::string> get_registered_dsps() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> dsp_names;
        for (const auto &[key, _] : creators) {
            dsp_names.push_back(key);
        }
        return dsp_names;
    }

private:
    DSPFactory() = default;
    mutable std::mutex mutex_; // Mutex for thread-safe access
    std::unordered_map<std::string, DSPCreator> creators;
};


// PolyphonicDSP class to handle multiple voices
// PolyphonicDSP class
class PolyphonicDSP : public DSP {
public:
    PolyphonicDSP(std::function<std::unique_ptr<DSP>()> create_dsp, int num_voices)
        : create_dsp(std::move(create_dsp)), voices(num_voices) {
        for (auto &voice : voices) {
            voice = this->create_dsp();
        }
    }

    void set_parameter(const std::string &name,
                       const std::variant<float, int, std::string> &value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &voice : voices) {
            voice->set_parameter(name, value);
        }
    }

    std::variant<float, int, std::string> get_parameter(const std::string &name) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return voices[0]->get_parameter(name);
    }

    std::vector<std::string> get_parameter_names() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return voices[0]->get_parameter_names();
    }

    void process_audio(jack_nframes_t nframes, float **inputs, float **outputs,
                       double sample_rate) override {
        std::vector<float> mixed_output(nframes, 0.0f);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto &voice : voices) {
                std::vector<float *> voice_outputs(1, mixed_output.data());
                voice->process_audio(nframes, inputs, voice_outputs.data(), sample_rate);
            }
        }

        for (jack_nframes_t i = 0; i < nframes; ++i) {
            outputs[0][i] = mixed_output[i];
        }
    }

    int get_num_inputs() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return voices[0]->get_num_inputs();
    }

    int get_num_outputs() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return voices[0]->get_num_outputs();
    }

private:
    std::function<std::unique_ptr<DSP>()> create_dsp;
    std::vector<std::unique_ptr<DSP>> voices;
    mutable std::mutex mutex_; // Mutex for thread-safe access
};

// ThreadManager class for handling threading
class ThreadManager {
public:
    static void init(unsigned num_threads);
    static void shutdown();
    static void run_task(const std::function<void()> &task);

private:
    static void worker_thread();

    inline static std::vector<std::thread> threads;
    inline static std::queue<std::function<void()>> tasks;
    inline static std::mutex tasks_mutex;
    inline static std::condition_variable tasks_available;
    inline static bool quit_flag = false;
};

void ThreadManager::init(unsigned num_threads) {
    quit_flag = false;
    threads.reserve(num_threads);
    for (unsigned i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_thread);
    }
}

void ThreadManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex);
        quit_flag = true;
    }
    tasks_available.notify_all();
    for (auto &thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads.clear();
}

void ThreadManager::run_task(const std::function<void()> &task) {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex);
        tasks.push(task);
    }
    tasks_available.notify_one();
}

void ThreadManager::worker_thread() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(tasks_mutex);
            tasks_available.wait(lock, [] { return quit_flag || !tasks.empty(); });

            if (quit_flag && tasks.empty()) {
                return;
            }

            task = std::move(tasks.front());
            tasks.pop();
        }
        task();
    }
}

// JackClient class to handle generic DSPs
class JackClient {
public:
    JackClient(const char *client_name, std::unique_ptr<DSP> dsp);
    ~JackClient();

    DSP *get_dsp() { return dsp.get(); }
    const char *get_name() const { return name.c_str(); }

private:
    static int process(jack_nframes_t nframes, void *arg);
    static void jack_shutdown(void *arg);

    void process_audio(jack_nframes_t nframes);

    jack_client_t *client = nullptr;
    std::vector<jack_port_t *> input_ports;
    std::vector<jack_port_t *> output_ports;
    std::unique_ptr<DSP> dsp;
    std::string name;
};

JackClient::JackClient(const char *client_name, std::unique_ptr<DSP> dsp)
    : dsp(std::move(dsp)), name(client_name) {
    client = jack_client_open(name.c_str(), JackNullOption, nullptr);
    if (!client) {
        throw std::runtime_error("Failed to open JACK client");
    }

    if (jack_set_process_callback (client, process, this) != 0) {
        jack_client_close(client);
        throw std::runtime_error("Failed to set JACK process callback");
    }

    jack_on_shutdown(client, jack_shutdown, this);

    const int num_inputs = this->dsp->get_num_inputs();
    const int num_outputs = this->dsp->get_num_outputs();

    input_ports.reserve(num_inputs);   // Reserve memory upfront
    output_ports.reserve(num_outputs); // Reserve memory upfront

    for (int i = 0; i < num_inputs; ++i) {
        input_ports.push_back(
            jack_port_register(client, ("input" + std::to_string(i)).c_str(),
                               JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0));
    }

    for (int i = 0; i < num_outputs; ++i) {
        output_ports.push_back(
            jack_port_register(client, ("output" + std::to_string(i)).c_str(),
                               JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0));
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

int JackClient::process(jack_nframes_t nframes, void *arg) {
    auto *self = static_cast<JackClient *>(arg);
    self->process_audio(nframes);
    return 0;
}

void JackClient::jack_shutdown(void *arg) {
    auto *self = static_cast<JackClient *>(arg);
    self->client = nullptr;
    std::cerr << "JACK client has been shut down" << std::endl;
}

void JackClient::process_audio(jack_nframes_t nframes) {
    const double sample_rate = jack_get_sample_rate(client);

    // Local buffers to avoid thread_local issues
    std::vector<float *> inputs(input_ports.size());
    std::vector<float *> outputs(output_ports.size());

    for (size_t i = 0; i < input_ports.size(); ++i) {
        inputs[i] =
            static_cast<float *>(jack_port_get_buffer(input_ports[i], nframes));
    }

    for (size_t i = 0; i < output_ports.size(); ++i) {
        outputs[i] =
            static_cast<float *>(jack_port_get_buffer(output_ports[i], nframes));
    }

    dsp->process_audio(nframes, inputs.data(), outputs.data(), sample_rate);
}

// Render GUI for a JackClient
void render_client_gui(JackClient *client) {
    ImGui::Begin(client->get_name());
    ImGui::Text("Simple DSP");
    DSP *dsp = client->get_dsp();
    for (const auto &param : dsp->get_parameter_names()) {
        auto value = dsp->get_parameter(param);
        if (param == "frequency" && std::holds_alternative<float>(value)) {
            float fvalue = std::get<float>(value);
            if (ImGui::SliderFloat(param.c_str(), &fvalue, 20.0f, 20000.0f)) {
                dsp->set_parameter(param, fvalue);
            }
        } else if (param == "amplitude" && std::holds_alternative<float>(value)) {
            float fvalue = std::get<float>(value);
            if (ImGui::SliderFloat(param.c_str(), &fvalue, 0.0f, 1.0f)) {
                dsp->set_parameter(param, fvalue);
            }
        } else if (std::holds_alternative<int>(value)) {
            int ivalue = std::get<int>(value);
            if (ImGui::SliderInt(param.c_str(), &ivalue, 0, 100)) {
                dsp->set_parameter(param, ivalue);
            }
        } else if (std::holds_alternative<std::string>(value)) {
            std::string svalue = std::get<std::string>(value);
            char buffer[128];
            std::strncpy(buffer, svalue.c_str(), sizeof(buffer));
            if (ImGui::InputText(param.c_str(), buffer, sizeof(buffer))) {
                dsp->set_parameter(param, std::string(buffer));
            }
        }
    }
    ImGui::End();
}

// Main functionint main(int, char **) {
    // Register DSP types
int main(int, char **) {
    DSPFactory::instance().register_dsp(
        "SinOsc", []() -> std::unique_ptr<DSP> { return std::make_unique<SinOsc>(); });
    // DSPFactory::instance().register_dsp(
    //     "SquareWave", []() -> std::unique_ptr<DSP> { return std::make_unique<SquareWave>(); });
    // DSPFactory::instance().register_dsp(
    //     "SawWave", []() -> std::unique_ptr<DSP> { return std::make_unique<SawWave>(); });

    // Initialize threading
    ThreadManager::init(std::thread::hardware_concurrency());

    // Vector for generic JackClients
    std::vector<std::unique_ptr<JackClient>> jack_clients;
    std::string selected_dsp_type = "SinOsc"; // Default DSP type

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }

    const char *glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow *window =
        glfwCreateWindow(1280, 720, "Audio Engine", nullptr, nullptr);
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
            auto dsp = DSPFactory::instance().create_dsp(selected_dsp_type);
            // Use PolyphonicDSP to manage multiple voices
            auto poly_dsp = std::make_unique<PolyphonicDSP>(
                [dsp_name = selected_dsp_type]() {
                    return DSPFactory::instance().create_dsp(dsp_name);
                },
                8); // Number of voices
            jack_clients.emplace_back(
                std::make_unique<JackClient>(client_name.c_str(), std::move(poly_dsp)));
        }
        if (ImGui::Button("Remove Last JackClient") && !jack_clients.empty()) {
            jack_clients.pop_back();
        }

        // Dropdown to select DSP type
        const auto &dsp_types = DSPFactory::instance().get_registered_dsps();
        static int current_dsp_type = 0;
        if (ImGui::Combo(
                "DSP Type", &current_dsp_type,
                [](void *data, int idx, const char **out_text) {
                    *out_text = static_cast<std::vector<std::string> *>(data)
                                    ->at(idx)
                                    .c_str();
                    return true;
                },
                (void *)&dsp_types, dsp_types.size())) {
            selected_dsp_type = dsp_types[current_dsp_type];
        }

        // Render GUI for each JackClient
        for (const auto &client : jack_clients) {
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

    // Shutdown threading
    ThreadManager::shutdown();

    return 0;
}

