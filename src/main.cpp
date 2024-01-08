#include "comps.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <getopt.h>
#include <git2.h>
#include <string>
#include <unistd.h>

int main(int argc, char** argv) {
  std::string           install;
  bool                  dry_run = false;
  std::filesystem::path prefix;

  char const*           short_opts  = "i:p:v";
  option                long_opts[] = {
    {"install", required_argument, nullptr, 'i'},
    { "prefix", required_argument, nullptr, 'p'},
    {"dry-run", optional_argument, nullptr, 'd'}
  };
  int ret;
  while ((ret = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1
  ) {
    switch (ret) {
    case 'i': {
      install = optarg;
    } break;

    case 'p': {
      prefix = std::filesystem::absolute(optarg);
    } break;

    case 'd': {
      dry_run = true;
    } break;
    }
  }

  git_libgit2_init();

  if (install == "nasm") {
    if (!install_nasm(prefix, dry_run)) {
      printf("Failed to install NASM\n");
      return EXIT_FAILURE;
    }
  } else if (install == "doxygen") {
    if (!install_doxygen(prefix, dry_run)) {
      printf("Failed to install Doxygen\n");
      return EXIT_FAILURE;
    }
  }

  git_libgit2_shutdown();

  return EXIT_SUCCESS;
}
