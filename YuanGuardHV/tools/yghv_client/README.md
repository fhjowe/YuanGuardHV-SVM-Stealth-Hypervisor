# YuanGuardHV Java/JNI IOCTL client

Build:

```
build.bat
```

Usage (all commands require the yuanguard driver to be loaded):

```
run.bat state
run.bat set-target <pid>
run.bat add-page <hex_va>
run.bat remove-page <hex_va>
run.bat start
run.bat stop
run.bat list-java
run.bat protect <pid> [maxPages]
```

`protect` sets the target, enumerates committed pages of the real Java process,
adds up to `maxPages` (driver cap is 64), starts protection and prints state.

Boundary: the current hypervisor runs a synthetic resident guest; real process
code still executes natively, so NPT/vmmcall interception of the real process
requires the later OS-as-guest milestone. This client validates the real-target
configuration, page translation and arming path on the physical machine.
