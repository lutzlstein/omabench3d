pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// OmaBench bar widget + terminal panel.
//   Left click   : open/close the [OMABENCH] panel (run, config, results)
//   Middle click : quick start / abort with saved settings
//   Right click  : open the results folder
// All process and file work goes through bin/omabench-ctl (argv only);
// QML just renders the small JSON it returns.
Panel {
  id: root
  moduleName: "io.github.lutzlstein.omabench"
  ipcTarget: "io.github.lutzlstein.omabench"

  // ---- Config model (inline shell.json entry) -------------------------
  readonly property string appPath: String(setting("appPath", "~/Projects/omabench/Packaged/Linux/OmaBench.sh"))
  readonly property string resolution: String(setting("resolution", "native"))
  readonly property int passes: Math.max(1, Math.min(5, Number(setting("passes", 1)) || 1))
  readonly property bool fullscreen: setting("fullscreen", true) === true
  readonly property bool quick: setting("quick", false) === true
  readonly property string tests: String(setting("tests", "T1"))
  readonly property string monitor: String(setting("monitor", "auto"))  // auto = highest refresh
  readonly property string pixel: String(setting("pixel", "auto"))
  readonly property bool hud: setting("hud", true) !== false
  readonly property string theme: String(setting("theme", "omarchy"))

  // Data-driven test catalogue: add T2/T3 here when the app ships them.
  readonly property var testCatalog: [
    { id: "T1", name: "City Night", detail: "raster · lumen · rain", available: true },
    { id: "T2", name: "Ray Tracing", detail: "coming soon", available: false },
    { id: "T3", name: "CPU Sim", detail: "coming soon", available: false }
  ]

  readonly property string ctl: String(Qt.resolvedUrl("../bin/omabench-ctl")).replace(/^file:\/\//, "")

  // ---- Live state -----------------------------------------------------
  property string benchState: "checking"  // checking|missing|idle|starting|running|error
  property var progress: null
  property var lastScore: null
  property double startRequestedAt: 0
  property var history: []
  property var monitors: []
  property var themes: []
  property int cursor: 0

  readonly property bool running: benchState === "running" || benchState === "starting"
  readonly property color fg: Commons.Color.popups.text
  readonly property color ac: Commons.Color.accent
  readonly property color dim: Commons.Color.muted
  readonly property string mono: Style.font.family
  readonly property int pct: (benchState === "running" && progress && progress.totalS > 0)
    ? Math.max(0, Math.min(100, Math.round(100 * progress.elapsedS / progress.totalS))) : 0

  function pixelBar(p, n) {
    const k = Math.max(0, Math.min(n, Math.round(p * n / 100)))
    return "▮".repeat(k) + "▯".repeat(n - k)
  }
  readonly property string label: {
    if (benchState === "missing") return "[OMABENCH] N/A"
    if (benchState === "error") return "[OMABENCH] ERR"
    if (benchState === "running" && progress && progress.totalS > 0) return "> RUN " + pixelBar(pct, 5) + " " + pct + "%"
    if (running) return "> BOOT_"
    return lastScore !== null ? "[OMABENCH] " + lastScore : "[OMABENCH] _"
  }

  // ---- Settings persistence ------------------------------------------
  function save(patch) {
    root.settings = Object.assign({}, root.settings, patch)
    if (root.bar && root.bar.shell) root.bar.shell.updateEntryInline(root.moduleName, root.settings)
  }
  function cycle(list, cur, dir) {
    let i = list.indexOf(cur); if (i < 0) i = 0
    return list[(i + dir + list.length) % list.length]
  }
  readonly property var monitorOptions: ["auto"].concat(monitors.map(m => m.name))
  readonly property var themeOptions: ["omarchy"].concat(themes)
  function monitorLabel(n) {
    if (n === "auto") return "AUTO (MAX HZ)"
    const m = monitors.find(x => x.name === n)
    return m ? n + " " + m.hz + "HZ" : n
  }

  // Rows: tests first, then config, then actions. kind: test|opt|run|open
  readonly property var rows: {
    const r = []
    for (const t of testCatalog) r.push({ kind: "test", id: t.id })
    r.push({ kind: "opt", key: "resolution", label: "RESOLUTION",
      value: ({ native: "NATIVE", "1920x1080": "1080P", "2560x1440": "1440P", "3840x2160": "4K" })[resolution] || resolution })
    r.push({ kind: "opt", key: "passes", label: "PASSES", value: String(passes) })
    r.push({ kind: "opt", key: "fullscreen", label: "FULLSCREEN", value: fullscreen ? "[X]" : "[ ]" })
    r.push({ kind: "opt", key: "quick", label: "QUICK", value: quick ? "[X]" : "[ ]" })
    r.push({ kind: "opt", key: "monitor", label: "MONITOR", value: monitorLabel(monitor) })
    r.push({ kind: "opt", key: "pixel", label: "PIXEL", value: pixel === "auto" ? "AUTO" : (pixel === "0" ? "OFF" : pixel + "X") })
    r.push({ kind: "opt", key: "hud", label: "HUD", value: hud ? "[X]" : "[ ]" })
    r.push({ kind: "opt", key: "theme", label: "THEME", value: theme === "omarchy" ? "MATCH OMARCHY" : theme.toUpperCase() })
    r.push({ kind: "run" })
    r.push({ kind: "open" })
    return r
  }

  function selectedTests() { return tests.split(",").filter(t => /^T[0-9]$/.test(t)) }
  function toggleTest(id) {
    const t = testCatalog.find(x => x.id === id)
    if (!t || !t.available || running) return
    let cur = selectedTests()
    cur = cur.indexOf(id) >= 0 ? cur.filter(x => x !== id) : cur.concat([id])
    if (cur.length === 0) return  // keep at least one test
    save({ tests: cur.join(",") })
  }
  function changeOpt(key, dir) {
    if (running) return
    if (key === "resolution") save({ resolution: cycle(["native", "1920x1080", "2560x1440", "3840x2160"], resolution, dir) })
    else if (key === "passes") save({ passes: ((passes - 1 + dir + 5) % 5) + 1 })
    else if (key === "fullscreen") save({ fullscreen: !fullscreen })
    else if (key === "quick") save({ quick: !quick })
    else if (key === "hud") save({ hud: !hud })
    else if (key === "monitor") save({ monitor: cycle(monitorOptions, monitor, dir) })
    else if (key === "pixel") save({ pixel: cycle(["auto", "0", "2", "4", "8"], pixel, dir) })
    else if (key === "theme") save({ theme: cycle(themeOptions, theme, dir) })
  }
  function activateRow(i, dir) {
    const r = rows[i]; if (!r) return
    if (r.kind === "test") toggleTest(r.id)
    else if (r.kind === "opt") changeOpt(r.key, dir)
    else if (r.kind === "run") toggleRun()
    else if (r.kind === "open") openResults()
  }

  // ---- Actions --------------------------------------------------------
  function refresh() { if (!statusProc.running) statusProc.running = true }
  function refreshDetails() {
    if (!summaryProc.running) summaryProc.running = true
    if (!monitorsProc.running) monitorsProc.running = true
    if (!themesProc.running) themesProc.running = true
  }
  function openResults() {
    Quickshell.execDetached(["xdg-open", Quickshell.env("HOME") + "/.local/share/omabench/results"])
  }
  function toggleRun() {
    if (benchState === "missing") { refresh(); return }
    if (running) {
      Quickshell.execDetached([ctl, "stop"])
      benchState = "idle"
    } else {
      benchState = "starting"
      startRequestedAt = Date.now()
      Quickshell.execDetached([ctl, "start", appPath, selectedTests().join(",") || "T1", resolution, String(passes),
        fullscreen ? "1" : "0", quick ? "1" : "0", "", "", monitor, pixel, hud ? "1" : "0", theme])
    }
    pollTimer.restart()
  }

  onOpenedChanged: if (opened) { refresh(); refreshDetails() }
  onRunningChanged: if (!running) refreshDetails()

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: (root.bar && root.bar.vertical) ? (root.running ? "▮" : "▣") : root.label
    foreground: root.running ? Commons.Color.accent : (root.bar ? root.bar.barForeground : Commons.Color.foreground)
    tooltipText: root.opened ? "" : (root.benchState === "missing"
      ? "OMABENCH :: NOT INSTALLED AT " + root.appPath
      : "> OMABENCH" + (root.lastScore !== null ? " · last GPU " + root.lastScore : "")
        + "\nClick: panel · Middle: " + (root.running ? "abort" : "run") + " · Right: results")
    onPressed: function(mouseButton) {
      if (mouseButton === Qt.LeftButton) root.toggle()
      else if (mouseButton === Qt.MiddleButton) root.toggleRun()
      else if (mouseButton === Qt.RightButton) root.openResults()
    }
  }

  // ---- Panel ----------------------------------------------------------
  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keys
    contentWidth: panel.fittedContentWidth(Style.space(420))
    contentHeight: panel.fittedContentHeight(col.implicitHeight)

    PanelKeyCatcher {
      id: keys
      anchors.fill: parent
      onMoveRequested: function(dx, dy) {
        if (dy !== 0) root.cursor = (root.cursor + dy + root.rows.length) % root.rows.length
        else root.activateRow(root.cursor, dx)
      }
      onActivateRequested: root.activateRow(root.cursor, 1)
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }

      Column {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Style.space(6)

        component Mono: Text {
          textFormat: Text.PlainText
          font.family: root.mono
          font.pixelSize: Style.font.body
          color: root.fg
          elide: Text.ElideRight
        }

        // Header
        Item {
          width: parent.width; height: hdr.implicitHeight
          Mono { id: hdr; text: "[OMABENCH]"; color: root.ac; font.bold: true; font.pixelSize: Style.font.subtitle }
          Mono {
            anchors.right: parent.right; anchors.verticalCenter: hdr.verticalCenter
            color: root.running ? root.ac : root.dim
            text: ({ checking: "BOOT…", missing: "NOT INSTALLED", idle: "READY_", starting: "STARTING…",
                     running: "RUNNING", error: "ERROR" })[root.benchState] || root.benchState
          }
        }
        Mono {
          width: parent.width; color: root.dim; font.pixelSize: Style.font.small || 10; elide: Text.ElideRight
          text: "OMB-T1 // NEXUS  ::  ESPER DIAG " + (root.history.length ? String(root.history[0].runId || "").slice(-6) : "000000")
        }
        Mono { width: parent.width; color: root.dim; text: "─".repeat(48); clip: true }

        Mono { text: "> TESTS"; color: root.dim }
        Repeater {
          model: root.rows.length
          delegate: Item {
            id: rowItem
            required property int index
            readonly property var row: root.rows[index]
            readonly property bool sel: root.cursor === index
            readonly property var test: row.kind === "test" ? root.testCatalog.find(t => t.id === row.id) : null
            visible: row.kind === "test" || row.kind === "opt"
            width: col.width; height: visible ? rowText.implicitHeight + Style.space(2) : 0
            Rectangle { anchors.fill: parent; color: root.ac; opacity: rowItem.sel ? 0.18 : (ma.containsMouse ? 0.08 : 0) }
            Mono {
              id: rowText
              anchors.left: parent.left; anchors.leftMargin: Style.space(4)
              anchors.verticalCenter: parent.verticalCenter
              width: parent.width - valText.implicitWidth - Style.space(12)
              color: rowItem.test && !rowItem.test.available ? root.dim : root.fg
              text: rowItem.test
                ? (rowItem.sel ? "▸ " : "  ") + (rowItem.test.available ? (root.selectedTests().indexOf(rowItem.test.id) >= 0 ? "[X] " : "[ ] ") : "[-] ")
                  + rowItem.test.id + " " + rowItem.test.name.toUpperCase()
                : (rowItem.sel ? "▸ " : "  ") + (rowItem.row.label || "")
            }
            Mono {
              id: valText
              anchors.right: parent.right; anchors.rightMargin: Style.space(4)
              anchors.verticalCenter: parent.verticalCenter
              color: rowItem.test ? root.dim : root.ac
              text: rowItem.test ? rowItem.test.detail : "‹ " + (rowItem.row.value || "") + " ›"
            }
            MouseArea {
              id: ma
              anchors.fill: parent; hoverEnabled: true
              acceptedButtons: Qt.LeftButton | Qt.RightButton
              onClicked: function(m) { root.cursor = rowItem.index; root.activateRow(rowItem.index, m.button === Qt.RightButton ? -1 : 1) }
            }
          }
        }

        Mono { width: parent.width; color: root.dim; text: "─".repeat(48); clip: true }

        // RUN / ABORT + progress
        Rectangle {
          id: runBtn
          readonly property bool sel: root.rows[root.cursor] && root.rows[root.cursor].kind === "run"
          width: parent.width; height: runText.implicitHeight + Style.space(14)
          color: root.running ? "transparent" : root.ac
          border.color: root.ac; border.width: Math.max(1, Style.space(2))
          opacity: root.benchState === "missing" ? 0.4 : 1
          Rectangle {  // live progress fill
            visible: root.running
            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: parent.width * root.pct / 100; color: root.ac; opacity: 0.35
          }
          Mono {
            id: runText
            anchors.centerIn: parent
            font.bold: true; font.pixelSize: Style.font.subtitle
            color: root.running ? root.ac : Commons.Color.popups.background
            text: (runBtn.sel ? "▸ " : "") + (root.benchState === "missing" ? "NOT INSTALLED"
              : root.running ? "ABORT  " + root.pixelBar(root.pct, 10) + " " + root.pct + "%"
                + (root.progress ? "  P" + root.progress.pass + "/" + root.progress.passes : "")
              : "▶ RUN")
          }
          MouseArea { anchors.fill: parent; onClicked: root.toggleRun() }
        }

        // Last result
        Mono { text: "> LAST RESULT"; color: root.dim }
        Mono {
          readonly property var h: root.history.length > 0 ? root.history[0] : null
          width: parent.width; wrapMode: Text.Wrap; elide: Text.ElideNone
          text: !h ? "  NO RUNS YET_" :
            "  SCORE " + (h.score ?? "—") + "   AVG " + (h.avgFps ?? "—") + " FPS   1% " + (h.p1LowFps ?? "—") + "\n"
            + "  " + (h.monitor || "?") + (h.refreshHz ? " " + h.refreshHz + "HZ" : "") + " · " + h.res
            + " · " + root.fmtDate(h.date) + (h.theme ? " · " + h.theme : "")
        }

        Mono { text: "> HISTORY"; color: root.dim; visible: root.history.length > 1 }
        Repeater {
          model: Math.max(0, Math.min(7, root.history.length - 1))
          delegate: Mono {
            required property int index
            readonly property var h: root.history[index + 1]
            width: col.width
            color: root.dim
            text: "  " + root.fmtDate(h.date) + "  " + String(h.score ?? "—").padStart(6) + "  " + (h.res || "") + "  " + (h.monitor || "")
          }
        }

        Mono {
          readonly property bool sel: root.rows[root.cursor] && root.rows[root.cursor].kind === "open"
          text: (sel ? "▸ " : "  ") + "[ OPEN RESULTS FOLDER ]"
          color: openMa.containsMouse || sel ? root.ac : root.fg
          MouseArea { id: openMa; anchors.fill: parent; hoverEnabled: true; onClicked: root.openResults() }
        }
        Mono { color: root.dim; font.pixelSize: Style.font.caption; text: "↑↓ select · ←→/click change · ⏎ toggle · esc close" }
      }
    }
  }

  function fmtDate(iso) {
    if (!iso) return "—"
    const d = new Date(String(iso) + "Z")
    return isNaN(d.getTime()) ? String(iso).slice(0, 16) : Qt.formatDateTime(d, "MM-dd hh:mm")
  }

  // ---- Polling / ctl processes -----------------------------------------
  Timer {
    id: pollTimer
    interval: root.running ? 1500 : 15000
    repeat: true; running: true; triggeredOnStart: true
    onTriggered: root.refresh()
  }

  Process {
    id: statusProc
    command: [root.ctl, "status", root.appPath]
    stdout: StdioCollector {
      onStreamFinished: {
        try {
          const s = JSON.parse(String(text).slice(0, 8192))
          root.progress = s.progress
          root.lastScore = s.lastScore
          if (!s.installed) root.benchState = "missing"
          else if (s.running) root.benchState = "running"
          else if (root.benchState === "starting" && Date.now() - root.startRequestedAt < 5000) {}
          else root.benchState = "idle"
        } catch (e) { root.benchState = "error" }
      }
    }
  }
  Process {
    id: summaryProc
    command: [root.ctl, "summary"]
    stdout: StdioCollector {
      onStreamFinished: {
        try { const s = JSON.parse(String(text).slice(0, 16384)); root.history = Array.isArray(s.history) ? s.history.slice(0, 8) : [] }
        catch (e) { root.history = [] }
      }
    }
  }
  Process {
    id: monitorsProc
    command: [root.ctl, "monitors"]
    stdout: StdioCollector {
      onStreamFinished: { try { const m = JSON.parse(String(text).slice(0, 4096)); root.monitors = Array.isArray(m) ? m : [] } catch (e) {} }
    }
  }
  Process {
    id: themesProc
    command: [root.ctl, "themes"]
    stdout: StdioCollector {
      onStreamFinished: { try { const t = JSON.parse(String(text).slice(0, 2048)); root.themes = Array.isArray(t) ? t : [] } catch (e) {} }
    }
  }
}
