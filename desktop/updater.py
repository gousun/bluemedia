#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BlueMedia 自更新：通过 GitHub Releases 检查/下载新版本并自替换重启。

流程：
  1. check_update(repo)  → 读 GitHub Releases latest，比较版本号
  2. apply_update(url)   → 下载 zip 到临时目录 → 解压 → 写 updater.bat
                           （等待本进程退出 → 覆盖安装目录 → 重启 BlueMedia.exe）
  3. 启动 bat 后本进程 os._exit(0)

源码模式（非 PyInstaller 打包）不支持自替换，apply_update 会改为执行 git pull。
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

import media_core

UA = "BlueMedia-Updater"
UPD_DIR = os.path.join(tempfile.gettempdir(), "bluemedia_update")


def _http_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read().decode("utf-8"))


def _version_tuple(v):
    v = v.strip().lstrip("vV")
    out = []
    for part in v.split("."):
        out.append(int(part) if part.isdigit() else 0)
    return tuple(out)


def latest_release(repo):
    """返回 {"tag":..., "asset":下载URL, "notes":...} 或 {"error":...}"""
    if not repo or repo.startswith("todo-"):
        return {"error": "未配置更新仓库（media_core.UPDATE_REPO）"}
    try:
        rel = _http_json(f"https://api.github.com/repos/{repo}/releases/latest")
    except Exception as e:
        return {"error": f"{type(e).__name__}: {e}"}
    asset_url = None
    for a in rel.get("assets", []):
        if str(a.get("name", "")).lower().endswith(".zip"):
            asset_url = a.get("browser_download_url")
            break
    return {"tag": rel.get("tag_name", "?"), "asset": asset_url,
            "notes": rel.get("body", "") or ""}


def check_update(repo):
    """比较版本：返回 {"current","latest","available","asset","notes"} 或 {"error"}"""
    info = latest_release(repo)
    if "error" in info:
        return info
    latest = info["tag"].lstrip("vV")
    return {
        "current": media_core.APP_VERSION,
        "latest": info["tag"],
        "available": _version_tuple(latest) > _version_tuple(media_core.APP_VERSION),
        "asset": info["asset"],
        "notes": info["notes"],
    }


def _download(url, dest):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r, open(dest, "wb") as f:
        while True:
            chunk = r.read(65536)
            if not chunk:
                break
            f.write(chunk)


def _install_dir():
    if getattr(sys, "frozen", False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.abspath(media_core.__file__))


def apply_update(asset_url):
    """下载并准备自替换。返回 {"ok":True}（调用方随后退出进程）或 {"ok":False,"err":...}"""
    if not asset_url:
        return {"ok": False, "err": "Release 没有可用的 zip 附件"}

    if not getattr(sys, "frozen", False):
        # 源码模式：直接 git pull
        repo_dir = os.path.dirname(_install_dir())
        try:
            out = subprocess.run(["git", "-C", repo_dir, "pull"],
                                 capture_output=True, text=True, timeout=120)
            ok = out.returncode == 0
            return {"ok": ok, "err": None if ok else out.stderr.strip()[-300:],
                    "output": out.stdout.strip()[-300:]}
        except Exception as e:
            return {"ok": False, "err": f"git pull 失败: {e}"}

    # exe 模式：下载 → 解压 → 写自替换脚本 → 重启
    try:
        if os.path.isdir(UPD_DIR):
            shutil.rmtree(UPD_DIR)
        os.makedirs(UPD_DIR, exist_ok=True)
        zip_path = os.path.join(UPD_DIR, "update.zip")
        _download(asset_url, zip_path)
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(UPD_DIR)
        os.remove(zip_path)

        install = _install_dir()
        exe = sys.executable
        bat = os.path.join(UPD_DIR, "updater.bat")
        with open(bat, "w", encoding="ascii") as f:
            f.write("@echo off\n"
                    "timeout /t 2 /nobreak >nul\n"
                    f'xcopy /e /c /i /y "{UPD_DIR}\\*" "{install}\\" >nul\n'
                    f'rd /s /q "{UPD_DIR}"\n'
                    f'start "" "{exe}"\n')
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
        subprocess.Popen(["cmd", "/c", bat], creationflags=flags, close_fds=True)
        return {"ok": True}
    except Exception as e:
        return {"ok": False, "err": f"{type(e).__name__}: {e}"}


def exit_app():
    """更新包已就绪，彻底退出本进程（由 updater.bat 接管并重启）"""
    os._exit(0)
