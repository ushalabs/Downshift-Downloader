# Git history and current authorization

The latest request authorizes assistant commits/pushes to the existing main branch for the downloader update, Phase 9 and Phase 10. Earlier manual commands below are historical; main already exists. No branch recreation or force push is needed.

# Phase 8: your Git steps

Update after the browser/video request: the user has committed Phase 8 as `a65468d` and the active checkout is now `main`. The new browser/video changes remain uncommitted for the user. The Phase 8 instructions below are historical; do not repeat branch-creation commands against the existing `main`. No assistant Git mutations were performed during this update.

All Phase 8 Git operations belong to you. The prepared changes are in `C:\Users\HP\Desktop\Projects\Downshift` on `dev/phase-01-foundation`. The verified Phase 7 checkpoint is `017390e3167f0f6dad40f2671ae29f8f894d456a`. The instructions below create `main` at that checkpoint and merge the new Phase 8 development commit into it, preserving both branches and all previous history.

After Phase 8 verification is complete, open PowerShell and run these commands one at a time. Continue when each succeeds:

```powershell
Set-Location 'C:\Users\HP\Desktop\Projects\Downshift'
git status
git add .
git commit -m "perf(engine): buffer writes and harden recovery checkpoints"
git -c http.sslBackend=openssl push origin dev/phase-01-foundation
git switch -c main 017390e3167f0f6dad40f2671ae29f8f894d456a
git merge --no-ff dev/phase-01-foundation -m "Merge Phase 8 into main"
git -c http.sslBackend=openssl push -u origin main
git log --graph --oneline --decorate -8
```

The `-c http.sslBackend=openssl` option uses the transport that worked on this machine without changing your global settings. The merge creates a visible merge commit; `main` contains Phases 1-8 and the development branch remains available. Build files, dependency caches, test downloads, and databases are ignored and should not appear in `git status`.

On [your repository](https://github.com/ushalabs/Downshift-Download-Manager), open **Settings → Default branch**, choose `main`, and select **Update**. This makes `main` the default for new clones and pull requests. See [GitHub's official steps](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-branches-in-your-repository/changing-the-default-branch).

These commands assume `main` does not already exist. If you create it beforehand, use `git switch main` instead of `git switch -c main ...`; commit the Phase 8 development changes first, then perform the same merge. If Git reports conflicts or rejects a push, stop at that command and inspect the message; preserve existing work. No force push or history rewrite is needed.

After the merge, you can return to development with `git switch dev/phase-01-foundation`. Future approved development should incorporate the merge with `git merge --ff-only main` before new edits. This keeps the branch up to date with the shared history. Phase 9 packaging still requires your approval.
