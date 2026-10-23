#!/bin/bash
# TransFrame PRD end-to-end acceptance suite.
# Drives the real installed app via Accessibility + synthesized input,
# asserts outcomes deterministically (window counts, AX titles, pixel bbox).
# Usage: bash scripts/e2e/run_e2e.sh [scenario...]
#   scenarios: menu ocr line text square all (default all)
set -u
cd "$(git rev-parse --show-toplevel 2>/dev/null || dirname "$0")"
EV=scripts/e2e/eventsynth
NOTE=/tmp/tf-e2e/note.txt
SHOT=/tmp/tf-e2e
mkdir -p "$SHOT"
WANT_NOTE='TRANSFRAME E2E 424242
line two content
'
if [ ! -f "$NOTE" ] || [ "$(cat "$NOTE" 2>/dev/null)" != "$WANT_NOTE" ]; then
  printf '%s' "$WANT_NOTE" > "$NOTE"
fi

PASS=0; FAIL=0; FAILED=()
WANT_EXTRA=0
case "$*" in *all*|*extra*) WANT_EXTRA=1 ;; esac
ok()   { PASS=$((PASS+1)); echo "  PASS  $1"; }
bad()  { FAIL=$((FAIL+1)); FAILED+=("$1"); echo "  FAIL  $1"; }
check(){ if [ "$2" = "$3" ] || { [ "$4" = "contains" ] && [[ "$2" == *"$3"* ]]; }; then ok "$1"; else bad "$1 (got: $2, want: $3)"; fi; }

# ---------- AX helpers (JXA) ----------
menu_titles() {
  osascript <<'AS'
tell application "System Events" to tell process "TransFrame"
  click menu bar item 1 of menu bar 2
  delay 0.35
  set names to name of every menu item of menu 1 of menu bar item 1 of menu bar 2
  key code 53
  return names
end tell
AS
}

# Wait until System Events can see the TransFrame process
wait_app() {
  for _ in $(seq 1 20); do
    N=$(osascript -l JavaScript 2>/dev/null <<'JXA'
function run() {
  try { return String(Application("System Events").applicationProcesses.byName("TransFrame").windows().length >= 0); }
  catch (e) { return "false"; }
}
JXA
)
    [ "$N" = "true" ] && return 0
    sleep 0.5
  done
  return 1
}

trigger_capture() {
  osascript >/dev/null <<'AS'
tell application "System Events" to tell process "TransFrame"
  click menu bar item 1 of menu bar 2
  delay 0.35
  click menu item "区域截图" of menu 1 of menu bar item 1 of menu bar 2
  return ""
end tell
AS
  sleep 1.2
}

count_windows() {
  osascript -l JavaScript <<'JXA'
function run() {
  return Application("System Events").applicationProcesses.byName("TransFrame").windows().length;
}
JXA
}

overlay_buttons() {
  osascript -l JavaScript <<'JXA'
function run() {
  const se = Application("System Events");
  const proc = se.applicationProcesses.byName("TransFrame");
  const out = [];
  for (const win of proc.windows()) {
    for (const el of win.entireContents()) {
      try {
        const role = el.role();
        if (role === "AXButton" || role === "AXRadioButton") {
          const n = String(el.name() || "");
          const d = String(el.description() || "");
          out.push((n && n !== "null" ? n : "") + "||" + d);
        }
      } catch (e) {}
    }
  }
  return out.join("\n");
}
JXA
}

press_button() {
  # $1 = AXIdentifier/name substring to match;  $2 = optional max Y filter (float)
  osascript -l JavaScript - "$1" "${2:-}" <<'JXA'
function run(argv) {
  const want = argv[0];
  const yMax = argv[1] ? parseFloat(argv[1]) : Infinity;
  const se = Application("System Events");
  const proc = se.applicationProcesses.byName("TransFrame");
  for (const win of proc.windows()) {
    const stack = [win];
    while (stack.length) {
      const el = stack.pop();
      try {
        const role = el.role();
        if (role === "AXButton" || role === "AXRadioButton") {
          let id = "";
          try { for (const a of el.attributes()) { if (a.name() === "AXIdentifier") id = String(a.value()); } } catch (e) {}
          const label = id + " " + String(el.name() || "") + " " + String(el.description() || "");
          const matched = want.indexOf("id=") === 0 ? id === want.slice(3) : label.indexOf(want) !== -1;
          if (matched) {
            let y = -1;
            try { y = parseFloat(String(el.position()).split(",")[1]); } catch (e) {}
            if (y < yMax) {
              try {
                for (const a of el.actions()) {
                  if (String(a.name()) === "AXPress") {
                    a.perform();
                    return "pressed:" + id;
                  }
                }
                el.press();
                return "pressed:" + id;
              } catch (e) {
                return "pressfail:" + e;
              }
            }
          }
        }
      } catch (e) {}
      try { for (const k of el.uiElements()) stack.push(k); } catch (e) {}
    }
  }
  return "notfound:" + want;
}
JXA
}

