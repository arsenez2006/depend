#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <getopt.h>
#include <git2.h>
#include <iostream>
#include <regex>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

struct remote_ref {
    std::string name;
    git_oid oid;

    friend bool operator<(const remote_ref& lhs, const remote_ref& rhs) {
        size_t lhs_major = 0, rhs_major = 0;
        size_t lhs_minor = 0, rhs_minor = 0;
        size_t lhs_patch = 0, rhs_patch = 0;
        size_t lhs_rc = 0, rhs_rc = 0;
        size_t lhs_summary = 0, rhs_summary = 0;

        // Parse lhs
        bool major = false;
        bool minor = false;
        bool patch_present = false;
        bool patch = false;
        bool rc_present = false;
        for (const auto& ch : lhs.name) {
            if (!major) {
                if (ch == '.') {
                    major = true;
                } else {
                    lhs_major = lhs_major * 10 + ch - '0';
                }
            } else if (!minor) {
                if (ch == '.') {
                    minor = true;
                    patch_present = true;
                } else if (ch == 'r') {
                    minor = true;
                    rc_present = true;
                } else {
                    lhs_minor = lhs_minor * 10 + ch - '0';
                }
            } else if (patch_present && !patch) {
                if (ch == 'r') {
                    patch = true;
                    rc_present = true;
                } else {
                    lhs_patch = lhs_patch * 10 + ch - '0';
                }
            } else if (rc_present) {
                if (ch != 'c') {
                    lhs_rc = lhs_rc * 10 + ch - '0';
                }
            }
        }

        // Parse rhs
        major = false;
        minor = false;
        patch_present = false;
        patch = false;
        rc_present = false;
        for (const auto& ch : rhs.name) {
            if (!major) {
                if (ch == '.') {
                    major = true;
                } else {
                    rhs_major = rhs_major * 10 + ch - '0';
                }
            } else if (!minor) {
                if (ch == '.') {
                    minor = true;
                    patch_present = true;
                } else if (ch == 'r') {
                    minor = true;
                    rc_present = true;
                } else {
                    rhs_minor = rhs_minor * 10 + ch - '0';
                }
            } else if (patch_present && !patch) {
                if (ch == 'r') {
                    patch = true;
                    rc_present = true;
                } else {
                    rhs_patch = rhs_patch * 10 + ch - '0';
                }
            } else if (rc_present) {
                if (ch != 'c') {
                    rhs_rc = rhs_rc * 10 + ch - '0';
                }
            }
        }

        lhs_summary = ((lhs_major & 0xFF) << 24) | ((lhs_minor & 0xFF) << 16) |
                      ((lhs_patch & 0xFF) << 8) | ((lhs_rc & 0xFF) << 0);
        rhs_summary = ((rhs_major & 0xFF) << 24) | ((rhs_minor & 0xFF) << 16) |
                      ((rhs_patch & 0xFF) << 8) | ((rhs_rc & 0xFF) << 0);

        return lhs_summary < rhs_summary;
    }
};

