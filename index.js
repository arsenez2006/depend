const action = require("@actions/core");
const cache = require("@actions/cache");
const io = require("@actions/io");
const { request } = require("@octokit/request");

const path = require("path")
const { createWriteStream, chmodSync } = require("fs");
const { error } = require("console");

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
        if (release.status != 200) {
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
        if (assets.status != 200) {
            throw "Failed to get release assets list";
        }

        const asset_depend = assets.data.find(el => el.name == "depend");
        if (asset_depend == null) {
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

        action.info(`Installer downloaded to ${file.path}`);
        return true;
    } catch (error) {
        action.error(error);
        return false;
    }
}

async function run() {
    const working_directory = path.join(process.cwd(), "deps");
    await io.mkdirP(working_directory);
    const installer = path.join(working_directory, "depend");

    if (!await download_installer(installer)) {
        action.setFailed("Failed to download installer");
    }
}

run();