# Prints "x y w h" of the button with the given AXIdentifier
button_frame() {
  osascript -l JavaScript - "$1" "${2:-}" <<'JXA'
function run(argv) {
  const want = argv[0];
  const yMax = argv[1] ? parseFloat(argv[1]) : Infinity;
  const se = Application("System Events");
  const proc = se.applicationProcesses.byName("TransFrame");
  for (const win of proc.windows()) {
    const stack = [win];
    while (stack.length) {
      const el = stack.pop();
      try {
        if (el.role() === "AXButton") {
          let id = "";
          try { for (const a of el.attributes()) { if (a.name() === "AXIdentifier") id = String(a.value()); } } catch (e) {}
          if (id === want) {
            let pos = "0,0", size = "0,0";
            try { for (const a of el.attributes()) {
              if (a.name() === "AXPosition") pos = String(a.value()).replace(" ", "");
              if (a.name() === "AXSize") size = String(a.value()).replace(" ", "");
            } } catch (e) {}
            const p = pos.split(","); const sz = size.split(",");
            const py = parseFloat(p[1]);
            if (py < yMax) {
              return p[0].replace(",", " ") + " " + p[1] + " " + sz[0].replace(",", " ") + " " + sz[1];
            }
          }
        }
      } catch (e) {}
      try { for (const k of el.uiElements()) stack.push(k); } catch (e) {}
    }
  }
  return "";
}
JXA
}

# Opens the known note in TextEdit at a fixed spot, raises it above all
# windows and prints its window frame as "x y w h".
setup_textedit() {
  open -a TextEdit "$NOTE" 2>/dev/null
  sleep 1.5
  # Dismiss save prompts so they never pollute screenshots/OCR
  osascript >/dev/null 2>&1 <<'AS' || true
tell application "System Events" to tell process "TextEdit"
  repeat with w in windows
    repeat with b in buttons of w
      if (name of b) is "复原" or (name of b) is "Revert" or (name of b) is "Don\'t Save" then
        click b
      end if
    end repeat
  end repeat
end tell
AS
  osascript >/dev/null 2>&1 <<'AS' || true
tell application "TextEdit" to activate
delay 0.3
tell application "System Events"
  tell process "TextEdit"
    set frontmost to true
    if (count of windows) > 0 then
      set position of window 1 to {260, 180}
      set size of window 1 to {720, 420}
      perform action "AXRaise" of window 1
    end if
  end tell
end tell
tell application "TextEdit" to activate
AS
  sleep 0.8
  local FRAME
  FRAME=$(osascript -l JavaScript <<'JXA' 2>/dev/null
function run() {
  try {
    const se = Application("System Events");
    const proc = se.applicationProcesses.byName("TextEdit");
    if (proc.windows().length === 0) return "260 180 720 420";
    const w = proc.windows()[0];
    let pos = "260,180", size = "720,420";
    for (const a of w.attributes()) {
      if (a.name() === "AXPosition") pos = String(a.value()).replace(/ /g, "");
      if (a.name() === "AXSize") size = String(a.value()).replace(/ /g, "");
    }
    return pos.replace(",", " ") + " " + size.replace(",", " ");
  } catch (e) {
    return "260 180 720 420";
  }
}
JXA
)
  echo "${FRAME:-260 180 720 420}"
}

finish_overlay() { ./scripts/e2e/eventsynth key return; sleep 0.8; }

# Cancel any leftover overlay so annotations from earlier runs don't leak
reset_overlay() {
  wait_app || true
  ./scripts/e2e/eventsynth key escape; sleep 0.4
  ./scripts/e2e/eventsynth key escape; sleep 0.4
  osascript -l JavaScript >/dev/null <<'JXA'
function run() {
  const se = Application("System Events");
  const proc = se.applicationProcesses.byName("TransFrame");
  let n = 0;
  try { n = proc.windows().length; } catch (e) { return "0"; }
  return String(n);
}
JXA
  W=$(osascript -l JavaScript <<'JXA'
function run() {
  try { return String(Application("System Events").applicationProcesses.byName("TransFrame").windows().length); }
  catch (e) { return "0"; }
}
JXA
)
  if [ "$W" -gt 1 ]; then
    press_button "id=xmark" >/dev/null 2>&1 || true
    sleep 0.5
  fi
}

