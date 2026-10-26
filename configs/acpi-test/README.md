# QEMU ACPI positive table

`qemu-positive.asl` adds one test I2C controller and one I2C, SPI, and UART endpoint under `\_SB.PCI0`. It also covers static and method `_STA`, static and method `_CRS`, PCI Config and System Memory fields, `_DSM`, `_DSD`, level and edge GPIO interrupt resources, a GPIO I/O resource, one absent device, endpoint `_DEP` relationships, and a conditional `_DEP` attached to an unconditional controller. The I2C endpoint follows the physical ALPS touchpad AML shape: it reopens its controller through an absolute `Scope`, then returns separate `SBFB` and `SBFG` resource buffers through `ConcatenateResTemplate`.

Build Mecocoa and run with the table injected:

```sh
make arch=atx-x86-flap32 run acpi-test=1
make arch=atx-x64-uefi64 run acpi-test=1
```

Inject the table into an existing image without rebuilding Mecocoa:

```sh
make arch=atx-x86-flap32 run-only acpi-test=1
make arch=atx-x64-uefi64 run-only acpi-test=1
```

The shell environment form `ACPI_TEST=1 make ...` is also supported. Without either setting, `run` and `run-only` keep their normal QEMU configuration.

Build only the AML table:

```sh
make acpi-test-table
```

The build uses `iasl` when available and otherwise uses the fixed-table Python generator in this directory. It writes `qemu-positive.aml` under `$(uobjpath)/acpi-test`; no generated AML file is stored in the repository.

Expected deltas from the same QEMU image without this table are:

- `tables`: +1
- `devices`: +14, with thirteen present and one absent
- `serial`: +3, split into one I2C, one SPI, and one UART resource
- `gpio-int`: +2
- `gpio-io`: +1
- successful PCI Config reads: at least +1
- successful System Memory reads: at least +1

The endpoint lines identify `MTPD` as `ALPS0001` at I2C address `0x2C`, `MSPD` at SPI chip select 1, and `MURT` at 115200 baud. Exact global totals depend on QEMU firmware.

The GPIO decoder test checks the descriptor layout, PinTable access, ResourceSource termination, VendorData bounds, revision handling and duplicate pins. The summary confirms that all three synthetic GPIO resources resolve to `\_SB_.MGPO` and that their endpoint `_DEP` relationships are satisfied:

```text
[ACPI-GPIO-TEST] decode=success pin=success offset=success missing-source=success source-end=success vendor=success revision=success duplicate-pin=success path=success duplicate-resource=success dependency=success
[ACPI-GPIO-SUM] descriptors=3 pins=3 valid=3 invalid=0 dep-ok=3 dep-none=0 dep-unmet=0 example=\_SB_.PCI0.MI2C.MTPD status=success dependency=satisfied
[ACPI-GPIO-NODE-SUM] controller=1 endpoint=1 linked=3 skipped=0
[GPIO-CONTROLLER-TEST] capability=success claim=success release=success duplicate=success range=success conflict=success input=success output=success pending=success mask=success unmask=success acknowledge=success firmware=success waiting=success recovery=success
```

The table also supplies methods for the generic AML invocation test. A successful run prints one compact line:

```text
[ACPI-CALL-TEST] zero=success seven=success integer=success string=success buffer=success package=success nested=success reference=success serialized=success lifetime=success unsupported=success not-found=success timeout=success type=success depth=success recovery=success
```

The same table validates the generic `_DSM` wrapper, including UUID encoding, revision-specific Function 0 masks, typed values, unsupported functions, malformed firmware results, and recovery:

```text
[ACPI-DSM-TEST] query=success integer=success buffer=success package=success uuid=success revision=success unsupported=success malformed=success type=success missing=success recovery=success
```

The synthetic `PNP0C50` endpoint also produces one HID-over-I2C summary. The descriptor register is returned by firmware and is not a driver constant:

```text
[ACPI-DSM-SUM] hid-i2c=1 success=1 unsupported=0 failed=0 example=\_SB_.PCI0.MI2C.MTPD status=success register=0020
```

After the DesignWare model is bound, the same endpoint performs the real DeviceNode transaction used for the physical probe. It writes the two-byte descriptor register, issues a repeated START, reads 30 bytes and validates the generic HID-over-I2C descriptor:

```text
[I2C-HID-PROBE] status=success i2c=success descriptor=success endpoint=\_SB_.PCI0.MI2C.MTPD address=002C controller=0000:00:00.0 register=0020 messages=2 bytes=32 abort=00000000 length=30 version=0100 vid=1234 pid=5678
```

The `_DSD` cases cover a named package, a zero-argument method, multiple UUID descriptors, integer, string, buffer, array and device-reference values, duplicate properties, malformed packages, owned DeviceNode storage and `_CRS` precedence. Buffer values under the Device Properties UUID are retained for firmware compatibility and marked nonstandard:

```text
[ACPI-DSD-TEST] integer=success string=success buffer=success array=success reference=success uuid=success method=success duplicate=success descriptor=success uuid-length=success malformed=success missing=success lifetime=success node=success precedence=success recovery=success
[ACPI-DSD-SUM] checked=6 present=1 properties=6 nonstandard=1 failed=0
```

The synthetic I2C endpoint also enables the DesignWare initialization model. The model requires the endpoint to use `ALPS0001` and its controller to use the test-only `MCOA0001`, so a physical `ALPS0001` device does not activate it. A successful run prints one compact line:

```text
[I2C-DW-TEST] fast=success standard=success apl=success disable-timeout=success enable-timeout=success write=success restart-read=success fifo=success nack=success transfer-timeout=success route=success contract-a=success contract-b=success missing-clock=success
```

`apl=success` verifies the 133 MHz Apollo Lake timing calculation. `route=success` verifies that the ACPI-created endpoint resolves its I2C address and connection speed, reaches the controller attached to its parent bus, and transfers through `Devsman::Send` and `Devsman::Read`.

`contract-a` and `contract-b` initialize the same DesignWare core through different clock and register-reset descriptions. `missing-clock` verifies that contract validation reports the missing clock before any controller register access.

The same test-only controller enables the declarative driver matcher test:

```text
[DRIVER-MATCH-TEST] acpi=success pci-priority=success class=success missing-resource=success ambiguous=success capability=success
```

This covers ACPI HID, exact PCI ID over a lower-priority PCI class candidate, class-only matching, required resources, equal-priority ambiguity, and capability rejection.

## Firmware namespace replay

An `acpidump` directory can be replayed in QEMU without changing or booting the physical machine:

```sh
make arch=atx-x86-flap32 run-only acpi-dump=/mnt/hgfs/tmp/acpi-dump
make arch=atx-x64-uefi64 run-only acpi-dump=/mnt/hgfs/tmp/acpi-dump
```

The shell environment form `ACPI_DUMP=/mnt/hgfs/tmp/acpi-dump make ...` is also supported. The replay converts the dumped DSDT into an SSDT under `$(uobjpath)/acpi-test` and injects it together with every static SSDT. This preserves the firmware AML namespace while retaining QEMU's own FADT and hardware tables. Do not combine `acpi-dump` with `acpi-test=1`.

QEMU does not provide the physical PCI devices named by the firmware tables. The replay validates AML loading, status/resource evaluation, serial resource discovery, and controller path resolution. A `pci-node-missing` result is expected when the corresponding QEMU PCI function is absent.
