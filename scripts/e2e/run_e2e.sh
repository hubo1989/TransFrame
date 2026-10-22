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
printf 'TRANSFRAME E2E 424242\nline two content\n' > "$NOTE"

PASS=0; FAIL=0; FAILED=()
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

setup_textedit() {
  open -a TextEdit "$NOTE" 2>/dev/null
  sleep 1.5
  osascript <<'AS'
tell application "TextEdit" to activate
tell application "System Events" to tell process "TextEdit"
  if (count of windows) > 0 then
    set position of window 1 to {260, 180}
    set size of window 1 to {720, 420}
  end if
end tell
AS
  sleep 0.5
}

finish_overlay() { ./scripts/e2e/eventsynth key return; sleep 0.8; }

# Cancel any leftover overlay so annotations from earlier runs don't leak
reset_overlay() {
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
  setup_textedit
  BEFORE=$(count_windows)
  trigger_capture
  $EV drag 280 220 960 560
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
  echo "$TXT" | grep -qi "TRANSFRAME" && ok "面板含 OCR 识别文本 TRANSFRAME" || bad "OCR 结果未显示 TRANSFRAME"
  R=$(press_button "id=xmark" 550)
  case "$R" in notfound*) bad "按下关闭按钮" ;; *) ok "关闭面板" ;; esac
  sleep 0.5
  press_button "xmark" 550 | grep -q notfound && ok "面板已收起" || bad "面板未收起"
  finish_overlay
  ;;

line)
  setup_textedit
  trigger_capture
  $EV drag 280 220 960 560
  sleep 1
  $EV shot 320 260 600 260 "$SHOT/line-before.png"
  R=$(press_button "id=line.diagonal")
  echo "  press line tool -> $R"
  case "$R" in notfound*) bad "找到直线工具" ;; *) ok "找到并激活直线工具" ;; esac
  $EV drag 360 300 820 460
  sleep 0.6
  $EV shot 320 260 600 260 "$SHOT/line-after.png"
  D=$($EV diff "$SHOT/line-before.png" "$SHOT/line-after.png")
  echo "  pixel diff: $D"
  awk -v d="$D" 'BEGIN{exit !(d > 0.0005)}' && ok "直线绘制产生像素变化" || bad "直线绘制无变化 (diff=$D)"
  finish_overlay
  ;;

text)
  setup_textedit
  trigger_capture
  $EV drag 280 220 960 560
  sleep 1
  $EV shot 320 260 600 260 "$SHOT/text-before.png"
  R=$(press_button "id=textbox")
  echo "  press text tool -> $R"
  case "$R" in notfound*) bad "找到文本工具" ;; *) ok "找到并激活文本工具" ;; esac
  $EV click 400 320
  sleep 0.6
  $EV type "E2ETEXT"
  sleep 0.3
  $EV click 700 500
  sleep 0.8
  $EV shot 320 260 600 260 "$SHOT/text-after.png"
  D=$($EV diff "$SHOT/text-before.png" "$SHOT/text-after.png")
  echo "  pixel diff after commit: $D"
  awk -v d="$D" 'BEGIN{exit !(d > 0.0005)}' && ok "文本提交后画布有内容" || bad "文本未提交 (diff=$D)"
  # drag the committed text box and confirm the pixels move
  $EV drag 400 320 520 420
  sleep 0.6
  $EV shot 320 260 600 260 "$SHOT/text-moved.png"
  D2=$($EV diff "$SHOT/text-after.png" "$SHOT/text-moved.png")
  echo "  pixel diff after drag: $D2"
  awk -v d="$D2" 'BEGIN{exit !(d > 0.0003)}' && ok "文本框可拖动" || bad "文本框拖动无变化 (diff=$D2)"
  finish_overlay
  ;;

square)
  setup_textedit
  trigger_capture
  $EV drag 280 220 960 560
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
  $EV drag 620 300 840 430 cmd
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

echo ""
echo "======== E2E RESULT: PASS=$PASS FAIL=$FAIL ========"
if [ "$FAIL" -gt 0 ]; then printf 'failed: %s\n' "${FAILED[@]}"; exit 1; fi
