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
  readonly property string label: {
    if (benchState === "missing") return "󰓅 n/a"
    if (benchState === "running" && progress && progress.totalS > 0)
      return "󰓅 " + Math.round(100 * progress.elapsedS / progress.totalS) + "%"
    if (running) return "󰓅 …"
    return lastScore !== null ? "󰓅 " + lastScore : "󰓅"
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
        fullscreen ? "1" : "0", quick ? "1" : "0"])
    }
    pollTimer.restart()
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.vertical ? "󰓅" : root.label
    tooltipText: {
      if (root.benchState === "missing") return "OmaBench not installed at " + root.appPath
      if (root.running) return "OmaBench running" + (root.progress ? " (pass " + root.progress.pass + "/" + root.progress.passes + ")" : "") + " — click to stop"
      return "Run OmaBench" + (root.lastScore !== null ? " · last GPU score " + root.lastScore : "") + "\nRight-click: open results"
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