# ---------- Scenarios ----------
S=("${@:-all}")
case "${S[0]}" in all) S=(menu ocr line text square) ;; esac

for sc in "${S[@]}"; do
echo "=== SCENARIO: $sc ==="
reset_overlay
case "$sc" in

menu)
  TITLES=$(menu_titles)
  echo "  menu: $TITLES"
  check "菜单含屏幕取色" "$TITLES" "屏幕取色" contains
  check "菜单含打开图片编辑" "$TITLES" "打开图片编辑..." contains
  check "菜单含区域截图" "$TITLES" "区域截图" contains
  case "$TITLES" in *menu.*) bad "菜单无裸键名 ($TITLES)" ;; *) ok "菜单无裸键名" ;; esac
  ;;

ocr)
  TF=$(setup_textedit)
  [ -n "$TF" ] || TF="260 180 720 420"
  set -- $TF
  SELX=$(( $1 + 4 )); SELY=$(( $2 + 30 )); SELW=$(( $3 - 8 )); SELH=$(( $4 - 60 ))
  BEFORE=$(count_windows)
  trigger_capture
  $EV drag $SELX $SELY $((SELX + SELW)) $((SELY + SELH))
  sleep 1
  W1=$(count_windows)
  [ "$W1" -gt "$BEFORE" ] && ok "选区 overlay 已出现 ($W1 > $BEFORE)" || bad "选区 overlay 未出现"
  R=$(press_button "id=text.viewfinder")
  echo "  press OCR -> $R"
  case "$R" in notfound*) bad "找到 OCR 按钮" ;; *) ok "找到并按下 OCR 按钮" ;; esac
  sleep 4
  W2=$(count_windows)
  [ "$W2" = "$W1" ] && ok "OCR 后窗口数不变（无外部新窗口）" || bad "OCR 弹出了新窗口 ($W1 -> $W2)"
  # presence check without pressing: search drawer close (xmark below toolbar row)
  FOUND=$(osascript -l JavaScript <<'JXA'
function run() {
  const se = Application("System Events");
  const proc = se.applicationProcesses.byName("TransFrame");
  for (const win of proc.windows()) {
    const stack = [win];
    while (stack.length) {
      const el = stack.pop();
      try {
        if (el.role() === "AXButton") {
          let id = "";
          try { for (const a of el.attributes()) { if (a.name() === "AXIdentifier") id = String(a.value()); } } catch (e) {}
          if (id === "xmark") {
            let y = -1;
            try { y = parseFloat(String(el.position()).split(",")[1]); } catch (e) {}
            if (y < 550) return "drawer-close-present";
          }
        }
      } catch (e) {}
      try { for (const k of el.uiElements()) stack.push(k); } catch (e) {}
    }
  }
  return "drawer-close-absent";
}
JXA
)
  [ "$FOUND" = "drawer-close-present" ] && ok "内嵌面板出现（含关闭按钮）" || bad "内嵌面板未出现 ($FOUND)"
  TXT=$(osascript -l JavaScript <<'JXA'
function run() {
  const se = Application("System Events");
  const proc = se.applicationProcesses.byName("TransFrame");
  const out = [];
  for (const win of proc.windows()) {
    const stack = [win];
    while (stack.length) {
      const el = stack.pop();
      try {
        const role = el.role();
        if (role === "AXStaticText" || role === "AXTextArea" || role === "AXTextField") {
          const v = String(el.value() || "") + " " + String(el.name() || "");
          if (v.trim() && v.trim() !== "null") out.push(v.trim());
        }
      } catch (e) {}
      try { for (const k of el.uiElements()) stack.push(k); } catch (e) {}
    }
  }
  return out.join(" | ");
}
JXA
)
  echo "  texts: $TXT"
  echo "$TXT" | grep -q "424242" && ok "面板含 OCR 识别文本 (424242)" || bad "OCR 结果未显示预期文本: $TXT"
  R=$(press_button "id=xmark" 550)
  case "$R" in notfound*) bad "按下关闭按钮" ;; *) ok "关闭面板" ;; esac
  sleep 0.5
  press_button "xmark" 550 | grep -q notfound && ok "面板已收起" || bad "面板未收起"
  finish_overlay
  ;;

