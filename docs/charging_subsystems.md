# ChromeOS Embedded Controller: Power and Charging Subsystems

## 1. The `charge_manager` Subsystem
The `charge_manager` subsystem acts as the **central power arbitration engine**
for the ChromeOS Embedded Controller (EC). Its primary purpose is to manage
available charging sources from multiple ports, apply a strict hierarchy of
supplier priorities, and enforce current and voltage limits requested by the
host or internal safety modules.

### Port and Supplier Arbitration
The charge manager keeps track of the available charge (voltage and current)
reported by all ports and supplier types. It evaluates these inputs and selects
the optimal charge port and charging profile based on a strict priority
hierarchy:
1. **Dedicated Chargers** (e.g., barrel jacks) – Always picked if present, as it
   is the best product decision.
2. **USB Power Delivery (PD) Negotiated Chargers** – Have the highest power
   potential (at least 15W).
3. **Type-C (1.5A or 3.0A)** – Standard Type-C connections supplying 1.5A or
   more.
4. **Proprietary & BC1.2 Chargers** (DCP, CDP, SDP) – Legacy USB chargers.
5. **Type-C under 1.5A / Other / VBUS** – Lowest priority suppliers.

If there is a tie in priority between two ports, it selects the supplier capable
of providing the highest total power.

### Charge Ceilings and Power Limits
The subsystem manages temporary current ceilings (limits) that can be set
independently by different requestors, such as the USB-PD task during
negotiation or the host (Application Processor). The charge manager evaluates
all requested ceilings and strictly enforces the *minimum* ceiling across all
requestors.

### Safe Mode and "Seeding"
During early system boot, before the USB-PD stack and battery fuel gauge are
fully initialized, the charge manager operates in a specialized **"Safe Mode"**.
* **Brownout Prevention:** While in safe mode, it ignores charge ceilings and
  treats all chargers as dedicated candidates. It intentionally prevents the
  system from selecting `CHARGE_PORT_NONE` (which would cut off input FETs) so
  the system doesn't lose power and shut down while negotiations are ongoing.
* **Seeding:** The charge manager waits to be "seeded"—meaning it waits for all
  potential power sources and ports to report their initial charge status—before
  it makes permanent charging decisions or transitions out of safe mode.

### Overrides and Dual-Role Management
* **Overrides:** The system allows for forced port selection. Through host or
  console commands, a specific port can be marked as the `override_port`. The
  charge manager will prioritize this port over the standard priority hierarchy,
  or it can be instructed to explicitly not charge at all
  (`OVERRIDE_DONT_CHARGE`).
* **Dual-Role Power (DRP) Swaps:** It tracks the dual-role capabilities of
  attached devices. If an override port is selected but the attached device is
  currently acting as a power sink (and is dual-role capable), the charge
  manager will request a power role swap from the PD stack.

### Host Communication and Telemetry
The `charge_manager` acts as the definitive source of truth for power
information and exposes this data to both the Host (via Host Commands) and
developers (via the EC console). It responds to host commands like
`EC_CMD_USB_PD_POWER_INFO` and provides console commands like `chgoverride`,
`chglim`, and `chgsup`.

---

## 2. The `charge_state` Machine
While the `charge_manager` arbitrates between multiple external power *sources*,
the `charge_state` machine monitors the *battery* itself. It dictates how much
current and voltage to request from the charger, handles deep-discharge
recovery, regulates battery State of Charge (SoC), and enforces critical safety
and thermal limits.

### The Four Primary States
The core of the state machine is defined by `enum charge_state`. It is evaluated
continuously by `decide_charge_state()`:
* **`ST_IDLE`:** The system is not actively charging or discharging. Entered if
  there is no battery present, if a dead battery timed out during precharge, or
  if charging is forcibly suspended (e.g., due to thermal limits).
* **`ST_DISCHARGE`:** The system is running on battery power (no AC power is
  present).
* **`ST_PRECHARGE`:** A recovery state for a battery that is unresponsive or at
  an extremely low voltage. It applies a safe, low "precharge current" for a
  limited time to safely revive the battery.
* **`ST_CHARGE`:** The normal charging state where the battery is responsive, AC
  power is present, and the battery can accept a charge.

### The Charger Task Loop (`charger_task`)
The state machine operates within a continuous RTOS task loop. During each
iteration, it performs the following sequence:
1. **Gather Telemetry:** Checks if AC power is present and reads parameters from
   the charger IC and battery fuel gauge.
2. **Determine State:** Evaluates battery presence and AC status to transition
   between the four primary states.
