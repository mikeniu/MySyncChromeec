
# Spec: ChromeOS EC Firmware Parity Auditor (V4 - Per-Bug Audit)

## 1. Context & Objective
We are quantifying the "Technical Debt Gap" between legacy firmware branches (e.g., `firmware-<board>-<version>.B`) and `main`. To provide actionable risk assessments, the auditor performs a "Binary-Aware" and "Source-Verified" audit of every bugfix on `main` that diverged since the branch's common ancestor.

The goal is to determine, for every bug, whether it is **Included**, **Missing**, or **Not Relevant** to specific device models based on the actual code present in their production binaries.

## 2. Analysis Core

### 2.1 Bug Categorization (Heuristic Labeling)
Every bug is analyzed via its primary commit on `main` and assigned one or more heuristic labels:
- **stability**: Fixes for crashes, hangs, panics, or watchdog resets.
- **bringup**: Board-specific initializations, hardware quirks, or early-stage development.
- **power**: PMIC, battery, charging, and power-sequencing adjustments.
- **security**: Fixes for vulnerabilities (CVEs) or sensitive data handling.
- **thermal**: DPTF, temperature management, or fan control.
- **feature**: New functional support or enhancements.
- **bugfix**: General logic corrections (catch-all).

### 2.2 Inclusion Verification
A bug is marked **Included** if any of its associated commits on `main` are found in the branch via:
1. **Metadata Matching**: The BUG ID (e.g., `b:123`, `Fixed: 123`) exists in the branch git log.
2. **Patch-ID Matching**: A commit with the same code changes (via `git patch-id`) exists on the branch, even if the SHA or commit message differs (e.g., manual cherry-picks).
3. **Source Verification (`--verify`)**: (Optional) The auditor attempts to reverse-apply the patch (`git apply --reverse --check`). If the patch can be reversed, the code state is confirmed to be "after the fix," regardless of git history.

### 2.3 Relevance Filtering (Binary-Aware)
A bug is marked **Not Relevant** if it is not Included but the code it touches is absent from the specific model's binary:
- **Symbol Check**: The functions/symbols modified by the commit are not found in the model's ELF (`objdump -t`).
- **Path Check**: The files modified by the commit are not found in the model's build manifest/MAP file. Paths are normalized to full repository-relative paths (e.g., `common/hooks.c`) to avoid false-positive basename matches.

## 3. Confidence Scoring
- **Very High**: Binary analysis confirmed relevance AND source verification confirmed presence.
- **High**: Binary analysis was used to filter relevance.
- **Low**: Analysis defaulted to purely text-based git-log comparison (e.g., GCS binary missing).

## 4. Output Specification (CSV)
The script outputs a matrix where each row is a unique **Bug ID**, and columns represent the status across all discovered **Models**.

`bug_id, subject, labels, board, [model_1_status], [model_2_status], ...`

**Field Definitions:**
- **subject**: The git subject line of the primary commit.
- **labels**: Comma-separated list of heuristic categories.
- **model_status**: Enum: `Included`, `Missing`, `Not Relevant`.

## 5. Logic Flow
1. **Branch Resolution**: Identify the most recent legacy branch and the `common-ancestor` with `main`.
2. **Binary Acquisition**: Retrieve `.elf` and `.map` files from GCS for each model associated with the board.
3. **Main Bug Collection**: Identify all unique Bug IDs on `main` since the `common-ancestor`.
4. **Verification Loop**: For each bug, perform inclusion and relevance checks against each model's binary and source state.
5. **Report Generation**: Aggregate results into the bug-centric CSV matrix.
