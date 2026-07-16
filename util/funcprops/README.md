# Zephyr & ChromeOS EC Static Analysis Suite (`@funcprops`)

This utility directory contains static analysis tools designed to extract `@funcprops` documentation qualifiers (`\isr_ok`, `\sleep`, `\pre_kernel_ok`, etc.) from Doxygen headers and enforce context safety rules across compiled ChromeOS EC Zephyr binaries and source trees.

---

## 1. Declarative Rule Engine Architecture (`rules.json`)

All context violation rules are defined declaratively in **[`rules.json`](file:///usr/local/google/home/robbarnes/chromeos-trees/funcprops-checker/src/platform/ec/util/funcprops/rules.json)**. New static analysis checks can be added or modified without editing C disassemblers or call-graph traversal code.

### Structure of a Rule Entry:
```json
{
  "rule_id": "MUTEX_IN_ISR",
  "category": "ISR Context Violation",
  "contexts": ["ISR"],
  "prohibited_props": ["sleep"],
  "exemption_props": ["isr_ok"],
  "path_matchers": ["mutex", "i2c_lock", "lock"],
  "description": "Locking or unlocking a mutex or acquiring bus locks inside an interrupt vector causes thread scheduling corruption."
}
```

* **`rule_id`**: Identifier printed in reports.
* **`category`**: High-level classification (ISR, Pre-Kernel, Userspace, etc.).
* **`contexts`**: Target execution context (`ISR`, `PRE_KERNEL`).
* **`prohibited_props`**: `@funcprops` qualifiers forbidden in this context (`sleep`, etc.).
* **`exemption_props`**: `@funcprops` qualifiers acting as safe pruning barriers (`isr_ok`, `pre_kernel_ok`).
* **`path_matchers`**: Pattern keywords matched along call traces (e.g. `mutex`, `lock`).
* **`description`**: Human-readable explanation of why the rule exists.

---

## 2. Quickstart Usage

Execute the tool commands directly from any workspace checkout:

```bash
cd src/platform/ec/util/funcprops

# Step 1: Extract Funcprops Database (Auto-detects EC and Zephyr headers relative to script)
python3 extract_funcprops.py

# Step 2: Run Declarative Static Analysis on Compiled Firmware Binaries
python3 analyze_funcprops.py \
  ../../build/zephyr/domika/build-ro/zephyr/zephyr.elf \
  ../../build/zephyr/lapis/build-ro/zephyr/zephyr.elf \
  ../../build/zephyr/riven/build-ro/zephyr/zephyr.elf
```

### Custom Rule Config or Database
```bash
python3 analyze_funcprops.py \
  --db /tmp/funcprops.json \
  --rules /path/to/custom_rules.json \
  /path/to/compiled/zephyr.elf
```

---

## 3. Running Unit Tests

To run the automated test suite:

```bash
python3 funcprops_test.py
```