line)
  TF=$(setup_textedit)
  [ -n "$TF" ] || TF="260 180 720 420"
  set -- $TF
  SELX=$(( $1 + 4 )); SELY=$(( $2 + 30 )); SELW=$(( $3 - 8 )); SELH=$(( $4 - 60 ))
  trigger_capture
  $EV drag $SELX $SELY $((SELX + SELW)) $((SELY + SELH))
  sleep 1
  $EV shot 320 260 600 260 "$SHOT/line-before.png"
  R=$(press_button "id=line.diagonal")
  echo "  press line tool -> $R"
  case "$R" in notfound*) bad "找到直线工具" ;; *) ok "找到并激活直线工具" ;; esac
  $EV drag $((SELX + 80)) $((SELY + 50)) $((SELX + 540)) $((SELY + 210))
  sleep 0.6
  $EV shot 320 260 600 260 "$SHOT/line-after.png"
  D=$($EV diff "$SHOT/line-before.png" "$SHOT/line-after.png")
  echo "  pixel diff: $D"
  awk -v d="$D" 'BEGIN{exit !(d > 0.0005)}' && ok "直线绘制产生像素变化" || bad "直线绘制无变化 (diff=$D)"
  finish_overlay
  ;;

text)
  TF=$(setup_textedit)
  [ -n "$TF" ] || TF="260 180 720 420"
  set -- $TF
  SELX=$(( $1 + 4 )); SELY=$(( $2 + 30 )); SELW=$(( $3 - 8 )); SELH=$(( $4 - 60 ))
  trigger_capture
  $EV drag $SELX $SELY $((SELX + SELW)) $((SELY + SELH))
  sleep 1
  $EV shot 320 260 600 260 "$SHOT/text-before.png"
  R=$(press_button "id=textbox")
  echo "  press text tool -> $R"
  case "$R" in notfound*) bad "找到文本工具" ;; *) ok "找到并激活文本工具" ;; esac
  $EV click $((SELX + 120)) $((SELY + 70))
  sleep 0.6
  $EV type "E2ETEXT"
  sleep 0.3
  $EV click $((SELX + 420)) $((SELY + 250))
  sleep 0.8
  $EV shot 320 260 600 260 "$SHOT/text-after.png"
  D=$($EV diff "$SHOT/text-before.png" "$SHOT/text-after.png")
  echo "  pixel diff after commit: $D"
  awk -v d="$D" 'BEGIN{exit !(d > 0.0005)}' && ok "文本提交后画布有内容" || bad "文本未提交 (diff=$D)"
  # drag the committed text box and confirm the pixels move
  $EV drag $((SELX + 120)) $((SELY + 70)) $((SELX + 240)) $((SELY + 170))
  sleep 0.6
  $EV shot 320 260 600 260 "$SHOT/text-moved.png"
  D2=$($EV diff "$SHOT/text-after.png" "$SHOT/text-moved.png")
  echo "  pixel diff after drag: $D2"
  awk -v d="$D2" 'BEGIN{exit !(d > 0.0003)}' && ok "文本框可拖动" || bad "文本框拖动无变化 (diff=$D2)"
  finish_overlay
  ;;

square)
  TF=$(setup_textedit)
  [ -n "$TF" ] || TF="260 180 720 420"
  set -- $TF
  SELX=$(( $1 + 4 )); SELY=$(( $2 + 30 )); SELW=$(( $3 - 8 )); SELH=$(( $4 - 60 ))
  trigger_capture
  $EV drag $SELX $SELY $((SELX + SELW)) $((SELY + SELH))
  sleep 1
  R=$(press_button "id=rectangle")
  echo "  press rect tool -> $R"
  case "$R" in notfound*) bad "找到矩形工具" ;; *) ok "找到并激活矩形工具" ;; esac
  # dismiss any system dialogs (e.g. Keychain prompts) before measuring
  osascript -l JavaScript >/dev/null <<'JXA'
