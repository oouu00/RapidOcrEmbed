import os
import subprocess
import sys

def run_git_cmd(cmd, check=True):
    print(f"执行命令: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True)
    stdout = result.stdout.decode('utf-8', errors='replace') if result.stdout else ''
    stderr = result.stderr.decode('utf-8', errors='replace') if result.stderr else ''
    if stdout:
        print("输出:", stdout.strip())
    if stderr:
        print("错误信息:", stderr.strip())
    if check and result.returncode != 0:
        raise Exception(f"命令执行失败: {' '.join(cmd)}")
    return result

def get_current_branch():
    result = run_git_cmd(["git", "rev-parse", "--abbrev-ref", "HEAD"], check=False)
    branch = result.stdout.decode('utf-8', errors='replace').strip()
    if branch and branch != "HEAD":
        return branch
    return "main"

def has_changes_to_commit():
    result = run_git_cmd(["git", "status", "--porcelain"])
    out = result.stdout.decode('utf-8', errors='replace')
    return bool(out.strip())

def clear_remote_and_upload():
    repo_git_url = "https://github.com/oouu00/RapidOcrEmbed.git"
    git_name = "oouu00"
    git_email = "qa@live.cn"

    # 当前目录就是 github发布
    target_path = os.getcwd()
    print(f"工作目录：{target_path}")

    # 初始化 git 仓库
    if not os.path.isdir(".git"):
        run_git_cmd(["git", "init"])
        # 设置默认分支为 main
        run_git_cmd(["git", "branch", "-M", "main"])

    # 配置用户名邮箱
    run_git_cmd(["git", "config", "user.name", git_name])
    run_git_cmd(["git", "config", "user.email", git_email])

    # 清理旧远程，重新绑定
    subprocess.run(["git", "remote", "remove", "origin"], capture_output=True)
    run_git_cmd(["git", "remote", "add", "origin", repo_git_url])

    # 先拉取远程（如果有），确保本地有完整历史
    run_git_cmd(["git", "fetch", "origin"], check=False)

    # 添加所有文件
    run_git_cmd(["git", "add", "."])

    # 检查是否有变更
    if has_changes_to_commit():
        commit_text = "更新仓库：完整源码 + 文档 + 合规文件"
        run_git_cmd(["git", "commit", "-m", commit_text])
    else:
        print("没有新的变更，跳过提交")

    branch = get_current_branch()
    print(f"当前分支: {branch}")

    # 强制推送到远程（清空远程仓库历史，用本地覆盖）
    print("准备强制推送（将清空远程仓库历史）...")
    run_git_cmd(["git", "push", "-f", "-u", "origin", branch])

    print("\n✅ 强制推送完成，远程仓库已被本地文件全部覆盖！")
    print(f"🔗 仓库地址: {repo_git_url}")

if __name__ == "__main__":
    try:
        clear_remote_and_upload()
    except Exception as err:
        print(f"\n❌ 执行失败：{str(err)}")
        sys.exit(1)