3. **Process Policies:** Checks if charging should be halted due to extreme
   temperatures and notifies the host of low battery states.
4. **Run Battery Sustainer:** Evaluates logic to maintain the battery within a
   specific percentage range.
5. **Adjust Requests:** Calculates the exact voltage and current to request from
   the physical charger IC, applying external limits or zeroing them out if
   necessary.
6. **Sleep/Wait:** Determines how long the task should sleep before polling
   again (e.g., frequent polling during active charging, infrequent polling when
   off and discharging).

### Safety and Hardware Protection
* **Critical Shutdowns:** If the battery gets too hot, too cold to discharge
  safely, or reaches critically low voltage, the EC starts a timer and can force
  a system shutdown or physically cut off the battery to prevent deep-discharge
  damage.
* **AP Throttling:** If the battery experiences an under-voltage or over-current
  discharge event, the state machine notifies the Application Processor (AP) to
  throttle system performance to reduce power draw.
* **Temperature Guardrails:** If battery temperature falls outside allowed
  charging limits, requested charge current and voltage are clamped to `0`,
  forcing the state machine into `ST_IDLE`.

### The Battery Sustainer
The "Battery Sustainer" (`sustain_battery_soc()`) allows the system to
artificially maintain the battery's State of Charge (SoC) between a `lower` and
`upper` bound by manipulating the control mode (`CHARGE_CONTROL_NORMAL`,
`CHARGE_CONTROL_IDLE`, or `CHARGE_CONTROL_DISCHARGE`).

### Interaction with LEDs
The `charge_state` machine acts as the source of truth for device indicator
LEDs. It translates the internal charge state (e.g., `ST_CHARGE`,
`ST_DISCHARGE`) into high-level LED states (like `LED_PWRS_CHARGE_NEAR_FULL` or
`LED_PWRS_ERROR`) to be interpreted by board-specific LED drivers.

---

## 3. Batteryless System Management
Because there is no battery to handle power transients or interruptions, the EC
employs specialized mechanisms to safely operate systems when no battery is
present (or when the battery is completely dead). The firmware modifies its
standard behavior to aggressively prevent system brownouts.

### Charge Manager Adaptations
* **Port Switching is Prevented:** To avoid interrupting the only power source,
  if the battery is disconnected, the charge manager is hardcoded to retain the
  same charge port and charge supplier.
* **Spoofed Dual-Role Capabilities:** To guarantee power is drawn, the EC
  overrides standard USB-PD rules and assumes the attached charger is a
  "dedicated" charger, even if it is a dual-role capable port.
* **Debouncing is Bypassed:** Charger detection debouncing
  (`CHARGE_DETECT_DELAY`) is bypassed on batteryless systems. The system must be
  stable immediately, as it cannot rely on battery power while negotiations are
  ongoing.

### Charge State Machine & Current Limits
* **Forced to `ST_IDLE`:** If the state machine detects no battery (`BP_NO`), it
  forces the state to `ST_IDLE`, stopping any requested charging voltage/current
  to the battery terminals.
* **Maximized Input Current:** The EC allows the system to pull as much input
  current as needed from the charger. While this risks overcurrenting the
  charger, it is deemed necessary to avoid browning out from insufficient input
  current.
* **Safe Clamping:** While pulling maximum input current, the EC calculates the
  maximum allowed system power (e.g., `CONFIG_USB_PD_MAX_POWER_MW`). If input
  power exceeds safe hardware limits, it clamps the requested current to prevent
  damaging the system.

### Power-On Safety & Boot Constraints
* **Power Thresholds:** The `charge_prevent_power_on()` function checks for
  battery presence. If missing, the EC strictly gates the Application Processor
  (AP) from booting unless the attached charger can supply a guaranteed minimum
  amount of power (`CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON`). This prevents
  the AP from attempting to boot and crashing the power rail.

### Hardware and Driver-Level Safeguards
* **"Dead Battery" Recovery Sequence:** Power Path Controllers (PPCs, e.g.,
  `nx20p348x`) enforce a strict "enable-before-clear" sequence. The high-voltage
  sink FET must be explicitly enabled in software *before* the hardware's
  internal "Dead Battery" exit bit is cleared; otherwise, the power path drops
  and the system browns out.
* **Disabling Hardware Ramping (ICO):** Input Current Optimization (ICO) on
  charger ICs (like the `bq25710`) is explicitly disabled if no battery is
  present. Testing the adapter's capacity limit without a battery to absorb
  transients causes the system voltage (`VSYS`) to drop out, leading to a system
  crash.
