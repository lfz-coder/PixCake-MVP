#include "pixcake/exportd/export_service.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--health") {
    pixcake::exportd::ExportService service;
    std::cout << "exportd boundary ready; pending=" << service.pending_count() << '\n';
    return 0;
  }
  std::cerr << "pixcake_app_exportd is a process boundary. Use --health for a self-check.\n";
  return argc == 1 ? 0 : 2;
}
