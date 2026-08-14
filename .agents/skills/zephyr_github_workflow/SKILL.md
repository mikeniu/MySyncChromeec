---
name: zephyr_github_workflow
description: Workflow for creating, verifying, and pushing Zephyr commits to a public GitHub repository, ensuring compliance with upstream attribution and DCO guidelines.
---

# Zephyr GitHub Commit & Push Workflow

Use this workflow when you need to take changes made in a local workspace's Zephyr repository and commit/push them to an external or public GitHub repository (e.g., the user's fork) rather than using internal review tools like Gerrit.

## 1. Environment and Identification Setup
Before creating any commits, identify the current user's git configuration. Do NOT hardcode any specific username or email.

1. Run the following to get the user's identity:
   ```bash
   git config user.name
   git config user.email
   ```
2. You will use these values for any explicitly required signatures or placeholders, ensuring the commits are legally attributed to the human author.

## 2. Safe Repository Checkout
Do NOT push directly from the local workspace repository if it is managed by a different manifest (e.g., ChromiumOS Repo) or if you want to avoid contaminating local branches. Instead, create a fresh, shallow checkout:

1. Identify the exact commit of the local workspace to ensure identical file contexts:
   ```bash
   git rev-parse HEAD
   ```
2. Ask the user to provide the SSH clone URL of their personal GitHub fork of the Zephyr repository (e.g., `git@github.com:<github-username>/zephyr.git`). Do NOT assume their GitHub username matches their local LDAP.
3. Clone the provided repository into a temporary scratch or application data directory:
   ```bash
   git clone --depth 1 <fork-ssh-url> /path/to/scratch/zephyr-checkout
   ```
4. Checkout a new branch for the changes:
   ```bash
   git checkout -b <branch-name>
   ```

## 3. Applying Changes
1. If applying changes made in a Google Repo checkout (where the git toplevel might be a parent directory), generate patches with paths relative to the actual Zephyr root.
2. Use `git diff > /tmp/changes.patch` in the source repository.
3. Apply the patch in the new checkout using the appropriate prefix stripping (usually `-p2` for ChromeOS layouts):
   ```bash
   git apply -p2 --check /tmp/changes.patch
   git apply -p2 /tmp/changes.patch
   ```

## 4. Commit Guidelines and AI Attribution
When committing, strictly adhere to the following rules based on the Zephyr Project Guidelines:

### A. Signed-off-by (Developer Certificate of Origin)
> [!CRITICAL]
> **AI agents must not add Signed-off-by tags.** Only humans can legally certify the Developer Certificate of Origin (DCO).
> - Do NOT use `git commit -s` unless specifically and explicitly instructed by the human user for each commit.
> - If the user explicitly authorizes it, ensuring the tag uses **their** name and email (as fetched in Step 1).

### B. Usage Disclosure (Assisted-by)
When AI tools are used to help write a contribution, proper attribution is required via an `Assisted-by:` tag in the commit message or PR description:
Format:
```
Assisted-by: [Agent Name]:[Model Version] [Tool1] [Tool2]
```

*(Basic development tools like git, gcc, or editors should not be listed, and since internal agent codenames should not be leaked, always use the public framework name: `Antigravity`).*

### C. Commit Message Structure
Zephyr requires exactly 72-75 character line wrapping and a blank line separating the summary from the body.
1. Draft the message to a temporary file:
   ```text
   <subsystem>: <short imperative summary>

   <Detailed description explaining WHY the change was made, wrapped
   at 72 characters per line.>

   Assisted-by: Antigravity:gemini-1.5-pro
   ```
2. Commit using the message file (skipping local hooks if they conflict, as we will verify manually):
   ```bash
   git commit -n -F /tmp/commit.msg
   ```

## 5. Verification (checkpatch.pl)
Before pushing, run Zephyr's `checkpatch.pl` script on the exact commit email format. Do NOT rely on just local git hooks.

```bash
git show --format=email | ./scripts/checkpatch.pl
```
- If any style or missing description warnings appear, amend the commit using the message file and run the check again until it passes with `0 errors, 0 warnings`.

## 6. Pushing
Once verified, push to the remote branch:

```bash
git push -u origin <branch-name>
```