bool
install_nasm(std::filesystem::path prefix, bool dry_run = false) {
    const char* const remote_url =
      "https://github.com/netwide-assembler/nasm.git";
    std::error_code ec;

    std::filesystem::path bin = prefix / "bin";
    std::filesystem::path src = prefix / "src";

    git_repository* repo = nullptr;
    git_remote* remote = nullptr;
    git_commit* tag_commit = nullptr;

    git_repository_init_options repo_options = GIT_REPOSITORY_INIT_OPTIONS_INIT;

    const git_remote_head** remote_refs;
    size_t remote_refs_count;

    std::regex tag_regex("^refs/tags/nasm-.*\\^\\{\\}");
    std::regex tag_prefix_regex("^refs/tags/nasm-");
    std::regex tag_suffix_regex("\\^\\{\\}");
    std::vector<remote_ref> remote_tags;
    pid_t child;

    if (dry_run) {
        // Create remote
        if (git_remote_create_detached(&remote, remote_url) != 0) {
            printf("%d, git_remote_create_detached: %s\n",
                   __LINE__,
                   git_error_last()->message);
            return false;
        }
    } else {
        // Create direcotires
        std::filesystem::create_directories(bin, ec);
        std::filesystem::create_directories(src, ec);
        if (ec) {
            printf(
              "%d, create_directories: %s\n", __LINE__, ec.message().c_str());
            return false;
        }

        // Initialize empty repo
        repo_options.origin_url = remote_url;
        if (git_repository_init_ext(&repo, src.c_str(), &repo_options) != 0) {
            printf("%d, git_repository_init_ext: %s\n",
                   __LINE__,
                   git_error_last()->message);
            return false;
        }

        // Get remote
        if (git_remote_lookup(&remote, repo, "origin") != 0) {
            printf("%s\n", git_error_last()->message);
            printf("%d, git_remote_lookup: %s\n",
                   __LINE__,
                   git_error_last()->message);
            return false;
        }
    }

    // Connect to remote
    if (git_remote_connect(
          remote, GIT_DIRECTION_FETCH, nullptr, nullptr, nullptr) != 0) {
        printf(
          "%d, git_remote_connect: %s\n", __LINE__, git_error_last()->message);
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
                printf(
                  "%d, git_oid_cpy: %s\n", __LINE__, git_error_last()->message);
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
            printf("%d, git_remote_fetch: %s\n",
                   __LINE__,
                   git_error_last()->message);
            git_remote_free(remote);
            git_repository_free(repo);
            return false;
        }

        // Get commit
        if (git_commit_lookup(&tag_commit, repo, &remote_tags.back().oid) !=
            0) {
            printf("%d, git_commit_lookup: %s\n",
                   __LINE__,
                   git_error_last()->message);
            git_remote_free(remote);
            git_repository_free(repo);
            return false;
        }

        // Set HEAD
        if (git_repository_set_head_detached(repo, &remote_tags.back().oid) !=
            0) {
            printf("%d, git_repository_set_head_detached: %s\n",
                   __LINE__,
                   git_error_last()->message);
            git_remote_free(remote);
            git_repository_free(repo);
            return false;
        }

        // Reset repo
        if (git_reset(repo,
                      reinterpret_cast<git_object*>(tag_commit),
                      GIT_RESET_HARD,
                      nullptr) != 0) {
            printf("%d, git_reset: %s\n", __LINE__, git_error_last()->message);
            git_remote_free(remote);
            git_repository_free(repo);
            return false;
        }

        // Run autogen.sh
        switch (child = fork()) {
            case 0: {
                std::filesystem::current_path(src);

                if (execlp((src / "autogen.sh").c_str(),
                           (src / "autogen.sh").c_str(),
                           nullptr) == -1) {
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
                do {
                    waitpid(child, &status, 0);
                } while (WIFEXITED(status) == 0);

                if (WEXITSTATUS(status) != EXIT_SUCCESS) {
                    printf("autogen.sh failed\n");
                    return false;
                }
            } break;
        }

        // Run configure
        switch (child = fork()) {
            case 0: {
                std::filesystem::current_path(src);

                if (execlp((src / "configure").c_str(),
                           (src / "configure").c_str(),
                           ("--prefix=" + bin.string()).c_str(),
                           nullptr) == -1) {
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
                do {
                    waitpid(child, &status, 0);
                } while (WIFEXITED(status) == 0);

                if (WEXITSTATUS(status) != EXIT_SUCCESS) {
                    printf("configure failed\n");
                    return false;
                }
            } break;
        }

        // Run make
        switch (child = fork()) {
            case 0: {
                std::filesystem::current_path(src);

                if (execlp("make",
                           "make",
                           "-j",
                           std::to_string(std::thread::hardware_concurrency())
                             .c_str(),
                           nullptr) == -1) {
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
                do {
                    waitpid(child, &status, 0);
                } while (WIFEXITED(status) == 0);

                if (WEXITSTATUS(status) != EXIT_SUCCESS) {
                    printf("make failed\n");
                    return false;
                }
            } break;
        }

        // Copy executables
        std::filesystem::copy_file(src / "nasm", bin / "nasm", ec);
        std::filesystem::copy_file(src / "ndisasm", bin / "ndisasm", ec);
        if (ec) {
            printf(
              "%d, create_directories: %s\n", __LINE__, ec.message().c_str());
            return false;
        }
    }

    git_remote_free(remote);
    if (!dry_run) {
        git_repository_free(repo);
    }
    return true;
}

int
main(int argc, char** argv) {
    std::string install;
    bool dry_run = false;
    std::filesystem::path prefix;

    const char* short_opts = "i:p:v";
    option long_opts[] = { { "install", required_argument, nullptr, 'i' },
                           { "prefix", required_argument, nullptr, 'p' },
                           { "dry-run", optional_argument, nullptr, 'd' } };
    int ret;
    while ((ret = getopt_long(argc, argv, short_opts, long_opts, nullptr)) !=
           -1) {
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
    }

    git_libgit2_shutdown();

    return EXIT_SUCCESS;
}