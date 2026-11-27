"""发一个 ship-designer-fix 版本：改版本号 → 重建 DLL → 打 zip(+sha256) → 提交并推三处 → 发 GitHub Release。

用法:
    python tools/release.py 0.1.1                 # 完整发版
    python tools/release.py --bump patch          # 0.1.0 -> 0.1.1
    python tools/release.py --bump minor          # 0.1.0 -> 0.2.0
    python tools/release.py 0.2.0 --dry-run       # 只打印计划，什么都不改
    python tools/release.py 0.1.1 --no-release    # 只提交/推送，不发 Release
    python tools/release.py 0.1.1 --no-push       # 只改文件/打包，不推

约定（来自 launcher 的 PLUGINS.md / PLUGIN_HANDBOOK.md）:
  * zip 里清单在最外层，旁边是 DLL；Release 里可以有多个 zip，清单的
    update.asset（ship-designer-fix-*.zip）决定装哪个
  * 每次发布 version 必须变；tag 用 v<version>
  * Release 里同时放 <zip>.sha256（十六进制摘要打头）时 launcher 会校验
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_DIR = os.path.dirname(HERE)
WORKSPACE = os.path.dirname(PLUGIN_DIR)
MANIFEST = os.path.join(PLUGIN_DIR, "stl-plugin.json")
DIST = os.path.join(PLUGIN_DIR, "dist")
PREFIX = "ship_designer_fix"           # 工作区里的子目录名（subtree prefix）

GXX_CANDIDATES = [
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "gse-toolchain", "mingw64", "bin", "g++.exe"),
    "g++",
]
MINHOOK = os.path.join(WORKSPACE, "gamesetup_ext", "third_party", "minhook")
SOURCES = ["dllmain.cpp", "plugin.cpp", "anchors.cpp", "log.cpp", "util.cpp", "hook_minhook.cpp"]


def fail(message):
    print("错误: " + message)
    sys.exit(1)


def run(cmd, cwd=None, capture=False):
    print("  $ " + " ".join(cmd) if isinstance(cmd, list) else "  $ " + cmd)
    if capture:
        return subprocess.run(cmd, cwd=cwd, check=True, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout.strip()
    subprocess.run(cmd, cwd=cwd, check=True)
    return ""


def git(*args, capture=False):
    return run(["git"] + list(args), cwd=WORKSPACE, capture=capture)


def read_manifest():
    with open(MANIFEST, encoding="utf-8") as f:
        return json.load(f)


def write_manifest(manifest):
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write("\n")


def bump(version, kind):
    parts = [int(x) for x in version.split(".")]
    while len(parts) < 3:
        parts.append(0)
    if kind == "major":
        parts = [parts[0] + 1, 0, 0]
    elif kind == "minor":
        parts = [parts[0], parts[1] + 1, 0]
    else:
        parts = [parts[0], parts[1], parts[2] + 1]
    return ".".join(str(p) for p in parts)


def find_gxx():
    for candidate in GXX_CANDIDATES:
        if os.path.isabs(candidate):
            if os.path.exists(candidate):
                return candidate
        elif shutil.which(candidate):
            return candidate
    fail("找不到 g++（试过 gse-toolchain 和 PATH）")


def build():
    gxx = find_gxx()
    objdir = os.path.join(PLUGIN_DIR, "build-mingw", "objhook")
    os.makedirs(objdir, exist_ok=True)
    if not any(f.endswith(".o") for f in os.listdir(objdir)):
        print("编译 MinHook（一次性）…")
        for root, _dirs, files in os.walk(os.path.join(MINHOOK, "src")):
            for name in files:
                if not name.endswith(".c"):
                    continue
                src = os.path.join(root, name)
                obj = os.path.join(objdir, os.path.splitext(name)[0] + ".o")
                run([gxx, "-O2", "-I" + os.path.join(MINHOOK, "include"),
                     "-I" + os.path.join(MINHOOK, "src"), "-c", src, "-o", obj])
    objects = [os.path.join(objdir, f) for f in os.listdir(objdir) if f.endswith(".o")]
    dll = os.path.join(PLUGIN_DIR, read_manifest()["dll"])
    common = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX",
              "-DUNICODE", "-D_UNICODE", "-D_CRT_SECURE_NO_WARNINGS", "-DGSE_WITH_MINHOOK",
              "-Isrc", "-I" + os.path.join(MINHOOK, "include")]
    run([gxx] + common + ["-shared", "-static", "-static-libgcc", "-static-libstdc++"] +
        [os.path.join("src", s) for s in SOURCES] + objects +
        ["-o", dll, "-lole32", "-luuid"], cwd=PLUGIN_DIR)
    return dll


def package(manifest, dll):
    version = manifest["version"]
    os.makedirs(DIST, exist_ok=True)
    name = f"{manifest['id']}-{version}.zip"
    path = os.path.join(DIST, name)
    entries = [(MANIFEST, "stl-plugin.json"), (dll, manifest["dll"])]
    defaults = os.path.join(PLUGIN_DIR, "defaults")
    if os.path.isdir(defaults):
        for f in sorted(os.listdir(defaults)):
            entries.append((os.path.join(defaults, f), "defaults/" + f))
    readme = os.path.join(PLUGIN_DIR, "README.md")
    if os.path.exists(readme):
        entries.append((readme, "README.md"))
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for src, arc in entries:
            z.write(src, arc)
    digest = hashlib.sha256(open(path, "rb").read()).hexdigest()
    sha_path = path + ".sha256"
    with open(sha_path, "w", encoding="ascii", newline="\n") as f:
        f.write(digest + "\n")
    print(f"  包: dist/{name}  ({os.path.getsize(path)} bytes)")
    print(f"  sha256: {digest}")
    return path, sha_path, name


def github_token():
    out = subprocess.run(["git", "credential", "fill"], cwd=WORKSPACE, text=True,
                         input="protocol=https\nhost=github.com\n\n",
                         stdout=subprocess.PIPE, check=True).stdout
    m = re.search(r"^password=(.+)$", out, re.M)
    if not m:
        fail("取不到 github 凭据（git credential fill）")
    return m.group(1).strip()


def api(method, url, token, data=None, ctype="application/json"):
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("Accept", "application/vnd.github+json")
    if data is not None:
        req.add_header("Content-Type", ctype)
    try:
        with urllib.request.urlopen(req) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        fail(f"{method} {url} -> HTTP {e.code}: " + e.read().decode("utf-8", "replace")[:300])


def release(manifest, zip_path, sha_path, name):
    repo = manifest.get("update", {}).get("github")
    if not repo:
        fail("清单里没有 update.github")
    tag = "v" + manifest["version"]
    token = github_token()

    existing = api("GET", f"https://api.github.com/repos/{repo}/releases", token)
    rel = next((r for r in existing if r["tag_name"] == tag), None)
    if rel is None:
        body = json.dumps({
            "tag_name": tag,
            "name": f"{manifest['version']} - {manifest.get('name', repo)}",
            "body": f"见 README（本版 {manifest['version']}）。\n\n安装: `stl plugin install <zip>` 或 Plugins 页。\n",
            "draft": False,
            "prerelease": False,
        }).encode("utf-8")
        rel = api("POST", f"https://api.github.com/repos/{repo}/releases", token, body)
        print(f"  创建 release {tag} (id {rel['id']})")
    else:
        print(f"  复用已有 release {tag} (id {rel['id']})")

    have = {a["name"] for a in rel.get("assets", [])}
    for path, asset, ctype in ((zip_path, name, "application/zip"),
                               (sha_path, name + ".sha256", "text/plain")):
        if asset in have:
            print(f"  资产已存在，跳过: {asset}")
            continue
        data = open(path, "rb").read()
        url = f"https://uploads.github.com/repos/{repo}/releases/{rel['id']}/assets?name={asset}"
        req = urllib.request.Request(url, data=data, method="POST")
        req.add_header("Authorization", "Bearer " + token)
        req.add_header("Content-Type", ctype)
        req.add_header("Accept", "application/vnd.github+json")
        try:
            with urllib.request.urlopen(req) as r:
                info = json.load(r)
                print(f"  上传 {info['name']} ({info['size']} bytes)")
        except urllib.error.HTTPError as e:
            fail(f"上传 {asset} 失败: HTTP {e.code} " + e.read().decode("utf-8", "replace")[:300])
    print(f"  https://github.com/{repo}/releases/tag/{tag}")


def main():
    parser = argparse.ArgumentParser(description="发一个 ship-designer-fix 版本")
    parser.add_argument("version", nargs="?", help="新版本号，例如 0.1.1")
    parser.add_argument("--bump", choices=["patch", "minor", "major"],
                        help="按当前版本号自增（给了 version 就不用这个）")
    parser.add_argument("--dry-run", action="store_true", help="只打印计划，什么都不改")
    parser.add_argument("--skip-build", action="store_true", help="不重建 DLL")
    parser.add_argument("--no-push", action="store_true", help="不提交/不推送")
    parser.add_argument("--no-release", action="store_true", help="不发 GitHub Release")
    parser.add_argument("--note", default="", help="提交信息里附加一句说明")
    args = parser.parse_args()

    manifest = read_manifest()
    current = manifest["version"]
    if args.version:
        new = args.version
    elif args.bump:
        new = bump(current, args.bump)
    else:
        fail("给个版本号（0.1.1）或者 --bump patch|minor|major")

    if not re.fullmatch(r"\d+\.\d+\.\d+", new):
        fail(f"版本号要 x.y.z 形式，收到 {new}")
    if new == current:
        fail(f"版本号没变（还是 {current}）—— 每次发布必须变")

    print(f"发版: {current} -> {new}")
    if args.dry_run:
        print("  [dry-run] 不会改文件、不构建、不推送、不发 Release")
        print(f"  计划: 改 stl-plugin.json 版本 → 重建 DLL → dist/{manifest['id']}-{new}.zip(+.sha256)"
              f" → 提交并推 origin/{(['ship', 'gh'] if not args.no_push else [])}"
              f" → GitHub Release v{new}")
        return

    if not args.no_push:
        dirty = git("status", "--porcelain", capture=True)
        if dirty:
            fail("工作区有未提交的改动，先提交干净再发版：\n" + dirty)

    manifest["version"] = new
    write_manifest(manifest)
    print("  已写 stl-plugin.json")

    dll = os.path.join(PLUGIN_DIR, manifest["dll"])
    if args.skip_build:
        if not os.path.exists(dll):
            fail("--skip-build 但 DLL 不存在")
    else:
        print("  重建 DLL…")
        dll = build()
    zip_path, sha_path, name = package(manifest, dll)

    if not args.no_push:
        print("  提交并推送…")
        git("add", "-A", PREFIX)
        message = f"{PREFIX} {new}: release"
        if args.note:
            message += "\n\n" + args.note
        git("commit", "-m", message, "-m", f"ship-designer-fix {new}")
        git("push", "origin", "main")
        git("subtree", "split", "--prefix=" + PREFIX, "-b", "ship-designer-fix")
        git("push", "ship", "ship-designer-fix:main")
        git("push", "gh", "ship-designer-fix:main")

    if not args.no_release:
        print("  发 GitHub Release…")
        release(manifest, zip_path, sha_path, name)

    print("完成。")


main()
