# Zephyr EC Tokenized Logging

[TOC]

## Introduction

Tokenized logging is a feature that reduces your binary's image size by
converting log format strings into 32 bit token identifiers. These format
strings and tokens are saved off into a token database used for detokenizing
when viewing the logs. Zephyr EC leverages [Pigweeds
Tokenizer](https://pigweed.dev/pw_tokenizer/) module to accomplish tokenizing
and detokenizing of logs.

Tokenized EC logs should be transparent to the developer in most scenarios.
Detokenizing occurs before the log is outputted to console or saved to
`cros_ec.log` on the DUT.

## Enabling Tokenization in EC

Add the Zephyr snippet [`pw-tokenize`] to your project's configuration.
Additionally, make sure `picolibc`, `pigweed` modules are added to your project.

```python
register_brox_project(
    project_name="brox",
    modules=["picolibc", "ec", "pigweed"],
    snippets=["pw-tokenize"],
)
The `pw-tokenize` snippet automatically configures the necessary Kconfig
options:
* `CONFIG_PLATFORM_EC_LOG_TOKENIZED=y`: Enables EC tokenized logging in the
  Zephyr EC application.
* `CONFIG_PIGWEED_LOG_TOKENIZED_LIB=y`: Selects Pigweed's tokenized logging
  backend library.
* `CONFIG_PICOLIBC=y` & `CONFIG_CPP=y`: Enables C++ and toolchain support
  required by Pigweed's tokenizer.


The `pw-tokenize` snippet automatically configures the necessary Kconfig
options:
* `CONFIG_PLATFORM_EC_LOG_TOKENIZED=y`: Enables EC tokenized logging in the
  Zephyr EC application.
* `CONFIG_PIGWEED_LOG_TOKENIZED_LIB=y`: Selects Pigweed's tokenized logging
  backend library.
* `CONFIG_PICOLIBC=y` & `CONFIG_CPP=y`: Enables C++ and toolchain support
  required by Pigweed's tokenizer.
```

The `pw-tokenize` snippet automatically configures the necessary Kconfig
options:
* `CONFIG_PLATFORM_EC_LOG_TOKENIZED=y`: Enables EC tokenized logging in the
  Zephyr EC application.
* `CONFIG_PIGWEED_LOG_TOKENIZED_LIB=y`: Selects Pigweed's tokenized logging
  backend library.
* `CONFIG_PICOLIBC=y` & `CONFIG_CPP=y`: Enables C++ and toolchain support
  required by Pigweed's tokenizer.

## Token Database
The token database contains a mapping of the 32 bit hashed token ids to the log
format strings they represent.  Upon receiving a tokenized log message from EC,
the detokenizing process references this database to decode the message back to
its original string.

There are three types of databases available when using tokenization in EC.
 1. **Board Specific** - This database is only compatible with the board its
 built against.
 2. **Unified** - This database is a merged database of all board specific
 databases of a build, this database is compatible with all boards in the
 current build.
 3. **Historical** - This database contains a history of all tokens.  This will
3. Start `servod` pointing `--token_db` to the local database output (e.g.
   `build/tokens.bin`).
4. Flash EC - specify `--dut_ip` argument to automatically scp the local token
   database (`build/tokens.bin`) to `/usr/local/cros_ec/tokens.bin` on the DUT.
 Historical Database section for more details.

## Workflows

### Local EC Images

1. Building EC with Tokenization
    1. Enable Tokenization as defined above.
3. Start `servod` pointing `--token_db` to the local database output (e.g.
   `build/tokens.bin`).
4. Flash EC - specify `--dut_ip` argument to automatically scp the local token
   database (`build/tokens.bin`) to `/usr/local/cros_ec/tokens.bin` on the DUT.
Servod accepts a `token_db` argument specifying the path to the token database.
The default path is `/usr/share/cros_ec/tokens/historical.bin`.

Servod leverages Pigweed's auto-updating detokenizer, which actively monitors
the token database file on disk and reloads it automatically whenever changes
are detected. By passing the local zmake database output path (e.g.
`build/tokens.bin` or `build/zephyr/${BOARD}/output/database.bin`) as the token
database argument, you can leave servod running continuously during local
development. Whenever you recompile and reflash the EC, servod will immediately
pick up newly added or modified log format strings without requiring a restart.
    * Portage/Ebuild
      * **Board Specific**: `"${root_build_dir}/${project}"/output/database.bin`
      * **Unified**: `"${root_build_dir}/tokens.bin`
3. Start `servod` pointing `--token_db` to the local database output (e.g.
   `build/tokens.bin`).
4. Flash EC - specify `--dut_ip` argument to automatically scp the local token
   database (`build/tokens.bin`) to `/usr/local/cros_ec/tokens.bin` on the DUT.
Servod accepts a `token_db` argument specifying the path to the token database.
The default path is `/usr/share/cros_ec/tokens/historical.bin`.

Servod leverages Pigweed's auto-updating detokenizer, which actively monitors
the token database file on disk and reloads it automatically whenever changes
are detected. By passing the local zmake database output path (e.g.
`build/tokens.bin` or `build/zephyr/${BOARD}/output/database.bin`) as the token
database argument, you can leave servod running continuously during local
development. Whenever you recompile and reflash the EC, servod will immediately
pick up newly added or modified log format strings without requiring a restart.
### Released EC images

Nothing to do! The token database is preloaded on DUT and servod docker images.
Viewing logs should not require any additional steps!

  Returns the active database file path (e.g.
`/usr/share/cros_ec/tokens/historical.bin`)

Servod accepts a `token_db` argument specifying the path to the token database.
The default path is `/usr/share/cros_ec/tokens/historical.bin`.

Servod leverages Pigweed's auto-updating detokenizer, which actively monitors
the token database file on disk and reloads it automatically whenever changes
are detected. By passing the local zmake database output path (e.g.
`build/tokens.bin` or `build/zephyr/${BOARD}/output/database.bin`) as the token
database argument, you can leave servod running continuously during local
development. Whenever you recompile and reflash the EC, servod will immediately
pick up newly added or modified log format strings without requiring a restart.

Upon startup, tokenization is defaulted on or off by the servod overlay
configuration file. You can change the default by modifying the
`uses_cros_ec_tokens` control name.
Example: https://crrev.com/c/5202485.
  Returns the active database file path (e.g.
#### Docker vs. Local Paths

When servod runs inside a Docker container (the standard execution mode), file
paths passed to `dut-control ec_token_db:<path>` are evaluated from within the
container's filesystem. Passing an arbitrary host path at runtime will fail if
that path was not mounted when the container started.

To load a newly built local database into a running servod container without
restarting it, you can copy the database file into the container via `docker cp`
and then point `dut-control` to the internal path:

```bash
# 1. Find the running servod container (e.g. servod_<PORT>) via servod-ps or docker ps
servod-ps
# or: docker ps

# 2. Copy the token database into the container's /tmp directory
docker cp build/tokens.bin <CONTAINER_NAME>:/tmp/tokens.bin

# 3. Point dut-control to the in-container path
dut-control ec_token_db:/tmp/tokens.bin
```

Alternatively, pass `--token_db` when launching `start-servod` to have the
wrapper automatically mount your host build directory upon container startup.

`/usr/share/cros_ec/tokens/historical.bin`)
  <control>
    <name>uses_cros_ec_tokens</name>
```bash
    <params drv="echo" value="always" interface="servo"/>
  </control>
```


You can dynamically inspect and control EC3PO detokenization at runtime without
restarting servod using the `ec_token_db` control:
automatically be used to detokenize logs:
* **Query tokenization status and active database**:
```bash
  ```

#### Docker vs. Local Paths
If you want to use a locally built token database, pass `--token_db` to
`start-servod`. The wrapper script will automatically mount the host directory
into the container and forward the path to the internal servod process:
```bash
restarting it, you can copy the database file into the container via `docker cp`
and then point `dut-control` to the internal path:

The docker image is prepopulated with the historical token database at
`/usr/share/cros_ec/tokens/historical.bin`.

# 3. Point dut-control to the in-container path

> **Note:** The in-band `%tokens` console command is being deprecated in favor
> of the `dut-control ec_token_db` interface (see [Controlling Tokenization via
`dut-control`](#controlling-tokenization-via-dut-control)).
> Prefer using `dut-control` to inspect or toggle token database configuration.

Once servod is running, you can also toggle the detokenizer algorithm directly
from the EC console by connecting to the `ec_uart_pty` and running one of the
following commands:
Alternatively, pass `--token_db` when launching `start-servod` to have the
wrapper automatically mount your host build directory upon container startup.
  Returns the active database file path (e.g.
`/usr/share/cros_ec/tokens/historical.bin`)
  if tokenization is enabled, or `off` if disabled.

```bash
  ```bash
  dut-control ec_token_db:/path/to/tokens.bin
Using `%tokens on` with no path reloads the last path specified. On start-up

  ```bash
  dut-control ec_token_db:off
  ```
automatically be used to detokenize logs:
* **Re-enable detokenization (with last used database)**:
```bash
  ```

#### Docker vs. Local Paths
If you want to use a locally built token database, pass `--token_db` to
`start-servod`. The wrapper script will automatically mount the host directory
into the container and forward the path to the internal servod process:
```bash
restarting it, you can copy the database file into the container via `docker cp`
character at the end of your command. Some terminal emulators add `"\r\n"` when

The docker image is prepopulated with the historical token database at
`/usr/share/cros_ec/tokens/historical.bin`.

# 3. Point dut-control to the in-container path

> **Note:** The in-band `%tokens` console command is being deprecated in favor
> of the `dut-control ec_token_db` interface (see [Controlling Tokenization via
`dut-control`](#controlling-tokenization-via-dut-control)).
> Prefer using `dut-control` to inspect or toggle token database configuration.

Once servod is running, you can also toggle the detokenizer algorithm directly
from the EC console by connecting to the `ec_uart_pty` and running one of the
#### ChromeOS

* **Writable path (`/usr/local`)**:
  The first path can be updated via secure copy (the same method used by
`flash_ec`):
  ```bash
  scp tokens.bin root@${DUT_IP}:/usr/local/cros_ec/tokens.bin
  ```
### Inside Chroot
* **Read-only rootfs (`/usr/share`)**:
  The second path is on a read-only partition. To update it on ChromeOS, build
and deploy the `chromeos-ec-token` package:
  ```bash
  cros workon start chromeos-base/chromeos-ec-token -b ${BOARD}
  cros_sdk cros_workon_make --board=${BOARD} chromeos-base/chromeos-ec-token
  cros deploy ${DUT_IP} chromeos-base/chromeos-ec-token
  ```

#### ALOS (Android)

On ALOS DUTs, copy the local token database to overwrite `chromeos-ec-token-
historical.bin` in the Android tree, build the module, and update the vendor
partition using `adevice`:

```bash
# 1. Copy local token database to Android ec-prebuilts
cp build/tokens.bin ${ANDROID_BUILD_TOP}/vendor/google/firmware/desktop/ec-prebuilts/chromeos-ec-token-historical.bin

# 2. Build the prebuilt module and update vendor partition
m chromeos-ec-token-historical
adevice update -p vendor

# 3. Confirm update/size on DUT
adb shell du -b /vendor/etc/cros_ec/tokens.bin
### Using Docker

The docker image supports fetching the historical database from GCS upon
start-up when provided the `--fetch-token-db` argument. This database will
automatically be used to detokenize logs:

tool](https://pigweed.dev/pw_tokenizer/detokenization.html#detokenizing-cli-
tool) to help with debugging detokenizing failures.
```
First, you'll need to setup your pigweed root directory. This typically lives
If you want to use a locally built token database, pass `--token_db` to
`start-servod`. The wrapper script will automatically mount the host directory
```bash
export PW_ROOT=~/chromiumos/src/third_party/pigweed/
start-servod -b ${BOARD} -m ${MODEL} --channel=release --token_db=${EC_PATH}/build/tokens.bin
character at the end of your command. Some terminal emulators add `"\r\n"` when
Using the tokenized message above, the command below detokenizes it:
`/usr/share/cros_ec/tokens/historical.bin`.
```bash
python3 -m pw_tokenizer.detokenize base64 -i failed.txt -p "\`" build/tokens.bin | sed "s/~//g"
```
*(Or invoke via `${PW_ROOT}/pw_tokenizer/py/pw_tokenizer/detokenize.py`)*

Output:
```
Port C0 CC3, Enable - Role: SNK-DFP TC State: Attached.SNK, Flags: 0x9002 PE
State: PE_SNK_Ready, Flags: 0x0201 SPR
> **Note:** The in-band `%tokens` console command is being deprecated in favor
> of the `dut-control ec_token_db` interface (see [Controlling Tokenization via
`dut-control`](#controlling-tokenization-via-dut-control)).
> Prefer using `dut-control` to inspect or toggle token database configuration.

You can also fetch the `cros_ec.log` from DUT and detokenize the log:
```bash
scp root@${DUT_IP}:/var/log/cros_ec.log ./cros_ec.log

A snippet from `cros_ec.log` may look like the following:
  The first path can be updated via secure copy (the same method used by
`flash_ec`):
  ```bash
2024-01-11T19:58:46.532000Z Board:      2
2024-01-11T19:58:46.532000Z RO: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z RW: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z Build:      markarth-0.0.0-8c88717 2024-01-11
10:15:35
2024-01-11T19:58:46.532000Z             asemjonovs@asemjonovs TOK
and deploy the `chromeos-ec-token` package:
  ```bash
Notice the log has detokenized lines as well as an untranslated tokenized line.
Running the following:
```bash
python3 -m pw_tokenizer.detokenize base64 -i ./cros_ec.log -p "\`" build/tokens.bin > cros_ec_detokenized.log

#### ALOS (Android)
Fixes the log:
On ALOS DUTs, copy the local token database to overwrite `chromeos-ec-token-
historical.bin` in the Android tree, build the module, and update the vendor
2024-01-11T19:58:46.532000Z Chip:       Nuvoton npcx9m3f 00160207
2024-01-11T19:58:46.532000Z Board:      2
2024-01-11T19:58:46.532000Z RO: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z RW: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z Build:      markarth-0.0.0-8c88717 2024-01-11
10:15:35
2024-01-11T19:58:46.532000Z             asemjonovs@asemjonovs TOK
# 2. Build the prebuilt module and update vendor partition
m chromeos-ec-token-historical
adevice update -p vendor

# 3. Confirm update/size on DUT
adb shell du -b /vendor/etc/cros_ec/tokens.bin
its entire history of log format strings used over time. This database should
*Note: Path to token database is based on where servod is launched and running!*

Viewing logs on a tokenized EC image with tokenization turned off will look like
the following.
```
tool](https://pigweed.dev/pw_tokenizer/detokenization.html#detokenizing-cli-
tool) to help with debugging detokenizing failures.
23-12-06 14:44:39.816 `o7eqFQAGBkVuYWJsZQNTTksDREZQ~`8RegCQA=~`P2J9PQxBdHRhY2hlZC5TTkuEwAQ=~`UpI03wxQRV9TTktfUmVhZHmCCA==~`ubjWdA==~`dwIKAA==~ec:~>
First, you'll need to setup your pigweed root directory. This typically lives
A failure to decode will dump its base64 tokenized message as well. You'll
notice above the base64 message is encapsulated with a prefix of
```bash
export PW_ROOT=~/chromiumos/src/third_party/pigweed/
*Note - depending on the terminal emulator used you may need to force a `\n`
character at the end of your command. Some terminal emulators add `"\r\n"` when
Using the tokenized message above, the command below detokenizes it:
`%tokens off<ctrl+v><enter><enter>` using socat.*
```bash
python3 -m pw_tokenizer.detokenize base64 -i failed.txt -p "\`" build/tokens.bin | sed "s/~//g"
```
*(Or invoke via `${PW_ROOT}/pw_tokenizer/py/pw_tokenizer/detokenize.py`)*

Output:
```
Port C0 CC3, Enable - Role: SNK-DFP TC State: Attached.SNK, Flags: 0x9002 PE
State: PE_SNK_Ready, Flags: 0x0201 SPR
### [Timberslide](https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/timberslide)

Timberslide expects the database to be located at one of these locations, and
will use the first found in this order.
1. `/usr/local/cros_ec/tokens.bin`
You can also fetch the `cros_ec.log` from DUT and detokenize the log:
```bash
scp root@${DUT_IP}:/var/log/cros_ec.log ./cros_ec.log

A snippet from `cros_ec.log` may look like the following:
  The first path can be updated via secure copy (the same method used by
`flash_ec`):
  ```bash
2024-01-11T19:58:46.532000Z Board:      2
2024-01-11T19:58:46.532000Z RO: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z RW: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z Build:      markarth-0.0.0-8c88717 2024-01-11
10:15:35
2024-01-11T19:58:46.532000Z             asemjonovs@asemjonovs TOK
and deploy the `chromeos-ec-token` package:
  ```bash
Notice the log has detokenized lines as well as an untranslated tokenized line.
Running the following:
```bash
python3 -m pw_tokenizer.detokenize base64 -i ./cros_ec.log -p "\`" build/tokens.bin > cros_ec_detokenized.log

#### ALOS (Android)
Fixes the log:
On ALOS DUTs, copy the local token database to overwrite `chromeos-ec-token-
historical.bin` in the Android tree, build the module, and update the vendor
2024-01-11T19:58:46.532000Z Chip:       Nuvoton npcx9m3f 00160207
2024-01-11T19:58:46.532000Z Board:      2
2024-01-11T19:58:46.532000Z RO: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z RW: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z Build:      markarth-0.0.0-8c88717 2024-01-11
10:15:35
2024-01-11T19:58:46.532000Z             asemjonovs@asemjonovs TOK
# 2. Build the prebuilt module and update vendor partition
m chromeos-ec-token-historical
adevice update -p vendor

# 3. Confirm update/size on DUT
adb shell du -b /vendor/etc/cros_ec/tokens.bin
its entire history of log format strings used over time. This database should

## Recovering from failures
https://storage.googleapis.com/chromeos-
localmirror/distfiles/cros_ec/tokens/chromeos-ec-token-historical.bin
A failure can occur when an outdated database is used with an EC image. Pigweed
provides a [Detokenizing CLI
tool](https://pigweed.dev/pw_tokenizer/detokenization.html#detokenizing-cli-
tool) to help with debugging detokenizing failures.

First, you'll need to setup your pigweed root directory. This typically lives
in the following location.

```bash
export PW_ROOT=~/chromiumos/src/third_party/pigweed/
```

Using the tokenized message above, the command below detokenizes it:

```bash
python3 -m pw_tokenizer.detokenize base64 -i failed.txt -p "\`" build/tokens.bin | sed "s/~//g"
```
*(Or invoke via `${PW_ROOT}/pw_tokenizer/py/pw_tokenizer/detokenize.py`)*

Output:
```
Port C0 CC3, Enable - Role: SNK-DFP TC State: Attached.SNK, Flags: 0x9002 PE
State: PE_SNK_Ready, Flags: 0x0201 SPR
ec:>
```

The `sed "s/~//g"` is used to strip the token suffix from the output.

You can also fetch the `cros_ec.log` from DUT and detokenize the log:
```bash
scp root@${DUT_IP}:/var/log/cros_ec.log ./cros_ec.log
```
A snippet from `cros_ec.log` may look like the following:
```
2024-01-11T19:58:46.532000Z [9536.808900 HC 0x0137]
2024-01-11T19:58:46.532000Z `jqTWxQdOdXZvdG9uCG5wY3g5bTNmCDAwMTYwMjA3
2024-01-11T19:58:46.532000Z Board:      2
2024-01-11T19:58:46.532000Z RO: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z RW: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z Build:      markarth-0.0.0-8c88717 2024-01-11
10:15:35
2024-01-11T19:58:46.532000Z             asemjonovs@asemjonovs TOK
2024-01-11T19:58:46.532000Z Reset flags: 0x00000020 (soft)
```
Notice the log has detokenized lines as well as an untranslated tokenized line.
Running the following:
```bash
python3 -m pw_tokenizer.detokenize base64 -i ./cros_ec.log -p "\`" build/tokens.bin > cros_ec_detokenized.log
```

Fixes the log:
```
2024-01-11T19:58:46.532000Z [9536.808900 HC 0x0137]
2024-01-11T19:58:46.532000Z Chip:       Nuvoton npcx9m3f 00160207
2024-01-11T19:58:46.532000Z Board:      2
2024-01-11T19:58:46.532000Z RO: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z RW: markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z Build:      markarth-0.0.0-8c88717 2024-01-11
10:15:35
2024-01-11T19:58:46.532000Z             asemjonovs@asemjonovs TOK
2024-01-11T19:58:46.532000Z Reset flags: 0x00000020 (soft)
```

## Historical Database Management

The historical token database is the database to support all boards and
its entire history of log format strings used over time.  This database should
handle all boards no matter when it was released.
This lives at
https://storage.googleapis.com/chromeos-localmirror/distfiles/cros_ec/tokens/chromeos-ec-token-historical.bin

Database management is handled in `recipes/build_firmware_historical_db.py`.  The
[firmware-zephyr-token-db-uploader](https://ci.chromium.org/ui/p/chromeos/builders/informational/firmware-zephyr-token-db-uploader) builder will update the database on a daily basis.
Race conditions between builders are handled using [request-preconditions](https://cloud.google.com/storage/docs/request-preconditions).
This allows multiple builders (such as firmware branches) to run the same recipe
to fetch, merge, and upload the database to GCS.

## Token Collisions

TODO(b/287267896)
Upon CQ submission, LUCI will identify when collisions occur and notify the
developer to alter their log statement.


[`pw-tokenize`]: ../../zephyr/snippets/pw-tokenize/snippet.yml
