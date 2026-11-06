#include "pixcake/renderd/render_service.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--health") {
    pixcake::renderd::RenderService service;
    std::cout << "renderd boundary ready; pending=" << (service.has_pending() ? "1" : "0") << '\n';
    return 0;
  }
  std::cerr << "pixcake_app_renderd is a process boundary. Use --health for a self-check.\n";
  return argc == 1 ? 0 : 2;
}