function run() {
  const se = Application("System Events");
  for (const p of se.applicationProcesses()) {
    try {
      for (const w of p.windows()) {
        for (const b of w.buttons()) {
          const n = String(b.name() || "");
          if (n === "取消" || n === "Cancel") {
            try { for (const a of b.actions()) { if (String(a.name()) === "AXPress") a.perform(); } } catch (e) {}
            return "dismissed";
          }
        }
      }
    } catch (e) {}
  }
  return "none";
}
JXA
  $EV drag $((SELX + 340)) $((SELY + 50)) $((SELX + 560)) $((SELY + 180)) cmd
  sleep 0.6
  # tight region around the expected draw area (avoid stray UI pixels)
  $EV shot 590 270 300 250 "$SHOT/square.png"
  BBOX=$($EV redbbox "$SHOT/square.png")
  echo "  red bbox: $BBOX"
  if [ "$BBOX" = "NONE" ]; then
    bad "⌘ 绘制未出现红色矩形"
  else
    set -- $BBOX
    W=$3; H=$4
    DIFF=$(( W > H ? W - H : H - W ))
    echo "  w=$W h=$H diff=$DIFF"
    [ "$DIFF" -le 4 ] && ok "⌘ 绘制保持正方形 (|w-h|=$DIFF <= 4)" || bad "⌘ 未锁定正方形 (w=$W h=$H)"
  fi
  finish_overlay
  ;;
esac
done

# --- Scenario: edge-click (user report 1/3) ---
run_edge_click() {
  echo "=== SCENARIO: edge-click ==="
  reset_overlay
  TF=$(setup_textedit)
  [ -n "$TF" ] || TF="260 180 720 420"
  set -- $TF
  SELX=$(( $1 + 4 )); SELY=$(( $2 + 30 )); SELW=$(( $3 - 8 )); SELH=$(( $4 - 60 ))
  trigger_capture
  $EV drag $SELX $SELY $((SELX + SELW)) $((SELY + SELH))
  sleep 1.2
  FRAME=$(button_frame "line.diagonal")
  if [ -z "$FRAME" ]; then
    bad "读取直线按钮位置"
    return
  fi
  set -- $FRAME
  BX=$1; BY=$2; BW=$3; BH=$4
  # click 3pt above the icon visual (old hit area leaked here and cancelled the selection)
  EDGE_X=$(( BX + BW / 2 ))
  EDGE_Y=$(( BY - 3 ))
  # 1) click the TOP EDGE of the line button (1pt inside its frame): must activate
  B0=$(osascript -l JavaScript <<'JXA'
function run() {
  const se = Application("System Events");
  const proc = se.applicationProcesses.byName("TransFrame");
  let n = 0;
  for (const win of proc.windows()) {
    const stack = [win];
    while (stack.length) {
      const el = stack.pop();
      try { if (el.role() === "AXButton" || el.role() === "AXRadioButton") n++; } catch (e) {}
      try { for (const k of el.uiElements()) stack.push(k); } catch (e) {}
    }
  }
  return String(n);
}
JXA
)
  $EV click $((BX + BW / 2)) $((BY + 1))
  sleep 0.6
  B1=$(osascript -l JavaScript <<'JXA'
function run() {
  const se = Application("System Events");
  const proc = se.applicationProcesses.byName("TransFrame");
  let n = 0;
  for (const win of proc.windows()) {
    const stack = [win];
    while (stack.length) {
      const el = stack.pop();
      try { if (el.role() === "AXButton" || el.role() === "AXRadioButton") n++; } catch (e) {}
      try { for (const k of el.uiElements()) stack.push(k); } catch (e) {}
    }
  }
  return String(n);
}
JXA
)
  echo "  edge-click y=BY+1: buttons $B0 -> $B1"
  [ "$B1" -gt "$B0" ] && ok "按钮边缘点击激活工具" || bad "按钮边缘点击未激活 ($B0->$B1)"
  # drawing with the activated tool produces a localized change
  BX1=$((SELX + 80)); BY1=$((SELY + 50)); BX2=$((SELX + 540)); BY2=$((SELY + 210))
  $EV shot $((BX1 - 20)) $((BY1 - 20)) $((BX2 - BX1 + 40)) $((BY2 - BY1 + 40)) "$SHOT/edge-before.png"
  $EV drag $BX1 $BY1 $BX2 $BY2
  sleep 0.6
  $EV shot $((BX1 - 20)) $((BY1 - 20)) $((BX2 - BX1 + 40)) $((BY2 - BY1 + 40)) "$SHOT/edge-after.png"
  D=$($EV diff "$SHOT/edge-before.png" "$SHOT/edge-after.png")
  echo "  draw diff=$D"
  awk -v d="$D" 'BEGIN{exit !(d > 0.0005 && d < 0.3)}' && ok "边缘激活后绘制生效 (diff=$D)" || bad "绘制行为异常 (diff=$D)"
  # 2) click just OUTSIDE the button (2pt above): must be inert (no cancel)
  $EV shot $((BX - 40)) $((BY - 30)) 200 80 "$SHOT/out-before.png"
  $EV click $((BX + BW / 2)) $((BY - 2))
  sleep 0.6
  $EV shot $((BX - 40)) $((BY - 30)) 200 80 "$SHOT/out-after.png"
  OD=$($EV diff "$SHOT/out-before.png" "$SHOT/out-after.png")
  BC=$(osascript -l JavaScript <<'JXA'
function run() {
  const se = Application("System Events");
  const proc = se.applicationProcesses.byName("TransFrame");
  let n = 0;
  for (const win of proc.windows()) {
    const stack = [win];
    while (stack.length) {
      const el = stack.pop();
      try { if (el.role() === "AXButton" || el.role() === "AXRadioButton") n++; } catch (e) {}
      try { for (const k of el.uiElements()) stack.push(k); } catch (e) {}
    }
  }
  return String(n);
}
JXA
)
  echo "  outside-2pt click: buttons=$BC toolbar-area diff=$OD"
  [ "$BC" -gt 0 ] && ok "按钮外点击不取消选区（工具栏保持）" || bad "按钮外点击取消了选区 (buttons=$BC)"
  finish_overlay
}

