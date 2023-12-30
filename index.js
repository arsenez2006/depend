const path = require("path");
const action = require("@actions/core");
const cache = require("@actions/cache");
const io = require("@actions/io")
const { request } = require("@octokit/request");

async function download_installer() {
    const release = await request("GET /repos/{owner}/{repo}/releases/latest", {
        owner: "arsenez2006",
        repo: "depend",
        headers: {
            "X-GitHub-Api-Version": "2022-11-28"
        }
    });
}

async function run() {
    const working_directory = path.join(process.cwd(), "deps");
    await io.mkdirP(working_directory);

    await download_installer();
}

run();
