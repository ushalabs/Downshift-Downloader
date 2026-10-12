# IDM comparison status

Phase 8 inspected the installed `IDMan.exe`, version `6, 42, 63, 3`, executable SHA-256 `27ca6f93b7c9fa368012f66ed5a9aa3916cb523a5562ec3d8ee289029858cf63`. A fair automated comparison first needs IDM to complete a controlled file through its [documented CLI](https://www.internetdownloadmanager.com/support/command_line.html).

The availability probe uses a 4 MiB deterministic HTTP file on a private loopback server, a private output folder, the documented `/d`, `/p`, `/f`, and `/n` parameters, and independent SHA-256 validation. It tests both the parameter order shown in the documentation and the `/n /d` example order. It does not start IDM's scheduler, alter connection/settings limits, install a different version, or change browser integration. Failed launchers created by the probe are closed by their own `Popen` PIDs; pre-existing instances are not closed.

Neither final attempt contacted the fixture with GET/HEAD or produced a file within 30 seconds. Those launchers were still running at timeout and were cleaned up. The earlier simple probe exited with code 2 and no requests/file. These observations establish that this installed copy could not perform the unattended comparison in this session; they do not establish a licensing, network, or configuration cause. No IDM throughput, CPU, memory, or superiority number is reported.

The raw final record is [phase8-idm-final-availability-20261012.json](../benchmarks/results/phase8-idm-final-availability-20261012.json). The earlier availability record is also retained, and an identified test launcher left by that early harness was subsequently closed. The final reproducible helper cleans up its own failed launcher before leaving the temporary fixture. Reproduce with a working installation from the repository root:

```powershell
python scripts/ProbeIdm.py --output build/idm-new-availability.json
```

If that record reports `available_for_benchmark: true`, a later fair throughput comparison can use the same sizes/shaping, sequential alternating runs, at least three repetitions, full completed-file SHA-256 verification, actual observed connection counts, and explicit IDM configuration/startup/verification differences. A 4 MiB availability probe is not a throughput benchmark. Downshift's current paired measurements compare its Phase 7 and Phase 8 builds only.
