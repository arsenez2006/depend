#include <cstddef>
#include <cstring>
#include <filesystem>
#include <git2.h>
#include <regex>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

struct remote_ref {
  std::string name;
  git_oid     oid;

  friend bool operator<(remote_ref const& lhs, remote_ref const& rhs) {
    std::vector< size_t > lhs_ver, rhs_ver;
    lhs_ver.reserve(8);
    rhs_ver.reserve(8);

    size_t ver_curr = 0;
    for (char ch : lhs.name) {
      if (ch == '_') {
        lhs_ver.push_back(ver_curr);
        ver_curr = 0;
      } else {
        ver_curr = ver_curr * 10 + ch - '0';
      }
    }
    lhs_ver.push_back(ver_curr);

    ver_curr = 0;
    for (char ch : rhs.name) {
      if (ch == '_') {
        rhs_ver.push_back(ver_curr);
        ver_curr = 0;
      } else {
        ver_curr = ver_curr * 10 + ch - '0';
      }
    }
    rhs_ver.push_back(ver_curr);

    return lhs_ver < rhs_ver;
  }
};

bool install_doxygen(std::filesystem::path prefix, bool dry_run = false) {
  char const*           remote_url = "https://github.com/doxygen/doxygen.git";
  std::error_code       ec;

  std::filesystem::path bin                = prefix / "bin";
  std::filesystem::path src                = prefix / "src";

  git_repository*       repo               = nullptr;
  git_remote*           remote             = nullptr;
  git_commit*           tag_commit         = nullptr;

  git_repository_init_options repo_options = GIT_REPOSITORY_INIT_OPTIONS_INIT;

  git_remote_head const**     remote_refs;
  size_t                      remote_refs_count;

  std::regex tag_regex("^refs/tags/Release_[0-9]+_[0-9]+_[0-9]+\\^\\{\\}");
  std::regex tag_prefix_regex("^refs/tags/Release_");
  std::regex tag_suffix_regex("\\^\\{\\}");

  std::vector< remote_ref > remote_tags;
  pid_t                     child;

  if (dry_run) {
    // Create remote
    if (git_remote_create_detached(&remote, remote_url) != 0) {
      printf(
          "%d, git_remote_create_detached: %s\n",
          __LINE__,
          git_error_last()->message
      );
      return false;
    }
  } else {
    // Create direcotires
    std::filesystem::create_directories(bin, ec);
    std::filesystem::create_directories(src, ec);
    if (ec) {
      printf("%d, create_directories: %s\n", __LINE__, ec.message().c_str());
      return false;
    }

    // Initialize empty repo
    repo_options.origin_url = remote_url;
    if (git_repository_init_ext(&repo, src.c_str(), &repo_options) != 0) {
      printf(
          "%d, git_repository_init_ext: %s\n",
          __LINE__,
          git_error_last()->message
      );
      return false;
    }

    // Get remote
    if (git_remote_lookup(&remote, repo, "origin") != 0) {
      printf("%s\n", git_error_last()->message);
      printf(
          "%d, git_remote_lookup: %s\n", __LINE__, git_error_last()->message
      );
      return false;
    }
  }

  // Connect to remote
  if (git_remote_connect(
          remote, GIT_DIRECTION_FETCH, nullptr, nullptr, nullptr
      ) != 0) {
    printf("%d, git_remote_connect: %s\n", __LINE__, git_error_last()->message);
    git_remote_free(remote);
    if (!dry_run) {
      git_repository_free(repo);
    }
    return false;
  }

  // Get remote refs
  if (git_remote_ls(&remote_refs, &remote_refs_count, remote) != 0) {
    printf("%d, git_remote_ls: %s\n", __LINE__, git_error_last()->message);
    git_remote_free(remote);
    if (!dry_run) {
      git_repository_free(repo);
    }
    return false;
  }

  // Save remote tags
  remote_tags.reserve(remote_refs_count);
  for (size_t i = 0; i < remote_refs_count; ++i) {
    if (std::regex_match(remote_refs[i]->name, tag_regex)) {
      remote_ref tmp;
      tmp.name = remote_refs[i]->name;
      if (git_oid_cpy(&tmp.oid, &remote_refs[i]->oid) != 0) {
        printf("%d, git_oid_cpy: %s\n", __LINE__, git_error_last()->message);
        git_remote_free(remote);
        if (!dry_run) {
          git_repository_free(repo);
        }
        return false;
      }

      // remove prefix and suffix
      tmp.name = std::regex_replace(tmp.name, tag_prefix_regex, "");
      tmp.name = std::regex_replace(tmp.name, tag_suffix_regex, "");

      remote_tags.emplace_back(std::move(tmp));
    }
  }
  remote_tags.shrink_to_fit();

  // Sort by version
  std::sort(remote_tags.begin(), remote_tags.end());

  // Print last version
  printf("%s\n", remote_tags.back().name.c_str());

  if (!dry_run) {
    // Fetch repo
    if (git_remote_fetch(remote, nullptr, nullptr, nullptr) != 0) {
      printf("%d, git_remote_fetch: %s\n", __LINE__, git_error_last()->message);
      git_remote_free(remote);
      git_repository_free(repo);
      return false;
    }

    // Get commit
    if (git_commit_lookup(&tag_commit, repo, &remote_tags.back().oid) != 0) {
      printf(
          "%d, git_commit_lookup: %s\n", __LINE__, git_error_last()->message
      );
      git_remote_free(remote);
      git_repository_free(repo);
      return false;
    }

    // Set HEAD
    if (git_repository_set_head_detached(repo, &remote_tags.back().oid) != 0) {
      printf(
          "%d, git_repository_set_head_detached: %s\n",
          __LINE__,
          git_error_last()->message
      );
      git_remote_free(remote);
      git_repository_free(repo);
      return false;
    }

    // Reset repo
    if (git_reset(
            repo,
            reinterpret_cast< git_object* >(tag_commit),
            GIT_RESET_HARD,
            nullptr
        ) != 0) {
      printf("%d, git_reset: %s\n", __LINE__, git_error_last()->message);
      git_remote_free(remote);
      git_repository_free(repo);
      return false;
    }

    std::filesystem::path build_path = src / "build";
    // Run CMake
    switch (child = fork()) {
    case 0: {
      std::filesystem::current_path(src);

      std::filesystem::create_directory(build_path);

      if (execlp(
              "cmake",
              "cmake",
              src.c_str(),
              "-B",
              build_path.c_str(),
              ("-DCMAKE_INSTALL_PREFIX=" + prefix.string()).c_str(),
              "-DCMAKE_BUILD_TYPE=Release",
              nullptr
          ) == -1) {
        printf("%d, execlp: %s\n", __LINE__, strerror(errno));
      }

      _exit(EXIT_FAILURE);
    } break;
    case -1: {
      printf("%d, fork: %s\n", __LINE__, strerror(errno));
      return false;
    } break;
    default: {
      int status;
      do { waitpid(child, &status, 0); } while (WIFEXITED(status) == 0);

      if (WEXITSTATUS(status) != EXIT_SUCCESS) {
        printf("cmake failed\n");
        return false;
      }
    } break;
    }

    // Run make
    switch (child = fork()) {
    case 0: {
      std::filesystem::current_path(build_path);

      if (execlp(
              "make",
              "make",
              "-j",
              std::to_string(std::thread::hardware_concurrency()).c_str(),
              nullptr
          ) == -1) {
        printf("%d, execlp: %s\n", __LINE__, strerror(errno));
      }

      _exit(EXIT_FAILURE);
    } break;
    case -1: {
      printf("%d, fork: %s\n", __LINE__, strerror(errno));
      return false;
    } break;
    default: {
      int status;
      do { waitpid(child, &status, 0); } while (WIFEXITED(status) == 0);

      if (WEXITSTATUS(status) != EXIT_SUCCESS) {
        printf("make failed\n");
        return false;
      }
    } break;
    }

    // Run make install
    switch (child = fork()) {
    case 0: {
      std::filesystem::current_path(build_path);

      if (execlp("make", "make", "install", nullptr) == -1) {
        printf("%d, execlp: %s\n", __LINE__, strerror(errno));
      }

      _exit(EXIT_FAILURE);
    } break;
    case -1: {
      printf("%d, fork: %s\n", __LINE__, strerror(errno));
      return false;
    } break;
    default: {
      int status;
      do { waitpid(child, &status, 0); } while (WIFEXITED(status) == 0);

      if (WEXITSTATUS(status) != EXIT_SUCCESS) {
        printf("make install failed\n");
        return false;
      }
    } break;
    }
  }
  return true;
}
