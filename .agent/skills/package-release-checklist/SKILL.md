---
name: package-release-checklist
description: Use when preparing, packaging, tagging, or pushing a release for this repository; when changing release versions; when editing package-release.ps1, README.md, packaging/README.txt, tab-audio-recorder-mvp/manifest.json, or browser-extension packaging contents; or when asked to verify that a release package/tag will not miss files, stale versions, build outputs, documentation notes, or remote packaging triggers.
---

# Package Release Checklist

## Goal

Run this skill before tagging, pushing, or declaring a release ready. Treat packaging as a checklist with evidence, not as an assumption.

## Workflow

1. Read `AGENTS.md` first and obey repository-specific rules.
2. Inspect `git status --short --branch`; identify unrelated untracked paths and do not stage them.
3. Determine the intended release version from the user request, branch state, tag plan, or release notes. If ambiguous, stop and ask.
4. Verify version consistency:
   - `tab-audio-recorder-mvp/manifest.json` `version` must match the release version without a leading `v`.
   - Git tag must use the repository's tag convention, usually `v<version>`.
   - README examples and packaging notes must not imply an obsolete release when they describe the current operation.
5. Verify browser extension package contents:
   - List current files under `tab-audio-recorder-mvp` with `rg --files`.
   - Compare that list against the files copied by `package-release.ps1`.
   - Pay special attention to `manifest.json`, `service_worker.js`, `media_control.js`, `media_session_hook.js`, `page_control.js`, `offscreen.html`, `offscreen.js`, `pcm-worklet.js`, and `README.md`.
   - If a runtime-needed extension file is not copied, fix the script before tagging.
6. Verify documentation:
   - `README.md` must describe user-facing setup, reload requirements, and known first-play limitations when relevant.
   - `packaging/README.txt` must match the package layout a user will receive.
   - Do not document test UI, debug menus, or removed workflows.
7. Verify build requirements:
   - If code changed, run the relevant `build.ps1` scripts.
   - Only perform manual build commands after `build.ps1` fails and more detail is needed.
   - Report exactly which build scripts passed or failed.
8. Verify release automation assumptions:
   - If packaging is done remotely, do not run local packaging unless the user explicitly asks.
   - Confirm that pushing the release branch/tag is sufficient to trigger remote packaging.
   - If local packaging is required, run the packaging script only after the above checks pass.
9. Verify tag and push readiness:
   - Confirm the release tag does not already exist locally or remotely.
   - Confirm the tag points to the final commit that includes version, packaging, docs, and code changes.
   - Push the branch and tag only after the user request clearly includes pushing or publishing.
10. Final response must state:
   - Version checked.
   - Files/scripts changed.
   - Build scripts run and results.
   - Whether packaging was local or remote.
   - Tag and push results.
   - Any residual untracked files intentionally left out.

## Commands

Prefer these checks, adapted to the current shell:

```powershell
rg --files tab-audio-recorder-mvp
rg -n "0[.]2[.]0|0[.]2[.]1|0[.]3[.]0|version" tab-audio-recorder-mvp README.md packaging/README.txt package-release.ps1
git status --short --branch
git tag --list "v*"
```

Use the full Windows Git path if the environment's default `git` is broken:

```powershell
& 'C:\Program Files\Git\cmd\git.exe' status --short --branch
```

## Failure Rules

- If `manifest.json` still shows an old version, fix it before any tag or push.
- If a package script omits a runtime-needed file, fix it before any tag or push.
- If docs describe removed controls or test UI, fix them before release.
- If the package/check command fails because of sandboxing, do not silently substitute a weaker check; rerun with proper approval or report the blocker.
- If a check is skipped, say why.
