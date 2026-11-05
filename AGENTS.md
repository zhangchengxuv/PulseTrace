# PulseTrace development and Git workflow

These project instructions apply to every subsequent PulseTrace task.

## Standing authorization

The user authorizes committing validated PulseTrace changes and pushing the same
commits to the already configured GitHub and Gitee repositories. Complete this
workflow before ending a development task; do not ask for routine push approval.
This does not authorize tags, Releases, force pushes, deleting remote branches,
rewriting shared history, bypassing branch protection, or publishing installers.

## Required workflow

1. Read `docs/development-status.md` when resuming work. Inspect actual code.
2. Run `git status`, `git remote -v`, and `git branch -vv`. Discover the real
   remotes and current branch; never assume names or change remote URLs.
3. Preserve unrelated and unfinished user changes. Implement the scoped change,
   build, and run relevant tests. Reuse existing valid long-running test results
   when the code they cover has not changed. Record external validation limits.
4. Before committing, run `git status --short`, `git diff --check`, and
   `git diff --stat`. Review new files as well as tracked diffs and `.gitignore`.
5. Stage only reviewed, explicitly selected paths or hunks. Never use an
   unreviewed `git add -A`. Do not commit personal databases/statistics, captured
   screens, user profiles, logs, credentials, downloads, caches, or release EXEs.
   Public validation documents must omit real personal traffic/input records.
6. Use concise English Conventional Commits: feat, fix, refactor, perf, style,
   docs, test, build, or chore. Add a body for important changes and compatibility
   limits. Keep independent work separate; never create deliberately broken
   intermediate snapshots just to split files by feature name.
7. Push the exact reviewed commit to both configured repositories' corresponding
   branches. `tools/sync-remotes.ps1 -Push -ExpectedCommit <SHA>` provides guarded
   synchronization; without `-Push` it only inspects remote state.
8. Compare local HEAD with actual GitHub and Gitee branch SHAs after pushing.
   Only report full synchronization when all three match. Push success alone is
   insufficient. Preserve successful pushes if the other repository fails.
9. Diagnose conflicts/permission errors safely. Retry transient network failures
   a limited number of times. Do not force, auto-rebase shared commits, or merge
   unknown remote work to manufacture a successful result.

## Reporting

End each task with a concise Chinese report containing: modification, actual
tests, commit SHA/message, GitHub result, Gitee result, SHA synchronization status,
and remaining issues. Distinguish failed pushes from unverified remote state.
Ordinary commits do not create a Tag, Release, or new published installation pack.
