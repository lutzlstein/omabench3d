import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// OmaBench bar toggle. Left click: start / stop the benchmark.
// Right click: open the results folder. All work happens in the detached
// benchmark process via bin/omabench-ctl; QML only polls a small status JSON.
BarWidget {
  id: root
  moduleName: "io.github.lutzlstein.omabench"

  // ---- Config model (shell.json settings; panel UI will edit these later).
  readonly property string appPath: String(setting("appPath", "~/Projects/omabench/Packaged/Linux/OmaBench.sh"))
  readonly property string resolution: String(setting("resolution", "native"))
  readonly property int passes: Math.max(1, Math.min(5, Number(setting("passes", 1)) || 1))
  readonly property bool fullscreen: setting("fullscreen", true) === true
  readonly property bool quick: setting("quick", false) === true
  readonly property string tests: String(setting("tests", "T1"))
  // Monitor to run on: "auto" picks the highest-refresh output (DP-3 here).
  readonly property string monitor: String(setting("monitor", "auto"))

  // Test catalogue; a future panel lists these for selection.
  readonly property var testCatalog: [
    { id: "T1", name: "City Raster", description: "Lumen + fog, procedural city flythrough" }
  ]

  readonly property string ctl: String(Qt.resolvedUrl("bin/omabench-ctl")).replace(/^file:\/\//, "")

  // "checking" | "missing" | "idle" | "starting" | "running" | "error"
  property string benchState: "checking"
  property var progress: null
  property var lastScore: null
  property string lastError: ""
  property double startRequestedAt: 0

  readonly property bool running: benchState === "running" || benchState === "starting"
  // Terminal-style label: "[OMABENCH] 27515" idle, "> RUN ▮▮▮▯▯ 42%" while running.
  function pixelBar(pct) {
    const n = Math.max(0, Math.min(5, Math.round(pct / 20)))
    return "▮".repeat(n) + "▯".repeat(5 - n)
  }
  readonly property string label: {
    if (benchState === "missing") return "[OMABENCH] N/A"
    if (benchState === "error") return "[OMABENCH] ERR"
    if (benchState === "running" && progress && progress.totalS > 0) {
      const pct = Math.max(0, Math.min(100, Math.round(100 * progress.elapsedS / progress.totalS)))
      return "> RUN " + pixelBar(pct) + " " + pct + "%"
    }
    if (running) return "> BOOT_"
    return lastScore !== null ? "[OMABENCH] " + lastScore : "[OMABENCH] _"
  }

  function refresh() { if (!statusProc.running) statusProc.running = true }

  function toggle() {
    if (benchState === "missing") { refresh(); return }
    if (running) {
      Quickshell.execDetached([ctl, "stop"])
      benchState = "idle"
    } else {
      benchState = "starting"
      startRequestedAt = Date.now()
      Quickshell.execDetached([ctl, "start", appPath, tests, resolution, String(passes),
        fullscreen ? "1" : "0", quick ? "1" : "0", "", "", monitor])
    }
    pollTimer.restart()
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.vertical ? (root.running ? "▮" : "▣") : root.label
    // Phosphor accent while a run is live; bar foreground otherwise (theme tokens, no literals).
    foreground: root.running ? Commons.Color.accent : (root.bar ? root.bar.barForeground : Commons.Color.foreground)
    tooltipText: {
      if (root.benchState === "missing") return "OMABENCH :: NOT INSTALLED AT " + root.appPath
      if (root.running) return "OmaBench running" + (root.progress ? " (pass " + root.progress.pass + "/" + root.progress.passes + ")" : "") + " — click to stop"
      return "> RUN OMABENCH" + (root.lastScore !== null ? " · last GPU score " + root.lastScore : "") + "\nRight-click: open results"
    }
    onPressed: function(mouseButton) {
      if (mouseButton === Qt.LeftButton) root.toggle()
      else if (mouseButton === Qt.RightButton)
        Quickshell.execDetached(["xdg-open", Quickshell.env("HOME") + "/.local/share/omabench/results"])
    }
  }

  Timer {
    id: pollTimer
    interval: root.running ? 1500 : 15000
    repeat: true
    running: true
    triggeredOnStart: true
    onTriggered: root.refresh()
  }

  Process {
    id: statusProc
    command: [root.ctl, "status", root.appPath]
    stdout: StdioCollector {
      onStreamFinished: {
        try {
          const s = JSON.parse(String(text).slice(0, 8192))
          root.lastError = ""
          root.progress = s.progress
          root.lastScore = s.lastScore
          if (!s.installed) root.benchState = "missing"
          else if (s.running) root.benchState = "running"
          // Give the launcher a few seconds to write its pid before showing idle.
          else if (root.benchState === "starting" && Date.now() - root.startRequestedAt < 5000) {}
          else root.benchState = "idle"
        } catch (e) {
          root.benchState = "error"
          root.lastError = "bad status output"
        }
      }
    }
  }
}
