import action from "@actions/core";
import cache from "@actions/cache";
import io from "@actions/io";
import { request } from "@octokit/request";

import path from "path";
import { createWriteStream, chmodSync } from "fs";
import { execFileSync } from "child_process";
import { once } from "events";

async function download_installer(path) {
    try {
        action.info("Downloading installer...");

        const release = await request("GET /repos/{owner}/{repo}/releases/latest", {
            owner: "arsenez2006",
            repo: "depend",
            headers: {
                "X-GitHub-Api-Version": "2022-11-28"
            }
        });
        if (release.status !== 200) {
            throw "Failed to get latest release";
        }

        const assets = await request("GET /repos/{owner}/{repo}/releases/{release_id}/assets", {
            owner: "arsenez2006",
            repo: "depend",
            release_id: release.data.id,
            headers: {
                "X-GitHub-Api-Version": "2022-11-28"
            }
        });
        if (assets.status !== 200) {
            throw "Failed to get release assets list";
        }

        const asset_depend = assets.data.find(el => el.name === "depend");
        if (asset_depend === null) {
            throw "Failed to get asset \"depend\"";
        }

        const download_url = asset_depend.browser_download_url;
        const response = await fetch(download_url);
        if (!response.ok) {
            throw "Cannot download installer";
        }
        const file = createWriteStream(path);
        const file_stream = new WritableStream({
            write(chunk) {
                file.write(chunk);
            }
        });
        await response.body.pipeTo(file_stream);

        chmodSync(file.path, "755");
        file.end();
        await once(file, "finish");

        action.info(`Installer downloaded to ${file.path}`);
        return true;
    } catch (error) {
        action.error(error);
        return false;
    }
}

async function install_component(installer, working_directory, use_cache, comp) {
    action.info(`Installing ${comp}...`);
    const comp_path = path.join(working_directory, comp);
    const comp_bin = path.join(comp_path, "bin");
    io.mkdirP(comp_path);
    action.debug(`${comp} path is ${comp_path}`);

    if (use_cache && cache.isFeatureAvailable()) {
        const version = execFileSync(installer, [`--prefix=${comp_path}`, `--install=${comp}`, `--dry-run`]).filter(el => 33 <= el && el <= 126).toString();
        action.debug(`${comp} latest version is ${version}`);
        const key = `${comp}-${version}`;
        const cache_id = await cache.restoreCache([comp_bin], key);
        if (cache_id !== undefined) {
            action.info("Restored from cache");
        } else {
            execFileSync(installer, [`--prefix=${comp_path}`, `--install=${comp}`], {
                stdio: "inherit"
            });

            action.debug(`Saving cache for ${comp}`);
            await cache.saveCache([comp_bin], key);
        }
    } else {
        execFileSync(installer, [`--prefix=${comp_path}`, `--install=${comp}`], {
            stdio: "inherit"
        });
    }

    action.addPath(comp_bin);
}

async function run() {
    const working_directory = path.join(process.cwd(), "deps");
    await io.mkdirP(working_directory);
    action.debug(`working_directory is ${working_directory}`);

    const installer = path.join(working_directory, "depend");
    action.debug(`installer path is ${installer}`);

    if (!await download_installer(installer)) {
        action.setFailed("Failed to download installer");
    }

    const use_cache = action.getBooleanInput("cache");
    const components = [];
    action.getBooleanInput("nasm") && components.push("nasm");

    for (let i = 0; i < components.length; i++) {
        await install_component(installer, working_directory, cache, components[i]);
    }
}

await run();
process.exit();