# --- Scenario: outside-click (user report 2/3) ---
run_outside_click() {
  echo "=== SCENARIO: outside-click ==="
  reset_overlay
  TF=$(setup_textedit)
  [ -n "$TF" ] || TF="260 180 720 420"
  set -- $TF
  SELX=$(( $1 + 4 )); SELY=$(( $2 + 30 )); SELW=$(( $3 - 8 )); SELH=$(( $4 - 60 ))
  trigger_capture
  $EV drag $SELX $SELY $((SELX + SELW)) $((SELY + SELH))
  sleep 1
  # plain click far from selection and toolbar
  $EV click 1500 200
  sleep 0.8
  R=$(press_button "id=text.viewfinder")
  case "$R" in
    pressed*) ok "选区外单击后选区与工具栏保持（OCR 按钮仍在）" ;;
    *) bad "选区外单击取消了选区 ($R)" ;;
  esac
  finish_overlay
}

# --- Scenario: crossline (user report 3/3) ---
run_crossline() {
  echo "=== SCENARIO: crossline ==="
  reset_overlay
  TF=$(setup_textedit)
  [ -n "$TF" ] || TF="260 180 720 420"
  set -- $TF
  SELX=$(( $1 + 4 )); SELY=$(( $2 + 30 )); SELW=$(( $3 - 8 )); SELH=$(( $4 - 60 ))
  trigger_capture
  $EV drag $SELX $SELY $((SELX + SELW)) $((SELY + SELH))
  sleep 1
  R=$(press_button "id=text.viewfinder")
  case "$R" in notfound*) bad "打开 OCR 面板" ; return ;; esac
  sleep 4
  # drawer close button (xmark below toolbar row) anchors the drawer
  ANCHOR=$(button_frame "xmark" 550)
  # drag-select vertically across several OCR lines inside the text block
  if [ -n "$ANCHOR" ]; then
    set -- $ANCHOR
    # text block lives below the drawer header, left of the close button
    TX=$(( $1 - 200 )); TY=$(( $2 + 60 ))
    $EV drag $TX $TY $((TX + 90)) $((TY + 150))
  else
    DX=$((SELX + SELW + 20))
    $EV drag $((DX + 30)) $((SELY + 40)) $((DX + 120)) $((SELY + 160))
  fi
  sleep 0.4
  $EV copy
  sleep 0.4
  CLIP=$(pbpaste)
  LINES=$(printf '%s' "$CLIP" | wc -l | tr -d ' ')
  TOTAL=$((LINES + 1))
  echo "  clipboard lines=$TOTAL content=[$CLIP]"
  [ "$TOTAL" -ge 2 ] && [ -n "$CLIP" ] && ok "跨行选择复制了多行文本 ($TOTAL 行)" || bad "跨行选择失败 (行数=$TOTAL)"
  finish_overlay
}

if [ "$WANT_EXTRA" = "1" ]; then
  run_edge_click
  run_outside_click
  run_crossline
fi

echo ""
echo "======== E2E RESULT: PASS=$PASS FAIL=$FAIL ========"
if [ "$FAIL" -gt 0 ]; then printf 'failed: %s\n' "${FAILED[@]}"; exit 1; fi
