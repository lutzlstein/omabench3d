# OmaBench3D (Omarchy shell plugin)

Bar toggle for the OmaBench3D UE5 GPU benchmark.

- **Left click**: start the benchmark (detached process) / stop a running one.
- **Right click**: open `~/.local/share/omabench3d/results`.
- Label shows `󰓅 n/a` (not installed), `󰓅 NN%` (running), or the last GPU score.

The plugin never downloads or installs the benchmark. Install the packaged app separately
and set `appPath` (default `~/Projects/omabench/Packaged/Linux/OmaBench.sh`).

Settings (in `shell.json` on the widget entry): `appPath`, `tests`, `resolution`, `passes`,
`fullscreen`, `quick`. All process work goes through `bin/omabench-ctl` (argv only, validated).

Test: `./tests/run`.
