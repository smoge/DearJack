#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <jack/jack.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

// Constants
constexpr double TWO_PI = 2.0 * M_PI;
constexpr float DEFAULT_FREQUENCY = 440.0f; // Default frequency in Hz

// GLFW error callback function
static void glfw_error_callback(int error, const char *description) {
  std::fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

// DSP base class
class DSP {
public:
  virtual ~DSP() = default;
  virtual void process_audio(jack_nframes_t nframes, float *out,
                             double sample_rate) = 0;
};

// SinOsc DSP implementation
class SinOsc : public DSP {
public:
  SinOsc() : phase(0.0), frequency(DEFAULT_FREQUENCY) {}

  void set_frequency(double freq) { frequency.store(freq); }

  void process_audio(jack_nframes_t nframes, float *out,
                     double sample_rate) override {
    double phase_increment = TWO_PI * frequency.load() / sample_rate;
    for (jack_nframes_t i = 0; i < nframes; ++i) {
      out[i] = std::sin(phase);
      phase += phase_increment;
      if (phase >= TWO_PI) {
        phase -= TWO_PI;
      }
    }
  }

private:
  double phase;
  std::atomic<double> frequency;
};

class JackClient {
public:
  JackClient(const char *client_name, std::unique_ptr<DSP> dsp);
  ~JackClient();

  DSP *get_dsp() const { return dsp.get(); }
  const char *get_name() const { return name.c_str(); }
  float &get_frequency() { return frequency; }

private:
  static int process(jack_nframes_t nframes, void *arg);
  static void jack_shutdown(void *arg);

  void process_audio(jack_nframes_t nframes);

  jack_client_t *client = nullptr;
  jack_port_t *output_port = nullptr;
  std::unique_ptr<DSP> dsp;
  std::string name;
  float frequency; // Instance variable for frequency
};

JackClient::JackClient(const char *client_name, std::unique_ptr<DSP> dsp)
    : dsp(std::move(dsp)), name(client_name), frequency(DEFAULT_FREQUENCY) {
  client = jack_client_open(name.c_str(), JackNullOption, nullptr);
  if (!client) {
    throw std::runtime_error("Failed to open JACK client");
  }

  if (jack_set_process_callback(client, process, this) != 0) {
    jack_client_close(client);
    throw std::runtime_error("Failed to set JACK process callback");
  }

  jack_on_shutdown(client, jack_shutdown, this);

  output_port = jack_port_register(client, "output", JACK_DEFAULT_AUDIO_TYPE,
                                   JackPortIsOutput, 0);
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

int JackClient::process(jack_nframes_t nframes, void *arg) {
  auto *self = static_cast<JackClient *>(arg);
  self->process_audio(nframes);
  return 0;
}

void JackClient::jack_shutdown(void *arg) { /* Add necessary cleanup here */
}

void JackClient::process_audio(jack_nframes_t nframes) {
  double sample_rate = jack_get_sample_rate(client);
  auto *out = static_cast<jack_default_audio_sample_t *>(
      jack_port_get_buffer(output_port, nframes));
  dsp->process_audio(nframes, out, sample_rate);
}

void render_client_gui(JackClient *client) {
  ImGui::Begin(client->get_name());
  ImGui::Text("Simple SinOsc");
  if (ImGui::SliderFloat("Frequency", &client->get_frequency(), 20.0f,
                         2000.0f)) {
    if (auto sin_osc = dynamic_cast<SinOsc *>(client->get_dsp())) {
      sin_osc->set_frequency(static_cast<double>(client->get_frequency()));
    }
  }
  ImGui::End();
}

int main(int, char **) {
  std::unique_ptr<JackClient> jack_client1;
  std::unique_ptr<JackClient> jack_client2;

  try {
    jack_client1 =
        std::make_unique<JackClient>("DearJack1", std::make_unique<SinOsc>());
    jack_client2 =
        std::make_unique<JackClient>("DearJack2", std::make_unique<SinOsc>());
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

  GLFWwindow *window =
      glfwCreateWindow(1280, 720, "Prototype", nullptr, nullptr);
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

    render_client_gui(jack_client1.get());
    render_client_gui(jack_client2.get());

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
