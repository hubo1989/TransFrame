#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""migrate_localizable.py — build the Windows string tables from the macOS
Localizable.strings (FR-37).

Source of truth: TransFrame/Resources/<lang>.lproj/Localizable.strings
(10 languages). Output: windows/UI/Strings/<lang>.json (canonical, loaded by
tf::core::loc::Localization) mirrored to windows/App/Assets/Strings/ for MSIX
staging (scripts/build_msix.py copies App/Assets wholesale).

Key policy:
  * MAC_KEYS   — keys reused verbatim from the macOS tables (same key name,
                 per-language values taken from the corresponding .strings;
                 a language missing a key inherits the English value).
  * WIN_ONLY   — Windows-only UI strings (no macOS counterpart). Values are
                 authored here for all 10 languages; zh-Hans matches the
                 pre-localization hardcoded UI text.

Every output file carries the exact same key set (validated before writing).
--check-code scans C++ sources for loc::t/tw/Lx("key", ...) uses and fails on
keys missing from the tables.
"""
import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MACOS_STRINGS = REPO / 'TransFrame' / 'Resources'
OUT_CANONICAL = REPO / 'windows' / 'UI' / 'Strings'
OUT_ASSETS = REPO / 'windows' / 'App' / 'Assets' / 'Strings'

LANGS = ['en', 'zh-Hans', 'de', 'es', 'fr', 'it', 'ja', 'ko', 'pt', 'ru']

# ---------------------------------------------------------------------------
# macOS keys reused by the Windows UI (settings tabs / tray / result+preview /
# onboarding / update+notify chrome).
# ---------------------------------------------------------------------------
MAC_KEYS = [
    # shell + tabs
    'settings.window.title',
    'settings.section.general', 'settings.section.engines', 'settings.section.prompts',
    'settings.section.languages', 'settings.section.shortcuts',
    # general page
    'settings.save.location', 'settings.save.location.message', 'settings.save.location.choose',
    'settings.format', 'settings.format.png', 'settings.format.jpeg', 'settings.format.heic',
    'settings.language', 'settings.language.system',
    'settings.paddleocr.useCloud',
    # actions / common
    'action.save', 'action.cancel', 'action.close', 'action.delete',
    'action.copy', 'action.undo', 'action.redo',
    'common.copied', 'app.name',
    'error.clipboard.write.failed',
    # shortcuts page + onboarding step 1
    'settings.shortcuts',
    'settings.shortcut.fullscreen', 'settings.shortcut.selection',
    'settings.shortcut.translation.mode', 'settings.shortcut.text.selection.translation',
    'settings.shortcut.translate.and.insert', 'settings.shortcut.pick.color',
    # engines page
    'engine.available.title', 'engine.category.builtin', 'engine.category.cloud',
    'engine.health.available', 'engine.health.unverified', 'engine.health.checking',
    'engine.preset.openai', 'engine.preset.ollama', 'engine.preset.custom',
    'engine.compatible.new', 'engine.compatible.displayName', 'engine.config.test',
    'settings.vlm.title', 'settings.vlm.provider', 'settings.vlm.baseURL',
    'settings.vlm.model', 'settings.vlm.apiMode',
    # prompts page
    'prompt.engine.title', 'prompt.compatible.title', 'prompt.scene.title',
    'prompt.default.title', 'prompt.editor.variables', 'prompt.button.edit',
    'prompt.button.reset',
    # advanced page (annotation defaults)
    'settings.stroke.width', 'settings.text.size',
    # language page (translation pair selectors)
    'translation.auto', 'translation.language.follow.system',
    # tray
    'menu.capture.full.screen', 'menu.capture.selection',
    # result window
    'history.copy.translation',
    # preview window
    'preview.window.title', 'preview.enter.text',
    'tool.rectangle', 'tool.text', 'tool.arrow', 'preview.pin',
    'save.success.title', 'save.success.message', 'copy.success.message',
    # onboarding
    'onboarding.window.title', 'onboarding.welcome.title', 'onboarding.welcome.message',
    'onboarding.feature.shortcuts.title', 'onboarding.feature.shortcuts.description',
    'onboarding.back', 'onboarding.continue', 'onboarding.complete.title',
    'onboarding.complete.start',
    # update / notify
    'about.check.for.updates',
]

# ---------------------------------------------------------------------------
# Windows-only keys. zh-Hans = the pre-localization UI text.
# ---------------------------------------------------------------------------
WIN_ONLY = {
    # ---- shell ----
    'win.settings.section.advanced': {
        'zh-Hans': '高级', 'en': 'Advanced', 'de': 'Erweitert', 'es': 'Avanzadas',
        'fr': 'Avancé', 'it': 'Avanzate', 'ja': '高度設定', 'ko': '고급',
        'pt': 'Avançado', 'ru': 'Дополнительно'},
    # ---- general page ----
    'win.settings.save.reveal': {
        'zh-Hans': '在资源管理器中显示', 'en': 'Show in Explorer',
        'de': 'Im Explorer anzeigen', 'es': 'Mostrar en el Explorador',
        'fr': 'Afficher dans l’Explorateur', 'it': 'Mostra in Esplora risorse',
        'ja': 'エクスプローラーで表示', 'ko': '탐색기에서 표시',
        'pt': 'Mostrar no Explorador', 'ru': 'Показать в Проводнике'},
    'win.settings.save.desktop': {
        'zh-Hans': '桌面', 'en': 'Desktop', 'de': 'Desktop', 'es': 'Escritorio',
        'fr': 'Bureau', 'it': 'Desktop', 'ja': 'デスクトップ', 'ko': '바탕 화면',
        'pt': 'Área de Trabalho', 'ru': 'Рабочий стол'},
    'win.settings.export.title': {
        'zh-Hans': '导出格式与质量', 'en': 'Export format & quality',
        'de': 'Exportformat und Qualität', 'es': 'Formato y calidad de exportación',
        'fr': 'Format et qualité d’exportation', 'it': 'Formato e qualità di esportazione',
        'ja': 'エクスポート形式と品質', 'ko': '내보내기 형식 및 품질',
        'pt': 'Formato e qualidade de exportação', 'ru': 'Формат и качество экспорта'},
    'win.settings.export.quality': {
        'zh-Hans': '导出质量', 'en': 'Export quality', 'de': 'Exportqualität',
        'es': 'Calidad de exportación', 'fr': 'Qualité d’exportation',
        'it': 'Qualità di esportazione', 'ja': 'エクスポート品質',
        'ko': '내보내기 품질', 'pt': 'Qualidade de exportação',
        'ru': 'Качество экспорта'},
    'win.settings.language.hint': {
        'zh-Hans': '设置窗口与托盘菜单的显示语言，切换立即生效',
        'en': 'Display language for the settings window and tray menu; applies immediately',
        'de': 'Anzeigesprache für Einstellungsfenster und Tray-Menü; sofort wirksam',
        'es': 'Idioma de la ventana de ajustes y del menú de la bandeja; se aplica al instante',
        'fr': 'Langue de la fenêtre Paramètres et du menu de la zone de notification ; appliquée immédiatement',
        'it': 'Lingua della finestra delle impostazioni e del menu della barra applicazioni; applicata subito',
        'ja': '設定ウィンドウとトレイメニューの表示言語（切り替えは即時反映）',
        'ko': '설정 창과 트레이 메뉴의 표시 언어 (즉시 적용)',
        'pt': 'Idioma da janela de definições e do menu da bandeja; aplicado de imediato',
        'ru': 'Язык окна настроек и меню трея; применяется сразу'},
    'win.settings.startupUpdates.title': {
        'zh-Hans': '启动与更新', 'en': 'Startup & updates', 'de': 'Start und Updates',
        'es': 'Inicio y actualizaciones', 'fr': 'Démarrage et mises à jour',
        'it': 'Avvio e aggiornamenti', 'ja': '起動と更新', 'ko': '시작 및 업데이트',
        'pt': 'Arranque e atualizações', 'ru': 'Запуск и обновления'},
    'win.settings.autostart': {
        'zh-Hans': '开机自启', 'en': 'Launch at login', 'de': 'Beim Systemstart öffnen',
        'es': 'Iniciar con el sistema', 'fr': 'Lancer à l’ouverture de session',
        'it': 'Avvia all’accensione', 'ja': '自動起動', 'ko': '자동 시작',
        'pt': 'Iniciar com o sistema', 'ru': 'Автозапуск'},
    'win.common.on': {
        'zh-Hans': '开', 'en': 'On', 'de': 'Ein', 'es': 'Sí', 'fr': 'Activé',
        'it': 'Attivo', 'ja': 'オン', 'ko': '켜기', 'pt': 'Ativado', 'ru': 'Вкл'},
    'win.common.off': {
        'zh-Hans': '关', 'en': 'Off', 'de': 'Aus', 'es': 'No', 'fr': 'Désactivé',
        'it': 'Disattivo', 'ja': 'オフ', 'ko': '끄기', 'pt': 'Desativado', 'ru': 'Выкл'},
    'win.settings.autostart.added': {
        'zh-Hans': '已写入开机自启（HKCU Run）', 'en': 'Launch at login enabled (HKCU Run)',
        'de': 'Autostart aktiviert (HKCU Run)', 'es': 'Inicio automático activado (HKCU Run)',
        'fr': 'Lancement à l’ouverture activé (HKCU Run)',
        'it': 'Avvio automatico attivato (HKCU Run)', 'ja': '自動起動を有効にしました（HKCU Run）',
        'ko': '자동 시작을 사용 설정했습니다(HKCU Run)', 'pt': 'Arranque automático ativado (HKCU Run)',
        'ru': 'Автозапуск включён (HKCU Run)'},
    'win.settings.autostart.removed': {
        'zh-Hans': '已移除开机自启', 'en': 'Launch at login removed',
        'de': 'Autostart entfernt', 'es': 'Inicio automático desactivado',
        'fr': 'Lancement à l’ouverture supprimé', 'it': 'Avvio automatico rimosso',
        'ja': '自動起動を無効にしました', 'ko': '자동 시작을 해제했습니다',
        'pt': 'Arranque automático removido', 'ru': 'Автозапуск отключён'},
    'win.settings.checkUpdates': {
        'zh-Hans': '更新检查', 'en': 'Check for updates', 'de': 'Updateprüfung',
        'es': 'Buscar actualizaciones', 'fr': 'Rechercher des mises à jour',
        'it': 'Cerca aggiornamenti', 'ja': '更新の確認', 'ko': '업데이트 확인',
        'pt': 'Verificar atualizações', 'ru': 'Проверка обновлений'},
    'win.common.saved': {
        'zh-Hans': '已保存', 'en': 'Saved', 'de': 'Gespeichert', 'es': 'Guardado',
        'fr': 'Enregistré', 'it': 'Salvato', 'ja': '保存しました', 'ko': '저장됨',
        'pt': 'Guardado', 'ru': 'Сохранено'},
    'win.common.saveFailedEngines': {
        'zh-Hans': '保存失败（engines.json 不可写）',
        'en': 'Save failed (engines.json not writable)',
        'de': 'Speichern fehlgeschlagen (engines.json nicht beschreibbar)',
        'es': 'No se pudo guardar (engines.json no se puede escribir)',
        'fr': 'Échec de l’enregistrement (engines.json non inscriptible)',
        'it': 'Salvataggio non riuscito (engines.json non scrivibile)',
        'ja': '保存に失敗しました（engines.json に書き込めません）',
        'ko': '저장 실패 (engines.json을 쓸 수 없음)',
        'pt': 'Falha ao guardar (engines.json indisponível para escrita)',
        'ru': 'Не удалось сохранить (engines.json недоступен для записи)'},
    'win.common.saveFailedSettings': {
        'zh-Hans': '保存失败（settings.json 不可写）',
        'en': 'Save failed (settings.json not writable)',
        'de': 'Speichern fehlgeschlagen (settings.json nicht beschreibbar)',
        'es': 'No se pudo guardar (settings.json no se puede escribir)',
        'fr': 'Échec de l’enregistrement (settings.json non inscriptible)',
        'it': 'Salvataggio non riuscito (settings.json non scrivibile)',
        'ja': '保存に失敗しました（settings.json に書き込めません）',
        'ko': '저장 실패 (settings.json을 쓸 수 없음)',
        'pt': 'Falha ao guardar (settings.json indisponível para escrita)',
        'ru': 'Не удалось сохранить (settings.json недоступен для записи)'},
    # ---- shortcuts page ----
    'win.shortcuts.title': {
        'zh-Hans': '全局快捷键', 'en': 'Global shortcuts',
        'de': 'Globale Tastenkombinationen', 'es': 'Atajos globales',
        'fr': 'Raccourcis globaux', 'it': 'Scorciatoie globali',
        'ja': 'グローバルショートカット', 'ko': '전역 단축키',
        'pt': 'Atalhos globais', 'ru': 'Глобальные горячие клавиши'},
    'win.shortcuts.hint': {
        'zh-Hans': '点击组合按钮后按下新组合键（需含至少一个修饰键），Esc 取消录制；冲突的组合会标红',
        'en': 'Click a combo, then press the new keys (at least one modifier); Esc cancels; conflicting combos turn red',
        'de': 'Kombination anklicken und neue Tasten drücken (mind. ein Modifikator); Esc bricht ab; Konflikte werden rot',
        'es': 'Pulsa una combinación y luego las nuevas teclas (al menos un modificador); Esc cancela; los conflictos se marcan en rojo',
        'fr': 'Cliquez sur un raccourci puis appuyez sur les nouvelles touches (au moins un modificateur) ; Échap annule ; les conflits sont en rouge',
        'it': 'Fai clic su una combinazione e premi i nuovi tasti (almeno un modificatore); Esc annulla; i conflitti diventano rossi',
        'ja': '組み合わせをクリックして新しいキーを押してください（修飾キーが1つ以上必要）。Esc で取消、競合は赤く表示されます',
        'ko': '조합을 클릭한 후 새 키를 누르세요(수정 키 1개 이상 필요). Esc로 취소, 충돌하는 조합은 빨간색으로 표시됩니다',
        'pt': 'Clique numa combinação e prima as novas teclas (pelo menos um modificador); Esc cancela; conflitos ficam a vermelho',
        'ru': 'Нажмите на комбинацию и введите новые клавиши (хотя бы один модификатор); Esc отменяет; конфликты подсвечиваются красным'},
    'win.shortcuts.recording': {
        'zh-Hans': '录制中…', 'en': 'Recording…', 'de': 'Aufnahme…', 'es': 'Grabando…',
        'fr': 'Enregistrement…', 'it': 'Registrazione…', 'ja': '記録中…',
        'ko': '기록 중…', 'pt': 'A gravar…', 'ru': 'Запись…'},
    'win.shortcuts.saved': {
        'zh-Hans': '快捷键已保存', 'en': 'Shortcuts saved',
        'de': 'Tastenkombinationen gespeichert', 'es': 'Atajos guardados',
        'fr': 'Raccourcis enregistrés', 'it': 'Scorciatoie salvate',
        'ja': 'ショートカットを保存しました', 'ko': '단축키가 저장되었습니다',
        'pt': 'Atalhos guardados', 'ru': 'Горячие клавиши сохранены'},
    'win.shortcuts.needModifier': {
        'zh-Hans': '快捷键需包含至少一个修饰键（Ctrl/Alt/Shift/Win）',
        'en': 'A shortcut needs at least one modifier (Ctrl/Alt/Shift/Win)',
        'de': 'Eine Tastenkombination braucht mindestens einen Modifikator (Strg/Alt/Shift/Win)',
        'es': 'Un atajo necesita al menos un modificador (Ctrl/Alt/Shift/Win)',
        'fr': 'Un raccourci nécessite au moins un modificateur (Ctrl/Alt/Maj/Win)',
        'it': 'Una scorciatoia richiede almeno un modificatore (Ctrl/Alt/Shift/Win)',
        'ja': 'ショートカットには修飾キー（Ctrl/Alt/Shift/Win）が1つ以上必要です',
        'ko': '단축키에는 수정 키(Ctrl/Alt/Shift/Win)가 1개 이상 필요합니다',
        'pt': 'Um atalho precisa de pelo menos um modificador (Ctrl/Alt/Shift/Win)',
        'ru': 'В комбинации нужен хотя бы один модификатор (Ctrl/Alt/Shift/Win)'},
    'win.shortcuts.conflict': {
        'zh-Hans': '与「%@」冲突（%@）', 'en': 'Conflicts with “%@” (%@)',
        'de': 'Kollidiert mit „%@“ (%@)', 'es': 'En conflicto con «%@» (%@)',
        'fr': 'En conflit avec « %@ » (%@)', 'it': 'In conflitto con “%@” (%@)',
        'ja': '「%@」と競合しています（%@）', 'ko': '"%@"와 충돌합니다(%@)',
        'pt': 'Em conflito com “%@” (%@)', 'ru': 'Конфликтует с «%@» (%@)'},
    # ---- tray ----
    'win.tray.translateMode': {
        'zh-Hans': '翻译模式', 'en': 'Translation mode', 'de': 'Übersetzungsmodus',
        'es': 'Modo de traducción', 'fr': 'Mode de traduction', 'it': 'Modalità traduzione',
        'ja': '翻訳モード', 'ko': '번역 모드', 'pt': 'Modo de tradução',
        'ru': 'Режим перевода'},
    'win.tray.textSelection': {
        'zh-Hans': '划词翻译', 'en': 'Translate selection', 'de': 'Auswahl übersetzen',
        'es': 'Traducir selección', 'fr': 'Traduire la sélection',
        'it': 'Traduci selezione', 'ja': '選択範囲を翻訳', 'ko': '선택 영역 번역',
        'pt': 'Traduzir seleção', 'ru': 'Перевод выделенного'},
    'win.tray.translateInsert': {
        'zh-Hans': '翻译并插入', 'en': 'Translate & insert', 'de': 'Übersetzen & einfügen',
        'es': 'Traducir e insertar', 'fr': 'Traduire et insérer',
        'it': 'Traduci e inserisci', 'ja': '翻訳して挿入', 'ko': '번역 후 삽입',
        'pt': 'Traduzir e inserir', 'ru': 'Перевести и вставить'},
    'win.tray.recentShot': {
        'zh-Hans': '截图 %@', 'en': 'Shot %@', 'de': 'Aufnahme %@', 'es': 'Captura %@',
        'fr': 'Capture %@', 'it': 'Scatto %@', 'ja': 'スクリーンショット %@',
        'ko': '캡처 %@', 'pt': 'Captura %@', 'ru': 'Снимок %@'},
    'win.tray.pinnedManager': {
        'zh-Hans': '置顶管理', 'en': 'Pinned windows', 'de': 'Angeheftete Fenster',
        'es': 'Ventanas fijadas', 'fr': 'Fenêtres épinglées', 'it': 'Finestre ancorate',
        'ja': 'ピン留め管理', 'ko': '고정 창 관리', 'pt': 'Janelas fixadas',
        'ru': 'Закреплённые окна'},
    'win.tray.pinnedItem': {
        'zh-Hans': '贴图 %@', 'en': 'Pin %@', 'de': 'Pin %@', 'es': 'Fijación %@',
        'fr': 'Épinglage %@', 'it': 'Pin %@', 'ja': 'ピン %@', 'ko': '핀 %@',
        'pt': 'Pin %@', 'ru': 'Закладка %@'},
    'win.tray.pinnedInteractive': {
        'zh-Hans': '可交互', 'en': 'interactive', 'de': 'interaktiv',
        'es': 'interactivo', 'fr': 'interactif', 'it': 'interattivo',
        'ja': '操作可能', 'ko': '상호작용 가능', 'pt': 'interativo', 'ru': 'интерактивно'},
    'win.tray.toggleThrough': {
        'zh-Hans': '切换穿透', 'en': 'Toggle click-through',
        'de': 'Klick-Durchgriff umschalten', 'es': 'Alternar clic a través',
        'fr': 'Basculer le clic traversant', 'it': 'Attiva/disattiva click-through',
        'ja': 'クリックスルー切替', 'ko': '클릭 통과 전환',
        'pt': 'Alternar clique através', 'ru': 'Сквозные нажатия: вкл/выкл'},
    'win.tray.gather': {
        'zh-Hans': '聚拢到前台', 'en': 'Bring all to front', 'de': 'Alle nach vorn holen',
        'es': 'Traer todo al frente', 'fr': 'Tout ramener au premier plan',
        'it': 'Porta tutto in primo piano', 'ja': 'すべて前面へ', 'ko': '모두 앞으로 가져오기',
        'pt': 'Trazer tudo para a frente', 'ru': 'Собрать поверх всех окон'},
    'win.tray.closeAll': {
        'zh-Hans': '全部关闭', 'en': 'Close all', 'de': 'Alle schließen',
        'es': 'Cerrar todo', 'fr': 'Tout fermer', 'it': 'Chiudi tutto', 'ja': 'すべて閉じる',
        'ko': '모두 닫기', 'pt': 'Fechar tudo', 'ru': 'Закрыть все'},
    'win.tray.history': {
        'zh-Hans': '历史记录', 'en': 'History', 'de': 'Verlauf', 'es': 'Historial',
        'fr': 'Historique', 'it': 'Cronologia', 'ja': '履歴', 'ko': '기록',
        'pt': 'Histórico', 'ru': 'История'},
    'win.tray.settings': {
        'zh-Hans': '设置', 'en': 'Settings', 'de': 'Einstellungen', 'es': 'Ajustes',
        'fr': 'Paramètres', 'it': 'Impostazioni', 'ja': '設定', 'ko': '설정',
        'pt': 'Definições', 'ru': 'Настройки'},
    'win.tray.checkUpdates': {
        'zh-Hans': '检查更新', 'en': 'Check for updates', 'de': 'Nach Updates suchen',
        'es': 'Buscar actualizaciones', 'fr': 'Rechercher des mises à jour',
        'it': 'Cerca aggiornamenti', 'ja': '更新を確認', 'ko': '업데이트 확인',
        'pt': 'Verificar atualizações', 'ru': 'Проверить обновления'},
    'win.tray.replayOnboarding': {
        'zh-Hans': '重新查看引导', 'en': 'Show setup guide again',
        'de': 'Einrichtung erneut anzeigen', 'es': 'Ver la guía de configuración otra vez',
        'fr': 'Revoir le guide de configuration', 'it': 'Rivedi la guida di configurazione',
        'ja': 'セットアップガイドを再表示', 'ko': '설정 가이드 다시 보기',
        'pt': 'Ver o guia de configuração novamente', 'ru': 'Показать руководство снова'},
    'win.tray.exit': {
        'zh-Hans': '退出', 'en': 'Exit', 'de': 'Beenden', 'es': 'Salir',
        'fr': 'Quitter', 'it': 'Esci', 'ja': '終了', 'ko': '종료', 'pt': 'Sair',
        'ru': 'Выход'},
    # ---- result window ----
    'win.result.title': {
        'zh-Hans': 'TransFrame 结果', 'en': 'TransFrame Result',
        'de': 'TransFrame Ergebnis', 'es': 'Resultado de TransFrame',
        'fr': 'Résultat TransFrame', 'it': 'Risultato TransFrame', 'ja': 'TransFrame 結果',
        'ko': 'TransFrame 결과', 'pt': 'Resultado do TransFrame',
        'ru': 'TransFrame — результат'},
    'win.result.preview': {
        'zh-Hans': '预览', 'en': 'Preview', 'de': 'Vorschau', 'es': 'Vista previa',
        'fr': 'Aperçu', 'it': 'Anteprima', 'ja': 'プレビュー', 'ko': '미리보기',
        'pt': 'Pré-visualizar', 'ru': 'Предпросмотр'},
    # ---- preview window ----
    'win.preview.title': {
        'zh-Hans': '预览', 'en': 'Preview', 'de': 'Vorschau', 'es': 'Vista previa',
        'fr': 'Aperçu', 'it': 'Anteprima', 'ja': 'プレビュー', 'ko': '미리보기',
        'pt': 'Pré-visualização', 'ru': 'Просмотр'},
    'win.preview.status.default': {
        'zh-Hans': '工具: 矩形（R/1）· 拖拽绘制 · Shift=正方形/45°',
        'en': 'Tool: rectangle (R/1) · drag to draw · Shift=square/45°',
        'de': 'Werkzeug: Rechteck (R/1) · Ziehen zum Zeichnen · Shift=Quadrat/45°',
        'es': 'Herramienta: rectángulo (R/1) · arrastra para dibujar · Shift=cuadrado/45°',
        'fr': 'Outil : rectangle (R/1) · glisser pour dessiner · Maj=carré/45°',
        'it': 'Strumento: rettangolo (R/1) · trascina per disegnare · Maiusc=quadrato/45°',
        'ja': 'ツール: 長方形（R/1）· ドラッグで描画 · Shift=正方形/45°',
        'ko': '도구: 사각형(R/1) · 드래그하여 그리기 · Shift=정사각형/45°',
        'pt': 'Ferramenta: retângulo (R/1) · arraste para desenhar · Shift=quadrado/45°',
        'ru': 'Инструмент: прямоугольник (R/1) · рисуйте перетаскиванием · Shift=квадрат/45°'},
    'win.preview.status.rectangle': {
        'zh-Hans': '工具: 矩形（R/1）· Shift=正方形',
        'en': 'Tool: rectangle (R/1) · Shift=square',
        'de': 'Werkzeug: Rechteck (R/1) · Shift=Quadrat',
        'es': 'Herramienta: rectángulo (R/1) · Shift=cuadrado',
        'fr': 'Outil : rectangle (R/1) · Maj=carré',
        'it': 'Strumento: rettangolo (R/1) · Maiusc=quadrato',
        'ja': 'ツール: 長方形（R/1）· Shift=正方形',
        'ko': '도구: 사각형(R/1) · Shift=정사각형',
        'pt': 'Ferramenta: retângulo (R/1) · Shift=quadrado',
        'ru': 'Инструмент: прямоугольник (R/1) · Shift=квадрат'},
    'win.preview.status.text': {
        'zh-Hans': '工具: 文字（T/4）· 点击画布输入',
        'en': 'Tool: text (T/4) · click the canvas to type',
        'de': 'Werkzeug: Text (T/4) · Klicke auf die Zeichenfläche',
        'es': 'Herramienta: texto (T/4) · haz clic en el lienzo para escribir',
        'fr': 'Outil : texte (T/4) · cliquez sur le canevas pour saisir',
        'it': 'Strumento: testo (T/4) · fai clic sulla tela per digitare',
        'ja': 'ツール: 文字（T/4）· キャンバスをクリックして入力',
        'ko': '도구: 텍스트(T/4) · 캔버스를 클릭하여 입력',
        'pt': 'Ferramenta: texto (T/4) · clica na tela para escrever',
        'ru': 'Инструмент: текст (T/4) · щёлкните по холсту для ввода'},
    'win.preview.status.arrow': {
        'zh-Hans': '工具: 箭头（A/3）· Shift=45°',
        'en': 'Tool: arrow (A/3) · Shift=45°',
        'de': 'Werkzeug: Pfeil (A/3) · Shift=45°',
        'es': 'Herramienta: flecha (A/3) · Shift=45°',
        'fr': 'Outil : flèche (A/3) · Maj=45°',
        'it': 'Strumento: freccia (A/3) · Maiusc=45°',
        'ja': 'ツール: 矢印（A/3）· Shift=45°',
        'ko': '도구: 화살표(A/3) · Shift=45°',
        'pt': 'Ferramenta: seta (A/3) · Shift=45°',
        'ru': 'Инструмент: стрелка (A/3) · Shift=45°'},
    'win.preview.status.select': {
        'zh-Hans': '工具: 选择/移动（点击标注，Del 删除）',
        'en': 'Tool: select/move (click an annotation, Del removes it)',
        'de': 'Werkzeug: Auswählen/Verschieben (Anmerkung anklicken, Entf löscht)',
        'es': 'Herramienta: seleccionar/mover (clic en una anotación, Supr elimina)',
        'fr': 'Outil : sélection/déplacement (cliquer une annotation, Suppr supprime)',
        'it': 'Strumento: seleziona/sposta (fai clic su un’annotazione, Canc elimina)',
        'ja': 'ツール: 選択/移動（注釈をクリック、Del で削除）',
        'ko': '도구: 선택/이동(주석 클릭, Del 키로 삭제)',
        'pt': 'Ferramenta: selecionar/mover (clica numa anotação, Del remove)',
        'ru': 'Инструмент: выбор/перемещение (щёлкните по аннотации, Del удаляет)'},
    'win.preview.copiedClosed': {
        'zh-Hans': '已复制并关闭', 'en': 'Copied and closed', 'de': 'Kopiert und geschlossen',
        'es': 'Copiado y cerrado', 'fr': 'Copié et fermé', 'it': 'Copiato e chiuso',
        'ja': 'コピーして閉じました', 'ko': '복사 후 닫았습니다',
        'pt': 'Copiado e fechado', 'ru': 'Скопировано и закрыто'},
    'win.preview.askPngFallback': {
        'zh-Hans': '是否改存为 PNG？', 'en': 'Save as PNG instead?',
        'de': 'Stattdessen als PNG speichern?', 'es': '¿Guardar como PNG?',
        'fr': 'Enregistrer en PNG à la place ?', 'it': 'Salvare come PNG?',
        'ja': 'PNG で保存しますか？', 'ko': 'PNG로 저장할까요?',
        'pt': 'Guardar como PNG?', 'ru': 'Сохранить вместо этого как PNG?'},
    'win.preview.savedPngFallback': {
        'zh-Hans': '已保存 PNG 回退: %@', 'en': 'Saved PNG fallback: %@',
        'de': 'PNG-Fallback gespeichert: %@', 'es': 'Respaldo PNG guardado: %@',
        'fr': 'Repli PNG enregistré : %@', 'it': 'Fallback PNG salvato: %@',
        'ja': 'PNG フォールバックを保存しました: %@', 'ko': 'PNG 대체 저장됨: %@',
        'pt': 'Alternativa PNG guardada: %@', 'ru': 'Сохранён запасной PNG: %@'},
    'win.preview.saveFailed': {
        'zh-Hans': '保存失败: %@', 'en': 'Save failed: %@',
        'de': 'Speichern fehlgeschlagen: %@', 'es': 'No se pudo guardar: %@',
        'fr': 'Échec de l’enregistrement : %@', 'it': 'Salvataggio non riuscito: %@',
        'ja': '保存に失敗しました: %@', 'ko': '저장 실패: %@',
        'pt': 'Falha ao guardar: %@', 'ru': 'Не удалось сохранить: %@'},
    'win.preview.pngFallbackAlsoFailed': {
        'zh-Hans': 'PNG 回退也失败: %@', 'en': 'PNG fallback also failed: %@',
        'de': 'PNG-Fallback ebenfalls fehlgeschlagen: %@',
        'es': 'El respaldo PNG también falló: %@',
        'fr': 'Le repli PNG a aussi échoué : %@', 'it': 'Anche il fallback PNG non è riuscito: %@',
        'ja': 'PNG フォールバックも失敗しました: %@', 'ko': 'PNG 대체도 실패: %@',
        'pt': 'A alternativa PNG também falhou: %@', 'ru': 'Запасной PNG тоже не сохранён: %@'},
    'win.preview.noImage': {
        'zh-Hans': '没有可保存的图像', 'en': 'No image to save',
        'de': 'Kein Bild zum Speichern', 'es': 'No hay imagen para guardar',
        'fr': 'Aucune image à enregistrer', 'it': 'Nessuna immagine da salvare',
        'ja': '保存できる画像がありません', 'ko': '저장할 이미지가 없습니다',
        'pt': 'Sem imagem para guardar', 'ru': 'Нет изображения для сохранения'},
    'win.preview.dialogTitle': {
        'zh-Hans': '保存截图', 'en': 'Save screenshot', 'de': 'Screenshot speichern',
        'es': 'Guardar captura', 'fr': 'Enregistrer la capture', 'it': 'Salva screenshot',
        'ja': 'スクリーンショットを保存', 'ko': '스크린샷 저장',
        'pt': 'Guardar captura de ecrã', 'ru': 'Сохранить снимок экрана'},
    'win.preview.filter.png': {
        'zh-Hans': 'PNG 图片', 'en': 'PNG image', 'de': 'PNG-Datei', 'es': 'Imagen PNG',
        'fr': 'Image PNG', 'it': 'Immagine PNG', 'ja': 'PNG 画像', 'ko': 'PNG 이미지',
        'pt': 'Imagem PNG', 'ru': 'Изображение PNG'},
    'win.preview.filter.jpeg': {
        'zh-Hans': 'JPEG 图片', 'en': 'JPEG image', 'de': 'JPEG-Datei', 'es': 'Imagen JPEG',
        'fr': 'Image JPEG', 'it': 'Immagine JPEG', 'ja': 'JPEG 画像', 'ko': 'JPEG 이미지',
        'pt': 'Imagem JPEG', 'ru': 'Изображение JPEG'},
    'win.preview.filter.heic': {
        'zh-Hans': 'HEIC 图片', 'en': 'HEIC image', 'de': 'HEIC-Datei', 'es': 'Imagen HEIC',
        'fr': 'Image HEIC', 'it': 'Immagine HEIC', 'ja': 'HEIC 画像', 'ko': 'HEIC 이미지',
        'pt': 'Imagem HEIC', 'ru': 'Изображение HEIC'},
    'win.preview.filter.all': {
        'zh-Hans': '所有文件', 'en': 'All files', 'de': 'Alle Dateien',
        'es': 'Todos los archivos', 'fr': 'Tous les fichiers', 'it': 'Tutti i file',
        'ja': 'すべてのファイル', 'ko': '모든 파일', 'pt': 'Todos os ficheiros',
        'ru': 'Все файлы'},
    'win.preview.comFailed': {
        'zh-Hans': 'COM 初始化失败，无法打开保存对话框',
        'en': 'COM init failed; cannot open the save dialog',
        'de': 'COM-Initialisierung fehlgeschlagen; Speicherdialog kann nicht geöffnet werden',
        'es': 'Error al inicializar COM; no se puede abrir el diálogo de guardado',
        'fr': 'Échec de l’initialisation COM ; impossible d’ouvrir la boîte d’enregistrement',
        'it': 'Inizializzazione COM non riuscita; impossibile aprire la finestra di salvataggio',
        'ja': 'COM の初期化に失敗したため、保存ダイアログを開けません',
        'ko': 'COM 초기화 실패로 저장 대화상자를 열 수 없습니다',
        'pt': 'Falha ao iniciar COM; não é possível abrir o diálogo de gravação',
        'ru': 'Не удалось инициализировать COM; диалог сохранения недоступен'},
    'win.preview.dialogFailed': {
        'zh-Hans': '无法打开保存对话框', 'en': 'Cannot open the save dialog',
        'de': 'Speicherdialog kann nicht geöffnet werden',
        'es': 'No se puede abrir el diálogo de guardado',
        'fr': 'Impossible d’ouvrir la boîte d’enregistrement',
        'it': 'Impossibile aprire la finestra di salvataggio',
        'ja': '保存ダイアログを開けません', 'ko': '저장 대화상자를 열 수 없습니다',
        'pt': 'Não é possível abrir o diálogo de gravação',
        'ru': 'Не удалось открыть диалог сохранения'},
    'win.preview.inputTextTitle': {
        'zh-Hans': '输入标注文字', 'en': 'Enter annotation text',
        'de': 'Anmerkungstext eingeben', 'es': 'Introduce el texto de la anotación',
        'fr': 'Saisir le texte de l’annotation', 'it': 'Inserisci il testo dell’annotazione',
        'ja': '注釈テキストを入力', 'ko': '주석 텍스트 입력',
        'pt': 'Introduzir texto da anotação', 'ru': 'Введите текст аннотации'},
    'win.preview.msgTitle': {
        'zh-Hans': 'TransFrame 预览', 'en': 'TransFrame Preview',
        'de': 'TransFrame Vorschau', 'es': 'Vista previa de TransFrame',
        'fr': 'Aperçu TransFrame', 'it': 'Anteprima TransFrame', 'ja': 'TransFrame プレビュー',
        'ko': 'TransFrame 미리보기', 'pt': 'Pré-visualização do TransFrame',
        'ru': 'TransFrame — просмотр'},
    'win.preview.composeFailed': {
        'zh-Hans': '合成图像失败', 'en': 'Failed to composite the image',
        'de': 'Bildzusammensetzung fehlgeschlagen', 'es': 'No se pudo componer la imagen',
        'fr': 'Échec de la composition de l’image', 'it': 'Composizione dell’immagine non riuscita',
        'ja': '画像の合成に失敗しました', 'ko': '이미지 합성 실패',
        'pt': 'Falha ao compor a imagem', 'ru': 'Не удалось наложить изображение'},
    # ---- language page ----
    'win.language.translation.title': {
        'zh-Hans': '翻译语言', 'en': 'Translation languages', 'de': 'Übersetzungssprachen',
        'es': 'Idiomas de traducción', 'fr': 'Langues de traduction',
        'it': 'Lingue di traduzione', 'ja': '翻訳言語', 'ko': '번역 언어',
        'pt': 'Idiomas de tradução', 'ru': 'Языки перевода'},
    'win.language.translation.hint': {
        'zh-Hans': '全局翻译与翻译插入可分别设置语言对',
        'en': 'Separate language pairs for global translation and translate-and-insert',
        'de': 'Getrennte Sprachpaare für globale Übersetzung und Übersetzen-Einfügen',
        'es': 'Pares de idiomas separados para traducción global y traducir-e-insertar',
        'fr': 'Paires de langues distinctes pour la traduction globale et traduire-insérer',
        'it': 'Coppie di lingue separate per traduzione globale e traduci-e-inserisci',
        'ja': '全局翻訳と翻訳挿入で言語ペアを個別に設定できます',
        'ko': '전역 번역과 번역 후 삽입에 각각 언어 쌍을 설정합니다',
        'pt': 'Pares de idiomas separados para tradução global e traduzir-e-inserir',
        'ru': 'Отдельные пары языков для обычного перевода и режима «перевести и вставить»'},
    'win.language.global': {
        'zh-Hans': '全局翻译', 'en': 'Global translation', 'de': 'Globale Übersetzung',
        'es': 'Traducción global', 'fr': 'Traduction globale', 'it': 'Traduzione globale',
        'ja': '全局翻訳', 'ko': '전역 번역', 'pt': 'Tradução global',
        'ru': 'Общий перевод'},
    'win.language.translateInsert': {
        'zh-Hans': '翻译插入', 'en': 'Translate & insert', 'de': 'Übersetzen & einfügen',
        'es': 'Traducir e insertar', 'fr': 'Traduire et insérer',
        'it': 'Traduci e inserisci', 'ja': '翻訳挿入', 'ko': '번역 후 삽입',
        'pt': 'Traduzir e inserir', 'ru': 'Перевести и вставить'},
    # ---- advanced page ----
    'win.advanced.annotations.title': {
        'zh-Hans': '标注默认样式', 'en': 'Annotation defaults',
        'de': 'Anmerkungs-Standardeinstellungen', 'es': 'Valores predeterminados de anotación',
        'fr': 'Valeurs par défaut des annotations', 'it': 'Predefiniti delle annotazioni',
        'ja': '注釈のデフォルトスタイル', 'ko': '주석 기본 스타일',
        'pt': 'Predefinições de anotação', 'ru': 'Параметры аннотаций по умолчанию'},
    'win.advanced.annotations.hint': {
        'zh-Hans': '新标注的描边颜色、宽度与文本大小',
        'en': 'Stroke color, width and text size for new annotations',
        'de': 'Strichfarbe, -breite und Textgröße für neue Anmerkungen',
        'es': 'Color, ancho del trazo y tamaño de texto de las nuevas anotaciones',
        'fr': 'Couleur et épaisseur du trait, taille du texte des nouvelles annotations',
        'it': 'Colore e spessore del tratto e dimensione del testo per le nuove annotazioni',
        'ja': '新しい注釈の描边色・幅・テキストサイズ',
        'ko': '새 주석의 선 색상, 두께 및 텍스트 크기',
        'pt': 'Cor, espessura do traço e tamanho do texto para novas anotações',
        'ru': 'Цвет и толщина линии, размер текста для новых аннотаций'},
    'win.advanced.data.title': {
        'zh-Hans': '数据与日志', 'en': 'Data & logs', 'de': 'Daten und Protokolle',
        'es': 'Datos y registros', 'fr': 'Données et journaux', 'it': 'Dati e registri',
        'ja': 'データとログ', 'ko': '데이터 및 로그', 'pt': 'Dados e registros',
        'ru': 'Данные и журналы'},
    'win.advanced.openDataDir': {
        'zh-Hans': '打开数据目录', 'en': 'Open data folder',
        'de': 'Datenordner öffnen', 'es': 'Abrir carpeta de datos',
        'fr': 'Ouvrir le dossier de données', 'it': 'Apri cartella dei dati',
        'ja': 'データフォルダーを開く', 'ko': '데이터 폴더 열기',
        'pt': 'Abrir pasta de dados', 'ru': 'Открыть папку данных'},
    'win.advanced.openLogs': {
        'zh-Hans': '打开日志', 'en': 'Open logs', 'de': 'Protokolle öffnen',
        'es': 'Abrir registros', 'fr': 'Ouvrir les journaux', 'it': 'Apri registri',
        'ja': 'ログを開く', 'ko': '로그 열기', 'pt': 'Abrir registos',
        'ru': 'Открыть журналы'},
    'win.advanced.exportPerf': {
        'zh-Hans': '导出性能报告', 'en': 'Export performance report',
        'de': 'Leistungsbericht exportieren', 'es': 'Exportar informe de rendimiento',
        'fr': 'Exporter le rapport de performances',
        'it': 'Esporta rapporto prestazioni', 'ja': 'パフォーマンスレポートを書き出す',
        'ko': '성능 보고서 내보내기', 'pt': 'Exportar relatório de desempenho',
        'ru': 'Экспорт отчёта о производительности'},
    'win.advanced.perfNote': {
        'zh-Hans': '环境快照（FR-40 埋点接入后为全链路数据）',
        'en': 'Environment snapshot (full pipeline data once FR-40 lands)',
        'de': 'Umgebungs-Snapshot (vollständige Daten, sobald FR-40 integriert ist)',
        'es': 'Instantánea del entorno (datos completos cuando llegue FR-40)',
        'fr': 'Aperçu de l’environnement (données complètes quand FR-40 arrivera)',
        'it': 'Snapshot dell’ambiente (dati completi quando arriverà FR-40)',
        'ja': '環境スナップショット（FR-40 計測接続後は全経路データ）',
        'ko': '환경 스냅샷(FR-40 계측 연결 후 전체 파이프라인 데이터)',
        'pt': 'Instantâneo do ambiente (dados completos quando o FR-40 chegar)',
        'ru': 'Снимок окружения (полные данные после подключения FR-40)'},
    'win.advanced.reset.title': {
        'zh-Hans': '重置', 'en': 'Reset', 'de': 'Zurücksetzen', 'es': 'Restablecer',
        'fr': 'Réinitialiser', 'it': 'Reimposta', 'ja': 'リセット', 'ko': '재설정',
        'pt': 'Reiniciar', 'ru': 'Сброс'},
    'win.advanced.reset.hint': {
        'zh-Hans': '把描边颜色、描边宽度、文本大小恢复为默认值（不影响其它设置）',
        'en': 'Restore default stroke color, width and text size (other settings untouched)',
        'de': 'Strichfarbe, -breite und Textgröße zurücksetzen (übrige Einstellungen bleiben)',
        'es': 'Restaura el color, ancho del trazo y tamaño de texto predeterminados (sin tocar el resto)',
        'fr': 'Rétablit la couleur et l’épaisseur du trait et la taille du texte (autres réglages conservés)',
        'it': 'Ripristina colore, spessore del tratto e dimensione del testo (altre impostazioni intatte)',
        'ja': '描边色・幅・テキストサイズを既定値に戻します（他の設定には影響しません）',
        'ko': '선 색상, 두께, 텍스트 크기를 기본값으로 되돌립니다(다른 설정은 유지)',
        'pt': 'Restaura cor, espessura do traço e tamanho do texto predefinidos (resto intacto)',
        'ru': 'Вернуть цвет и толщину линии и размер текста (остальные настройки не трогаются)'},
    'win.advanced.resetAll': {
        'zh-Hans': '全部重置', 'en': 'Reset all', 'de': 'Alles zurücksetzen',
        'es': 'Restablecer todo', 'fr': 'Tout réinitialiser', 'it': 'Reimposta tutto',
        'ja': 'すべてリセット', 'ko': '모두 재설정', 'pt': 'Reiniciar tudo',
        'ru': 'Сбросить всё'},
    'win.advanced.resetDialog.body': {
        'zh-Hans': '将把描边颜色、描边宽度和文本大小恢复为默认值。此操作无法撤销。',
        'en': 'Stroke color, stroke width and text size will return to their defaults. This cannot be undone.',
        'de': 'Strichfarbe, -breite und Textgröße kehren zu den Standardeinstellungen zurück. Dies kann nicht rückgängig gemacht werden.',
        'es': 'El color, el ancho del trazo y el tamaño del texto volverán a sus valores predeterminados. No se puede deshacer.',
        'fr': 'La couleur et l’épaisseur du trait et la taille du texte reviendront à leurs valeurs par défaut. Action irréversible.',
        'it': 'Colore, spessore del tratto e dimensione del testo torneranno ai valori predefiniti. Operazione irreversibile.',
        'ja': '描边色・描边幅・テキストサイズを既定値に戻します。この操作は取り消せません。',
        'ko': '선 색상, 두께, 텍스트 크기가 기본값으로 되돌아갑니다. 되돌릴 수 없습니다.',
        'pt': 'A cor, a espessura do traço e o tamanho do texto voltam aos valores predefinidos. Não é possível desfazer.',
        'ru': 'Цвет и толщина линии и размер текста вернутся к значениям по умолчанию. Отменить будет нельзя.'},
    'win.advanced.exportFailed': {
        'zh-Hans': '性能报告导出失败', 'en': 'Performance report export failed',
        'de': 'Export des Leistungsberichts fehlgeschlagen',
        'es': 'Error al exportar el informe de rendimiento',
        'fr': 'Échec de l’export du rapport de performances',
        'it': 'Esportazione del rapporto prestazioni non riuscita',
        'ja': 'パフォーマンスレポートの書き出しに失敗しました',
        'ko': '성능 보고서 내보내기 실패',
        'pt': 'Falha ao exportar o relatório de desempenho',
        'ru': 'Не удалось экспортировать отчёт о производительности'},
    'win.advanced.noLocalAppdata': {
        'zh-Hans': '无法定位 %LOCALAPPDATA%', 'en': 'Cannot locate %LOCALAPPDATA%',
        'de': '%LOCALAPPDATA% nicht gefunden', 'es': 'No se encuentra %LOCALAPPDATA%',
        'fr': 'Impossible de localiser %LOCALAPPDATA%', 'it': 'Impossibile trovare %LOCALAPPDATA%',
        'ja': '%LOCALAPPDATA% を特定できません', 'ko': '%LOCALAPPDATA%를 찾을 수 없습니다',
        'pt': 'Não é possível localizar %LOCALAPPDATA%', 'ru': 'Не удалось найти %LOCALAPPDATA%'},
    'win.advanced.exported': {
        'zh-Hans': '已导出 %@', 'en': 'Exported %@', 'de': 'Exportiert: %@',
        'es': 'Exportado: %@', 'fr': 'Exporté : %@', 'it': 'Esportato: %@',
        'ja': '書き出しました: %@', 'ko': '내보냈습니다: %@', 'pt': 'Exportado: %@',
        'ru': 'Экспортировано: %@'},
    # ---- engines page ----
    'win.engines.available.hint': {
        'zh-Hans': '点击名称配置；开关控制是否可加入链/对比集',
        'en': 'Click a name to configure; the switch controls chain/compare membership',
        'de': 'Name anklicken zum Konfigurieren; der Schalter steuert Ketten-/Vergleichsmitgliedschaft',
        'es': 'Pulsa un nombre para configurar; el interruptor controla si entra en la cadena/comparación',
        'fr': 'Cliquez sur un nom pour configurer ; le commutateur contrôle l’entrée dans la chaîne/comparaison',
        'it': 'Fai clic su un nome per configurare; l’interruttore controlla l’ingresso in catena/confronto',
        'ja': '名前をクリックして設定。スイッチでチェーン/比較への参加を制御します',
        'ko': '이름을 클릭하여 구성. 스위치로 체인/비교 집합 포함 여부를 제어합니다',
        'pt': 'Clica num nome para configurar; o interruptor controla a entrada na cadeia/comparação',
        'ru': 'Нажмите на название для настройки; переключатель управляет вхождением в цепочку/сравнение'},
    'win.engines.clickToConfigure': {
        'zh-Hans': '点击名称配置', 'en': 'Click a name to configure',
        'de': 'Zum Konfigurieren Namen anklicken', 'es': 'Pulsa un nombre para configurar',
        'fr': 'Cliquez sur un nom pour configurer', 'it': 'Fai clic su un nome per configurare',
        'ja': '名前をクリックして設定', 'ko': '이름을 클릭하여 구성',
        'pt': 'Clica num nome para configurar', 'ru': 'Нажмите на название для настройки'},
    'win.engines.enabledShort': {
        'zh-Hans': '启用', 'en': 'Enabled', 'de': 'Aktiviert', 'es': 'Activado',
        'fr': 'Activé', 'it': 'Attivo', 'ja': '有効', 'ko': '사용', 'pt': 'Ativado',
        'ru': 'Включён'},
    'win.engines.inUse': {
        'zh-Hans': '使用中', 'en': 'In use', 'de': 'In Verwendung', 'es': 'En uso',
        'fr': 'Utilisé', 'it': 'In uso', 'ja': '使用中', 'ko': '사용 중',
        'pt': 'Em uso', 'ru': 'Используется'},
    'win.engines.notInChain': {
        'zh-Hans': '未加入链/对比集', 'en': 'Not in chain/compare set',
        'de': 'Nicht in Kette/Vergleichsmenge', 'es': 'Fuera de la cadena/comparación',
        'fr': 'Hors chaîne/comparaison', 'it': 'Fuori da catena/confronto',
        'ja': 'チェーン/比較に未参加', 'ko': '체인/비교 집합에 없음',
        'pt': 'Fora da cadeia/comparação', 'ru': 'Не в цепочке/сравнении'},
    'win.engines.compatibleSection': {
        'zh-Hans': '兼容引擎（OpenAI 兼容端点）', 'en': 'Compatible engines (OpenAI-compatible endpoints)',
        'de': 'Kompatible Engines (OpenAI-kompatible Endpunkte)',
        'es': 'Motores compatibles (endpoints compatibles con OpenAI)',
        'fr': 'Moteurs compatibles (points de terminaison compatibles OpenAI)',
        'it': 'Motori compatibili (endpoint compatibili OpenAI)',
        'ja': '互換エンジン（OpenAI 互換エンドポイント）',
        'ko': '호환 엔진(OpenAI 호환 엔드포인트)',
        'pt': 'Motores compatíveis (endpoints compatíveis com OpenAI)',
        'ru': 'Совместимые движки (OpenAI-совместимые адреса)'},
    'win.engines.translationPolicy': {
        'zh-Hans': '翻译策略', 'en': 'Translation policy', 'de': 'Übersetzungsstrategie',
        'es': 'Política de traducción', 'fr': 'Stratégie de traduction',
        'it': 'Criteri di traduzione', 'ja': '翻訳ポリシー', 'ko': '번역 정책',
        'pt': 'Política de tradução', 'ru': 'Схема перевода'},
    'win.engines.policyOrdered': {
        'zh-Hans': '顺序回退链（ordered）', 'en': 'Ordered fallback chain',
        'de': 'Geordnete Rückfallkette', 'es': 'Cadena de respaldo ordenada',
        'fr': 'Chaîne de repli ordonnée', 'it': 'Catena di ripiego ordinata',
        'ja': '順次フォールバックチェーン（ordered）', 'ko': '순차 폴백 체인(ordered)',
        'pt': 'Cadeia de recurso ordenada', 'ru': 'Последовательная цепочка (ordered)'},
    'win.engines.policyOrderedHint': {
        'zh-Hans': '按顺序尝试，失败自动回退到下一个', 'en': 'Try in order; failures fall back to the next engine',
        'de': 'Der Reihe nach versuchen; bei Fehlern fällt es auf die nächste Engine zurück',
        'es': 'Prueba en orden; los fallos pasan al siguiente motor',
        'fr': 'Essaie dans l’ordre ; un échec bascule sur le moteur suivant',
        'it': 'Prova in ordine; in caso di errore passa al motore successivo',
        'ja': '順番に試行し、失敗時は次のエンジンへ自動フォールバック',
        'ko': '순서대로 시도하고 실패 시 다음 엔진으로 폴백',
        'pt': 'Tenta por ordem; falhas recaem no motor seguinte',
        'ru': 'Попробует по порядку; при сбое переходит к следующему'},
    'win.engines.policyCompare': {
        'zh-Hans': '并行对比（compare）', 'en': 'Side-by-side compare',
        'de': 'Paralleler Vergleich', 'es': 'Comparación en paralelo',
        'fr': 'Comparaison parallèle', 'it': 'Confronto parallelo',
        'ja': '並列比較（compare）', 'ko': '병렬 비교(compare)',
        'pt': 'Comparação paralela', 'ru': 'Параллельное сравнение (compare)'},
    'win.engines.policyCompareHint': {
        'zh-Hans': '同时调用，结果并排对比（2–4 个）', 'en': 'Run several at once, results side by side (2–4)',
        'de': 'Mehrere gleichzeitig aufrufen, Ergebnisse nebeneinander (2–4)',
        'es': 'Llama a varios a la vez, resultados lado a lado (2–4)',
        'fr': 'En lance plusieurs à la fois, résultats côte à côte (2–4)',
        'it': 'Ne invoca più d’uno, risultati affiancati (2–4)',
        'ja': '同時に呼び出して結果を並べて比較（2〜4 個）',
        'ko': '여러 개를 동시에 호출해 결과를 나란히 비교(2–4개)',
        'pt': 'Chama vários ao mesmo tempo, resultados lado a lado (2–4)',
        'ru': 'Запускает несколько сразу, результаты рядом (2–4)'},
    'win.engines.chainTitle': {
        'zh-Hans': '顺序回退链', 'en': 'Fallback chain', 'de': 'Rückfallkette',
        'es': 'Cadena de respaldo', 'fr': 'Chaîne de repli', 'it': 'Catena di ripiego',
        'ja': '順次フォールバックチェーン', 'ko': '순차 폴백 체인', 'pt': 'Cadeia de recurso',
        'ru': 'Цепочка с резервом'},
    'win.engines.compareTitle': {
        'zh-Hans': '并行对比引擎', 'en': 'Compare engines', 'de': 'Engines vergleichen',
        'es': 'Comparar motores', 'fr': 'Comparer les moteurs', 'it': 'Confronta motori',
        'ja': '並列比較エンジン', 'ko': '병렬 비교 엔진', 'pt': 'Comparar motores',
        'ru': 'Движки для сравнения'},
    'win.engines.addEngine': {
        'zh-Hans': '添加引擎 ▾', 'en': 'Add engine ▾', 'de': 'Engine hinzufügen ▾',
        'es': 'Añadir motor ▾', 'fr': 'Ajouter un moteur ▾', 'it': 'Aggiungi motore ▾',
        'ja': 'エンジンを追加 ▾', 'ko': '엔진 추가 ▾', 'pt': 'Adicionar motor ▾',
        'ru': 'Добавить движок ▾'},
    'win.engines.replace': {
        'zh-Hans': '替换 ▾', 'en': 'Replace ▾', 'de': 'Ersetzen ▾', 'es': 'Reemplazar ▾',
        'fr': 'Remplacer ▾', 'it': 'Sostituisci ▾', 'ja': '置換 ▾', 'ko': '교체 ▾',
        'pt': 'Substituir ▾', 'ru': 'Заменить ▾'},
    'win.engines.health.abnormal': {
        'zh-Hans': '异常', 'en': 'Down', 'de': 'Gestört', 'es': 'Error',
        'fr': 'En panne', 'it': 'Guasto', 'ja': '異常', 'ko': '오류',
        'pt': 'Com falhas', 'ru': 'Ошибка'},
    'win.engines.health.noCredentials': {
        'zh-Hans': '缺少凭证', 'en': 'Missing credentials',
        'de': 'Zugangsdaten fehlen', 'es': 'Faltan credenciales',
        'fr': 'Identifiants manquants', 'it': 'Credenziali mancanti',
        'ja': '資格情報がありません', 'ko': '자격 증명 없음',
        'pt': 'Credenciais em falta', 'ru': 'Нет учётных данных'},
    'win.engines.healthTesting': {
        'zh-Hans': '测试中…', 'en': 'Testing…', 'de': 'Test läuft…', 'es': 'Probando…',
        'fr': 'Test en cours…', 'it': 'Test in corso…', 'ja': 'テスト中…',
        'ko': '테스트 중…', 'pt': 'A testar…', 'ru': 'Проверка…'},
    'win.engines.refreshHealth': {
        'zh-Hans': '刷新健康状态', 'en': 'Refresh health', 'de': 'Status aktualisieren',
        'es': 'Actualizar estado', 'fr': 'Actualiser l’état', 'it': 'Aggiorna stato',
        'ja': '状態を更新', 'ko': '상태 새로 고침', 'pt': 'Atualizar estado',
        'ru': 'Обновить состояние'},
    'win.engines.healthRefreshed': {
        'zh-Hans': '健康状态已刷新', 'en': 'Health refreshed',
        'de': 'Status aktualisiert', 'es': 'Estado actualizado',
        'fr': 'État actualisé', 'it': 'Stato aggiornato', 'ja': '状態を更新しました',
        'ko': '상태를 새로 고쳤습니다', 'pt': 'Estado atualizado',
        'ru': 'Состояние обновлено'},
    'win.engines.ocrMode': {
        'zh-Hans': 'OCR 模式', 'en': 'OCR mode', 'de': 'OCR-Modus', 'es': 'Modo OCR',
        'fr': 'Mode OCR', 'it': 'Modalità OCR', 'ja': 'OCR モード', 'ko': 'OCR 모드',
        'pt': 'Modo OCR', 'ru': 'Режим OCR'},
    'win.engines.ocr.fast': {
        'zh-Hans': '快速（fast，约 1s，行分组）', 'en': 'Fast (~1s, line grouping)',
        'de': 'Schnell (~1 s, Zeilengruppierung)', 'es': 'Rápido (~1 s, agrupación por líneas)',
        'fr': 'Rapide (~1 s, groupement par lignes)', 'it': 'Veloce (~1 s, raggruppamento per righe)',
        'ja': '高速（fast、約1秒、行グループ化）', 'ko': '빠름(fast, 약 1초, 줄 그룹화)',
        'pt': 'Rápido (~1 s, agrupamento por linhas)', 'ru': 'Быстрый (~1 с, группировка строк)'},
    'win.engines.ocr.precise': {
        'zh-Hans': '精准（precise，约 12s，VL-1.5）', 'en': 'Precise (~12s, VL-1.5)',
        'de': 'Präzise (~12 s, VL-1.5)', 'es': 'Preciso (~12 s, VL-1.5)',
        'fr': 'Précis (~12 s, VL-1.5)', 'it': 'Preciso (~12 s, VL-1.5)',
        'ja': '高精度（precise、約12秒、VL-1.5）', 'ko': '정밀(precise, 약 12초, VL-1.5)',
        'pt': 'Preciso (~12 s, VL-1.5)', 'ru': 'Точный (~12 с, VL-1.5)'},
    'win.engines.ocr.cloudKey': {
        'zh-Hans': '云端 API Key', 'en': 'Cloud API key', 'de': 'Cloud-API-Schlüssel',
        'es': 'Clave de API en la nube', 'fr': 'Clé d’API cloud', 'it': 'Chiave API cloud',
        'ja': 'クラウド API キー', 'ko': '클라우드 API 키', 'pt': 'Chave de API na nuvem',
        'ru': 'Облачный ключ API'},
    'win.engines.ocr.cloudUrl': {
        'zh-Hans': '云端 Base URL', 'en': 'Cloud base URL', 'de': 'Cloud-Basis-URL',
        'es': 'URL base en la nube', 'fr': 'URL de base cloud', 'it': 'URL di base cloud',
        'ja': 'クラウド Base URL', 'ko': '클라우드 Base URL', 'pt': 'URL base na nuvem',
        'ru': 'Базовый URL облака'},
    'win.engines.ocr.cloudModel': {
        'zh-Hans': '云端模型 ID', 'en': 'Cloud model ID', 'de': 'Cloud-Modell-ID',
        'es': 'ID de modelo en la nube', 'fr': 'ID de modèle cloud',
        'it': 'ID modello cloud', 'ja': 'クラウドモデル ID', 'ko': '클라우드 모델 ID',
        'pt': 'ID do modelo na nuvem', 'ru': 'ID облачной модели'},
    'win.engines.ocr.paddleHint': {
        'zh-Hans': '本地 OCR 引擎（可选云端）', 'en': 'Local OCR engine (optional cloud)',
        'de': 'Lokale OCR-Engine (optional Cloud)', 'es': 'Motor OCR local (nube opcional)',
        'fr': 'Moteur OCR local (cloud optionnel)', 'it': 'Motore OCR locale (cloud opzionale)',
        'ja': 'ローカル OCR エンジン（クラウド利用可）', 'ko': '로컬 OCR 엔진(클라우드 선택)',
        'pt': 'Motor OCR local (nuvem opcional)', 'ru': 'Локальный OCR-движок (облако по желанию)'},
    'win.engines.vlm.hint': {
        'zh-Hans': '视觉语言模型，用于截图翻译前的图像理解（API Key 存于凭据管理器）',
        'en': 'Vision-language model for image understanding before screenshot translation (API key kept in Credential Manager)',
        'de': 'Vision-Language-Modell zum Bildverstehen vor der Screenshot-Übersetzung (API-Schlüssel in der Anmeldeinformationsverwaltung)',
        'es': 'Modelo de visión-lenguaje para comprender la imagen antes de traducir la captura (la clave de API se guarda en el Administrador de credenciales)',
        'fr': 'Modèle vision-langage pour comprendre l’image avant la traduction de la capture (clé d’API conservée dans le Gestionnaire d’identifiants)',
        'it': 'Modello visione-linguaggio per comprendere l’immagine prima di tradurre lo screenshot (chiave API nel Gestore credenziali)',
        'ja': 'スクリーンショット翻訳前に画像を理解するビジョン言語モデル（API キーは資格情報マネージャーに保存）',
        'ko': '스크린샷 번역 전 이미지를 이해하는 비전-언어 모델(API 키는 자격 증명 관리자에 저장)',
        'pt': 'Modelo visão-linguagem para compreender a imagem antes de traduzir a captura (chave de API no Gestor de Credenciais)',
        'ru': 'Мультимодальная модель для понимания снимка перед переводом (ключ API хранится в диспетчере учётных данных)'},
    'win.engines.saveVlm': {
        'zh-Hans': '保存 VLM 配置', 'en': 'Save VLM configuration',
        'de': 'VLM-Konfiguration speichern', 'es': 'Guardar configuración VLM',
        'fr': 'Enregistrer la configuration VLM', 'it': 'Salva configurazione VLM',
        'ja': 'VLM 設定を保存', 'ko': 'VLM 구성 저장', 'pt': 'Guardar configuração VLM',
        'ru': 'Сохранить настройки VLM'},
    'win.engines.vlmSaved': {
        'zh-Hans': 'VLM 配置已保存', 'en': 'VLM configuration saved',
        'de': 'VLM-Konfiguration gespeichert', 'es': 'Configuración VLM guardada',
        'fr': 'Configuration VLM enregistrée', 'it': 'Configurazione VLM salvata',
        'ja': 'VLM 設定を保存しました', 'ko': 'VLM 구성이 저장되었습니다',
        'pt': 'Configuração VLM guardada', 'ru': 'Настройки VLM сохранены'},
    'win.engines.savePaddle': {
        'zh-Hans': '保存 PaddleOCR 配置', 'en': 'Save PaddleOCR configuration',
        'de': 'PaddleOCR-Konfiguration speichern', 'es': 'Guardar configuración de PaddleOCR',
        'fr': 'Enregistrer la configuration PaddleOCR', 'it': 'Salva configurazione PaddleOCR',
        'ja': 'PaddleOCR 設定を保存', 'ko': 'PaddleOCR 구성 저장',
        'pt': 'Guardar configuração do PaddleOCR', 'ru': 'Сохранить настройки PaddleOCR'},
    'win.engines.paddleSaved': {
        'zh-Hans': 'PaddleOCR 配置已保存', 'en': 'PaddleOCR configuration saved',
        'de': 'PaddleOCR-Konfiguration gespeichert', 'es': 'Configuración de PaddleOCR guardada',
        'fr': 'Configuration PaddleOCR enregistrée', 'it': 'Configurazione PaddleOCR salvata',
        'ja': 'PaddleOCR 設定を保存しました', 'ko': 'PaddleOCR 구성이 저장되었습니다',
        'pt': 'Configuração do PaddleOCR guardada', 'ru': 'Настройки PaddleOCR сохранены'},
    'win.engines.editCompatible': {
        'zh-Hans': '编辑兼容引擎', 'en': 'Edit compatible engine',
        'de': 'Kompatible Engine bearbeiten', 'es': 'Editar motor compatible',
        'fr': 'Modifier le moteur compatible', 'it': 'Modifica motore compatibile',
        'ja': '互換エンジンを編集', 'ko': '호환 엔진 편집',
        'pt': 'Editar motor compatível', 'ru': 'Изменить совместимый движок'},
    'win.engines.compatibleSaved': {
        'zh-Hans': '兼容实例已保存', 'en': 'Compatible instance saved',
        'de': 'Kompatible Instanz gespeichert', 'es': 'Instancia compatible guardada',
        'fr': 'Instance compatible enregistrée', 'it': 'Istanza compatibile salvata',
        'ja': '互換インスタンスを保存しました', 'ko': '호환 인스턴스가 저장되었습니다',
        'pt': 'Instância compatível guardada', 'ru': 'Совместимый экземпляр сохранён'},
    'win.engines.compatibleDeleted': {
        'zh-Hans': '兼容实例已删除', 'en': 'Compatible instance deleted',
        'de': 'Kompatible Instanz gelöscht', 'es': 'Instancia compatible eliminada',
        'fr': 'Instance compatible supprimée', 'it': 'Istanza compatibile eliminata',
        'ja': '互換インスタンスを削除しました', 'ko': '호환 인스턴스가 삭제되었습니다',
        'pt': 'Instância compatível eliminada', 'ru': 'Совместимый экземпляр удалён'},
    'win.engines.compatibleLimit': {
        'zh-Hans': '兼容引擎已达上限（5）', 'en': 'Compatible engine limit reached (5)',
        'de': 'Limit kompatibler Engines erreicht (5)', 'es': 'Límite de motores compatibles alcanzado (5)',
        'fr': 'Limite de moteurs compatibles atteinte (5)', 'it': 'Limite motori compatibili raggiunto (5)',
        'ja': '互換エンジンの上限に達しました（5）', 'ko': '호환 엔진 한도에 도달했습니다(5)',
        'pt': 'Limite de motores compatíveis atingido (5)', 'ru': 'Достигнут предел совместимых движков (5)'},
    'win.engines.engineSaved': {
        'zh-Hans': '引擎配置已保存', 'en': 'Engine configuration saved',
        'de': 'Engine-Konfiguration gespeichert', 'es': 'Configuración del motor guardada',
        'fr': 'Configuration du moteur enregistrée', 'it': 'Configurazione del motore salvata',
        'ja': 'エンジン設定を保存しました', 'ko': '엔진 구성이 저장되었습니다',
        'pt': 'Configuração do motor guardada', 'ru': 'Настройки движка сохранены'},
    'win.engines.toggleSaved': {
        'zh-Hans': '引擎开关已保存', 'en': 'Engine switch saved',
        'de': 'Engine-Schalter gespeichert', 'es': 'Interruptor del motor guardado',
        'fr': 'Commutateur du moteur enregistré', 'it': 'Interruttore del motore salvato',
        'ja': 'エンジンのスイッチを保存しました', 'ko': '엔진 스위치가 저장되었습니다',
        'pt': 'Interruptor do motor guardado', 'ru': 'Переключатель движка сохранён'},
    'win.engines.policySaved': {
        'zh-Hans': '翻译策略已保存', 'en': 'Translation policy saved',
        'de': 'Übersetzungsstrategie gespeichert', 'es': 'Política de traducción guardada',
        'fr': 'Stratégie de traduction enregistrée', 'it': 'Criteri di traduzione salvati',
        'ja': '翻訳ポリシーを保存しました', 'ko': '번역 정책이 저장되었습니다',
        'pt': 'Política de tradução guardada', 'ru': 'Схема перевода сохранена'},
    'win.engines.keyStored': {
        'zh-Hans': '已保存密钥（输入可替换）', 'en': 'Key saved (type to replace)',
        'de': 'Schlüssel gespeichert (Eingabe ersetzt)', 'es': 'Clave guardada (escribe para reemplazar)',
        'fr': 'Clé enregistrée (saisir pour remplacer)', 'it': 'Chiave salvata (digita per sostituire)',
        'ja': 'キー保存済み（入力で置き換え）', 'ko': '키 저장됨(입력하여 교체)',
        'pt': 'Chave guardada (escrever para substituir)', 'ru': 'Ключ сохранён (введите новый для замены)'},
    'win.engines.keyNotSet': {
        'zh-Hans': '未设置密钥', 'en': 'No key set', 'de': 'Kein Schlüssel gesetzt',
        'es': 'Sin clave', 'fr': 'Aucune clé définie', 'it': 'Nessuna chiave impostata',
        'ja': 'キー未設定', 'ko': '키 미설정', 'pt': 'Sem chave definida',
        'ru': 'Ключ не задан'},
    'win.engines.legendGreen': {
        'zh-Hans': '连接正常（绿）', 'en': 'Reachable (green)', 'de': 'Erreichbar (grün)',
        'es': 'Accesible (verde)', 'fr': 'Joignable (vert)', 'it': 'Raggiungibile (verde)',
        'ja': '接続正常（緑）', 'ko': '연결 정상(초록)', 'pt': 'Acessível (verde)',
        'ru': 'Доступен (зелёный)'},
    'win.engines.legendOrange': {
        'zh-Hans': '缺少凭证（橙）', 'en': 'Missing credentials (orange)',
        'de': 'Zugangsdaten fehlen (orange)', 'es': 'Faltan credenciales (naranja)',
        'fr': 'Identifiants manquants (orange)', 'it': 'Credenziali mancanti (arancione)',
        'ja': '資格情報なし（橙）', 'ko': '자격 증명 없음(주황)',
        'pt': 'Credenciais em falta (laranja)', 'ru': 'Нет учётных данных (оранжевый)'},
    'win.engines.legendRed': {
        'zh-Hans': '连接失败（红）', 'en': 'Unreachable (red)', 'de': 'Nicht erreichbar (rot)',
        'es': 'Inaccesible (rojo)', 'fr': 'Injoignable (rouge)', 'it': 'Irraggiungibile (rosso)',
        'ja': '接続失敗（赤）', 'ko': '연결 실패(빨강)', 'pt': 'Inacessível (vermelho)',
        'ru': 'Недоступен (красный)'},
    'win.engines.configureEngine': {
        'zh-Hans': '配置 %@', 'en': 'Configure %@', 'de': '%@ konfigurieren',
        'es': 'Configurar %@', 'fr': 'Configurer %@', 'it': 'Configura %@',
        'ja': '%@ を設定', 'ko': '%@ 구성', 'pt': 'Configurar %@', 'ru': 'Настроить %@'},
    'win.engines.presets': {
        'zh-Hans': '预设', 'en': 'Preset', 'de': 'Voreinstellung', 'es': 'Preajuste',
        'fr': 'Préréglage', 'it': 'Preset', 'ja': 'プリセット', 'ko': '프리셋',
        'pt': 'Predefinição', 'ru': 'Профиль'},
    'win.engines.apiKey': {
        'zh-Hans': 'API Key', 'en': 'API key', 'de': 'API-Schlüssel',
        'es': 'Clave de API', 'fr': 'Clé d’API', 'it': 'Chiave API', 'ja': 'API キー',
        'ko': 'API 키', 'pt': 'Chave de API', 'ru': 'Ключ API'},
    'win.engines.noApiKeyNeeded': {
        'zh-Hans': '此引擎无需密钥', 'en': 'This engine needs no key',
        'de': 'Diese Engine braucht keinen Schlüssel', 'es': 'Este motor no necesita clave',
        'fr': 'Ce moteur n’a pas besoin de clé', 'it': 'Questo motore non richiede chiavi',
        'ja': 'このエンジンにキーは不要です', 'ko': '이 엔진에는 키가 필요 없습니다',
        'pt': 'Este motor não precisa de chave', 'ru': 'Этому движку ключ не нужен'},
    # ---- prompts page ----
    'win.prompts.engine.hint': {
        'zh-Hans': '为单个引擎设置自定义 Prompt（覆盖内置默认）',
        'en': 'Per-engine custom prompts (override the built-in default)',
        'de': 'Eigene Prompts pro Engine (überschreibt den Standard)',
        'es': 'Prompts personalizados por motor (anulan el predeterminado)',
        'fr': 'Prompts personnalisés par moteur (remplacent la valeur par défaut)',
        'it': 'Prompt personalizzati per motore (sostituiscono il predefinito)',
        'ja': 'エンジンごとにカスタム Prompt を設定（内蔵の既定値を上書き）',
        'ko': '엔진별 사용자 지정 프롬프트(기본값 대체)',
        'pt': 'Prompts personalizados por motor (substituem o predefinido)',
        'ru': 'Свои промпты для каждого движка (переопределяют стандартный)'},
    'win.prompts.compatible.hint': {
        'zh-Hans': '按 OpenAI 兼容实例单独设置', 'en': 'Set per OpenAI-compatible instance',
        'de': 'Je OpenAI-kompatibler Instanz einstellbar',
        'es': 'Configura por instancia compatible con OpenAI',
        'fr': 'Réglable par instance compatible OpenAI',
        'it': 'Imposta per ogni istanza compatibile OpenAI',
        'ja': 'OpenAI 互換インスタンスごとに設定',
        'ko': 'OpenAI 호환 인스턴스별로 설정',
        'pt': 'Definir por instância compatível com OpenAI',
        'ru': 'Отдельно для каждого OpenAI-совместимого экземпляра'},
    'win.prompts.scene.hint': {
        'zh-Hans': '按使用场景设置（优先级：场景 > 引擎 > 默认）',
        'en': 'Per scene (priority: scene > engine > default)',
        'de': 'Je Szenario (Priorität: Szenario > Engine > Standard)',
        'es': 'Por escenario (prioridad: escenario > motor > predeterminado)',
        'fr': 'Par scénario (priorité : scénario > moteur > défaut)',
        'it': 'Per scena (priorità: scena > motore > predefinito)',
        'ja': 'シーンごとに設定（優先度: シーン > エンジン > 既定）',
        'ko': '장면별 설정(우선순위: 장면 > 엔진 > 기본값)',
        'pt': 'Por cenário (prioridade: cenário > motor > predefinido)',
        'ru': 'По сценарию (приоритет: сценарий > движок > стандарт)'},
    'win.prompts.default.title': {
        'zh-Hans': '默认 Prompt 预览', 'en': 'Default prompt preview',
        'de': 'Standard-Prompt-Vorschau', 'es': 'Vista previa del prompt predeterminado',
        'fr': 'Aperçu du prompt par défaut', 'it': 'Anteprima prompt predefinito',
        'ja': '既定 Prompt のプレビュー', 'ko': '기본 프롬프트 미리보기',
        'pt': 'Pré-visualização do prompt predefinido',
        'ru': 'Просмотр стандартного промпта'},
    'win.prompts.fallback': {
        'zh-Hans': '通用默认', 'en': 'Shared default', 'de': 'Gemeinsamer Standard',
        'es': 'Predeterminado común', 'fr': 'Défaut commun', 'it': 'Predefinito comune',
        'ja': '共通の既定値', 'ko': '공통 기본값', 'pt': 'Predefinição comum',
        'ru': 'Общий по умолчанию'},
    'win.prompts.customSet': {
        'zh-Hans': '已设置自定义 Prompt', 'en': 'Custom prompt set',
        'de': 'Eigener Prompt gesetzt', 'es': 'Prompt personalizado establecido',
        'fr': 'Prompt personnalisé défini', 'it': 'Prompt personalizzato impostato',
        'ja': 'カスタム Prompt を設定済み', 'ko': '사용자 지정 프롬프트 설정됨',
        'pt': 'Prompt personalizado definido', 'ru': 'Задан свой промпт'},
    'win.prompts.insertVariable': {
        'zh-Hans': '插入 %@', 'en': 'Insert %@', 'de': '%@ einfügen', 'es': 'Insertar %@',
        'fr': 'Insérer %@', 'it': 'Inserisci %@', 'ja': '%@ を挿入', 'ko': '%@ 삽입',
        'pt': 'Inserir %@', 'ru': 'Вставить %@'},
    'win.prompts.placeholder': {
        'zh-Hans': '（空 = 使用默认 Prompt）', 'en': '(empty = default prompt)',
        'de': '(leer = Standard-Prompt)', 'es': '(vacío = prompt predeterminado)',
        'fr': '(vide = prompt par défaut)', 'it': '(vuoto = prompt predefinito)',
        'ja': '（空 = 既定の Prompt）', 'ko': '(비어 있음 = 기본 프롬프트)',
        'pt': '(vazio = prompt predefinido)', 'ru': '(пусто = стандартный промпт)'},
    # ---- onboarding ----
    'win.onboarding.step2.title': {
        'zh-Hans': '隐私与网络', 'en': 'Privacy & network', 'de': 'Datenschutz und Netzwerk',
        'es': 'Privacidad y red', 'fr': 'Confidentialité et réseau',
        'it': 'Privacy e rete', 'ja': 'プライバシーとネットワーク', 'ko': '개인정보 및 네트워크',
        'pt': 'Privacidade e rede', 'ru': 'Приватность и сеть'},
    'win.onboarding.step2.hint': {
        'zh-Hans': '当前启用的翻译引擎及联网情况',
        'en': 'Enabled translation engines and their network use',
        'de': 'Aktivierte Übersichts-Engines und ihre Netzwerknutzung',
        'es': 'Motores de traducción activados y su uso de red',
        'fr': 'Moteurs de traduction activés et leur usage du réseau',
        'it': 'Motori di traduzione attivi e il loro uso della rete',
        'ja': '現在有効な翻訳エンジンと通信状況',
        'ko': '현재 활성화된 번역 엔진과 네트워크 사용 현황',
        'pt': 'Motores de tradução ativados e o uso da rede',
        'ru': 'Включённые движки перевода и их сетевая активность'},
    'win.onboarding.privacy.local': {
        'zh-Hans': '本地引擎 · 不出网', 'en': 'Local engine · nothing leaves this device',
        'de': 'Lokale Engine · verlässt dieses Gerät nicht',
        'es': 'Motor local · nada sale de este equipo',
        'fr': 'Moteur local · rien ne quitte cet appareil',
        'it': 'Motore locale · nulla lascia questo dispositivo',
        'ja': 'ローカルエンジン · 外部へ送信しません',
        'ko': '로컬 엔진 · 외부로 전송하지 않음',
        'pt': 'Motor local · nada sai deste dispositivo',
        'ru': 'Локальный движок · данные не покидают устройство'},
    'win.onboarding.privacy.cloud': {
        'zh-Hans': '云端引擎 · 文本将发送到 %@',
        'en': 'Cloud engine · text is sent to %@',
        'de': 'Cloud-Engine · Text wird an %@ gesendet',
        'es': 'Motor en la nube · el texto se envía a %@',
        'fr': 'Moteur cloud · le texte est envoyé à %@',
        'it': 'Motore cloud · il testo viene inviato a %@',
        'ja': 'クラウドエンジン · テキストを %@ へ送信します',
        'ko': '클라우드 엔진 · 텍스트가 %@로 전송됩니다',
        'pt': 'Motor na nuvem · o texto é enviado para %@',
        'ru': 'Облачный движок · текст уходит на %@'},
    'win.onboarding.privacy.cloudDefault': {
        'zh-Hans': '云端引擎 · 文本将发送到默认端点',
        'en': 'Cloud engine · text is sent to the default endpoint',
        'de': 'Cloud-Engine · Text geht an den Standard-Endpunkt',
        'es': 'Motor en la nube · el texto se envía al endpoint predeterminado',
        'fr': 'Moteur cloud · le texte est envoyé au point de terminaison par défaut',
        'it': 'Motore cloud · il testo va all’endpoint predefinito',
        'ja': 'クラウドエンジン · テキストを既定のエンドポイントへ送信します',
        'ko': '클라우드 엔진 · 텍스트가 기본 엔드포인트로 전송됩니다',
        'pt': 'Motor na nuvem · o texto é enviado para o endpoint predefinido',
        'ru': 'Облачный движок · текст уходит на адрес по умолчанию'},
    'win.onboarding.privacy.noEngines': {
        'zh-Hans': '尚未启用任何翻译引擎', 'en': 'No translation engines enabled yet',
        'de': 'Noch keine Übersetzungs-Engines aktiviert',
        'es': 'Aún no hay motores de traducción activados',
        'fr': 'Aucun moteur de traduction activé pour l’instant',
        'it': 'Nessun motore di traduzione attivato',
        'ja': '翻訳エンジンが有効化されていません',
        'ko': '아직 활성화된 번역 엔진이 없습니다',
        'pt': 'Ainda sem motores de tradução ativados',
        'ru': 'Движки перевода ещё не включены'},
    'win.onboarding.step3.title': {
        'zh-Hans': '默认翻译链', 'en': 'Default translation chain',
        'de': 'Standard-Übersetzungskette', 'es': 'Cadena de traducción predeterminada',
        'fr': 'Chaîne de traduction par défaut', 'it': 'Catena di traduzione predefinita',
        'ja': '既定の翻訳チェーン', 'ko': '기본 번역 체인',
        'pt': 'Cadeia de tradução predefinida', 'ru': 'Цепочка перевода по умолчанию'},
    'win.onboarding.step3.hint': {
        'zh-Hans': '选择截图翻译的默认引擎，稍后可在 设置→引擎 修改',
        'en': 'Pick the default engine for screenshot translation; change it later in Settings → Engines',
        'de': 'Standard-Engine für die Screenshot-Übersetzung wählen; später unter Einstellungen → Engines änderbar',
        'es': 'Elige el motor predeterminado para traducir capturas; cámbialo luego en Ajustes → Motores',
        'fr': 'Choisissez le moteur par défaut pour traduire les captures ; modifiable ensuite dans Paramètres → Moteurs',
        'it': 'Scegli il motore predefinito per la traduzione degli screenshot; cambialo poi in Impostazioni → Motori',
        'ja': 'スクリーンショット翻訳の既定エンジンを選択してください。後で 設定→エンジン から変更できます',
        'ko': '스크린샷 번역의 기본 엔진을 선택하세요. 나중에 설정→엔진에서 변경할 수 있습니다',
        'pt': 'Escolhe o motor predefinido para traduzir capturas; muda depois em Definições → Motores',
        'ru': 'Выберите движок по умолчанию для перевода снимков; потом можно изменить в Настройки → Движки'},
    'win.onboarding.chain.mtran.title': {
        'zh-Hans': 'MTranServer（本地）', 'en': 'MTranServer (local)',
        'de': 'MTranServer (lokal)', 'es': 'MTranServer (local)',
        'fr': 'MTranServer (local)', 'it': 'MTranServer (locale)',
        'ja': 'MTranServer（ローカル）', 'ko': 'MTranServer(로컬)',
        'pt': 'MTranServer (local)', 'ru': 'MTranServer (локальный)'},
    'win.onboarding.chain.mtran.hint': {
        'zh-Hans': '需在本机 8989 端口运行 MTranServer；文本不出网',
        'en': 'Needs MTranServer running locally on port 8989; text never leaves this device',
        'de': 'Benötigt MTranServer lokal auf Port 8989; Text verlässt das Gerät nicht',
        'es': 'Requiere MTranServer en el puerto 8989 local; el texto no sale del equipo',
        'fr': 'Nécessite MTranServer en local sur le port 8989 ; le texte ne quitte pas l’appareil',
        'it': 'Richiede MTranServer in locale sulla porta 8989; il testo non lascia il dispositivo',
        'ja': 'ローカルの 8989 番ポートで MTranServer が動作している必要があります。テキストは外部に送信されません',
        'ko': '로컬 8989 포트에서 MTranServer가 실행 중이어야 합니다. 텍스트는 외부로 전송되지 않습니다',
        'pt': 'Precisa do MTranServer local na porta 8989; o texto não sai do dispositivo',
        'ru': 'Требуется MTranServer на порту 8989 локально; текст не покидает устройство'},
    'win.onboarding.chain.ollama.title': {
        'zh-Hans': 'Ollama（本地）', 'en': 'Ollama (local)', 'de': 'Ollama (lokal)',
        'es': 'Ollama (local)', 'fr': 'Ollama (local)', 'it': 'Ollama (locale)',
        'ja': 'Ollama（ローカル）', 'ko': 'Ollama(로컬)', 'pt': 'Ollama (local)',
        'ru': 'Ollama (локальный)'},
    'win.onboarding.chain.ollama.hint': {
        'zh-Hans': '本机 Ollama 服务（默认端口 11434）；文本不出网',
        'en': 'Uses a local Ollama service (default port 11434); text never leaves this device',
        'de': 'Nutzt einen lokalen Ollama-Dienst (Standardport 11434); Text verlässt das Gerät nicht',
        'es': 'Usa un servicio Ollama local (puerto 11434); el texto no sale del equipo',
        'fr': 'Utilise un service Ollama local (port 11434 par défaut) ; le texte ne quitte pas l’appareil',
        'it': 'Usa un servizio Ollama locale (porta 11434); il testo non lascia il dispositivo',
        'ja': 'ローカルの Ollama サービスを使用します（既定ポート 11434）。テキストは外部に送信されません',
        'ko': '로컬 Ollama 서비스를 사용합니다(기본 포트 11434). 텍스트는 외부로 전송되지 않습니다',
        'pt': 'Usa um serviço Ollama local (porta 11434); o texto não sai do dispositivo',
        'ru': 'Локальный сервис Ollama (порт 11434); текст не покидает устройство'},
    'win.onboarding.chain.cloud.title': {
        'zh-Hans': '云引擎（OpenAI 兼容）', 'en': 'Cloud engine (OpenAI-compatible)',
        'de': 'Cloud-Engine (OpenAI-kompatibel)',
        'es': 'Motor en la nube (compatible con OpenAI)',
        'fr': 'Moteur cloud (compatible OpenAI)',
        'it': 'Motore cloud (compatibile OpenAI)',
        'ja': 'クラウドエンジン（OpenAI 互換）', 'ko': '클라우드 엔진(OpenAI 호환)',
        'pt': 'Motor na nuvem (compatível com OpenAI)',
        'ru': 'Облачный движок (OpenAI-совместимый)'},
    'win.onboarding.chain.cloud.hint': {
        'zh-Hans': '翻译质量更好；需在 设置→引擎 配置 API Key，文本会发送到云端',
        'en': 'Better quality; needs an API key in Settings → Engines, and text goes to the cloud',
        'de': 'Bessere Qualität; braucht einen API-Schlüssel unter Einstellungen → Engines; Text geht in die Cloud',
        'es': 'Mejor calidad; necesita una clave de API en Ajustes → Motores, y el texto se envía a la nube',
        'fr': 'Meilleure qualité ; nécessite une clé d’API dans Paramètres → Moteurs, et le texte part dans le cloud',
        'it': 'Qualità migliore; richiede una chiave API in Impostazioni → Motori, e il testo va nel cloud',
        'ja': 'より高品質な翻訳。設定→エンジンで API キーが必要で、テキストはクラウドへ送信されます',
        'ko': '더 나은 품질. 설정→엔진에서 API 키가 필요하며 텍스트는 클라우드로 전송됩니다',
        'pt': 'Melhor qualidade; precisa de uma chave de API em Definições → Motores, e o texto vai para a nuvem',
        'ru': 'Лучшее качество; нужен ключ API в Настройки → Движки, текст уходит в облако'},
    'win.onboarding.skip': {
        'zh-Hans': '跳过', 'en': 'Skip', 'de': 'Überspringen', 'es': 'Omitir',
        'fr': 'Passer', 'it': 'Salta', 'ja': 'スキップ', 'ko': '건너뛰기',
        'pt': 'Ignorar', 'ru': 'Пропустить'},
    'win.onboarding.hotkeyHint': {
        'zh-Hans': '可在 设置→快捷键 修改这些热键',
        'en': 'Change these shortcuts in Settings → Shortcuts',
        'de': 'Diese Kombinationen unter Einstellungen → Tastenkombinationen ändern',
        'es': 'Cambia estos atajos en Ajustes → Atajos de teclado',
        'fr': 'Modifiez ces raccourcis dans Paramètres → Raccourcis',
        'it': 'Modifica queste scorciatoie in Impostazioni → Scorciatoie',
        'ja': 'これらのショートカットは 設定→ショートカット で変更できます',
        'ko': '이 단축키는 설정→단축키에서 변경할 수 있습니다',
        'pt': 'Altera estes atalhos em Definições → Atalhos',
        'ru': 'Изменить эти клавиши можно в Настройки → Горячие клавиши'},
    # ---- notifications + update ----
    'win.notify.updateTitle': {
        'zh-Hans': 'TransFrame 更新', 'en': 'TransFrame update',
        'de': 'TransFrame-Update', 'es': 'Actualización de TransFrame',
        'fr': 'Mise à jour de TransFrame', 'it': 'Aggiornamento di TransFrame',
        'ja': 'TransFrame の更新', 'ko': 'TransFrame 업데이트',
        'pt': 'Atualização do TransFrame', 'ru': 'Обновление TransFrame'},
    'win.notify.uptodate': {
        'zh-Hans': '当前已是最新版本（%@）', 'en': 'You’re up to date (%@)',
        'de': 'Aktuellste Version installiert (%@)',
        'es': 'Ya tienes la última versión (%@)',
        'fr': 'Vous êtes à jour (%@)', 'it': 'Versione più recente (%@)',
        'ja': '最新バージョンです（%@）', 'ko': '최신 버전입니다(%@)',
        'pt': 'Já está na versão mais recente (%@)', 'ru': 'Установлена последняя версия (%@)'},
    'win.update.unpackaged': {
        'zh-Hans': '当前为免安装版，暂无自动更新。请通过发布页提供的 .appinstaller 文件安装 MSIX 版本以启用自动更新。',
        'en': 'This is the portable build; automatic updates are unavailable. Install the MSIX build via the .appinstaller file on the releases page to enable them.',
        'de': 'Dies ist die portable Version; automatische Updates sind nicht verfügbar. Installiere die MSIX-Version über die .appinstaller-Datei der Release-Seite, um sie zu aktivieren.',
        'es': 'Esta es la versión portátil; no hay actualizaciones automáticas. Instala la versión MSIX mediante el archivo .appinstaller de la página de publicaciones para activarlas.',
        'fr': 'Version portable : pas de mises à jour automatiques. Installez la version MSIX via le fichier .appinstaller de la page des versions pour les activer.',
        'it': 'Questa è la build portabile; aggiornamenti automatici non disponibili. Installa la build MSIX tramite il file .appinstaller nella pagina delle release per attivarli.',
        'ja': 'ポータブル版のため自動更新は利用できません。リリースページの .appinstaller ファイルから MSIX 版をインストールすると有効になります。',
        'ko': '휴대용 빌드에는 자동 업데이트가 없습니다. 릴리스 페이지의 .appinstaller 파일로 MSIX 버전을 설치하면 활성화됩니다.',
        'pt': 'Esta é a compilação portátil; sem atualizações automáticas. Instale a compilação MSIX através do ficheiro .appinstaller da página de lançamentos para as ativar.',
        'ru': 'Это портативная сборка — автообновления недоступны. Установите MSIX-сборку через файл .appinstaller со страницы релизов, чтобы включить их.'},
    'win.update.noFeed': {
        'zh-Hans': '此安装未关联自动更新源。请通过发布页提供的 .appinstaller 文件安装，以启用自动更新。',
        'en': 'This install has no update feed. Install via the .appinstaller file from the releases page to enable updates.',
        'de': 'Diese Installation hat keine Update-Quelle. Installiere über die .appinstaller-Datei der Release-Seite, um Updates zu aktivieren.',
        'es': 'Esta instalación no tiene fuente de actualizaciones. Instala mediante el archivo .appinstaller de la página de publicaciones para activarlas.',
        'fr': 'Cette installation n’a pas de source de mise à jour. Installez via le fichier .appinstaller de la page des versions pour les activer.',
        'it': 'Questa installazione non ha una fonte di aggiornamento. Installa tramite il file .appinstaller della pagina delle release per attivarli.',
        'ja': 'このインストールには更新ソースが関連付けられていません。リリースページの .appinstaller ファイルからインストールすると有効になります。',
        'ko': '이 설치에는 업데이트 소스가 연결되어 있지 않습니다. 릴리스 페이지의 .appinstaller 파일로 설치하면 활성화됩니다.',
        'pt': 'Esta instalação não tem fonte de atualizações. Instale através do ficheiro .appinstaller da página de lançamentos para as ativar.',
        'ru': 'У этой установки нет источника обновлений. Установите через .appinstaller со страницы релизов, чтобы включить обновления.'},
    'win.update.available': {
        'zh-Hans': '发现新版本。现在打开 Windows 应用安装程序更新？',
        'en': 'A new version is available. Open Windows App Installer to update now?',
        'de': 'Eine neue Version ist verfügbar. Windows App Installer jetzt zum Aktualisieren öffnen?',
        'es': 'Hay una nueva versión. ¿Abrir el Instalador de aplicación de Windows para actualizar?',
        'fr': 'Une nouvelle version est disponible. Ouvrir Windows App Installer pour mettre à jour ?',
        'it': 'È disponibile una nuova versione. Aprire Windows App Installer per aggiornare?',
        'ja': '新しいバージョンがあります。Windows アプリインストーラーを開いて更新しますか？',
        'ko': '새 버전이 있습니다. Windows 앱 설치 관리자를 열어 업데이트할까요?',
        'pt': 'Está disponível uma nova versão. Abrir o Instalador de Aplicações do Windows para atualizar?',
        'ru': 'Доступна новая версия. Открыть установщик приложений Windows и обновить?'},
    'win.update.openFailed': {
        'zh-Hans': '无法打开 Windows 应用安装程序，请检查是否已安装 App Installer。',
        'en': 'Cannot open Windows App Installer; check that App Installer is installed.',
        'de': 'Windows App Installer lässt sich nicht öffnen; prüfe, ob App Installer installiert ist.',
        'es': 'No se puede abrir el Instalador de aplicación de Windows; comprueba que App Installer esté instalado.',
        'fr': 'Impossible d’ouvrir Windows App Installer ; vérifiez qu’App Installer est installé.',
        'it': 'Impossibile aprire Windows App Installer; verifica che App Installer sia installato.',
        'ja': 'Windows アプリインストーラーを開けません。App Installer がインストールされているか確認してください。',
        'ko': 'Windows 앱 설치 관리자를 열 수 없습니다. App Installer가 설치되어 있는지 확인하세요.',
        'pt': 'Não é possível abrir o Instalador de Aplicações do Windows; verifique se o App Installer está instalado.',
        'ru': 'Не удалось открыть установщик приложений Windows; проверьте, установлен ли App Installer.'},
    'win.update.httpsRequired': {
        'zh-Hans': '更新源必须使用 HTTPS。', 'en': 'The update source must use HTTPS.',
        'de': 'Die Update-Quelle muss HTTPS verwenden.', 'es': 'La fuente de actualización debe usar HTTPS.',
        'fr': 'La source de mise à jour doit utiliser HTTPS.',
        'it': 'La fonte di aggiornamento deve usare HTTPS.',
        'ja': '更新ソースは HTTPS を使用する必要があります。',
        'ko': '업데이트 소스는 HTTPS를 사용해야 합니다.',
        'pt': 'A fonte de atualização tem de usar HTTPS.',
        'ru': 'Источник обновлений должен использовать HTTPS.'},
    'win.update.checkFailed': {
        'zh-Hans': '检查更新失败：%@', 'en': 'Update check failed: %@',
        'de': 'Updateprüfung fehlgeschlagen: %@', 'es': 'Error al buscar actualizaciones: %@',
        'fr': 'Échec de la recherche : %@', 'it': 'Verifica aggiornamenti non riuscita: %@',
        'ja': '更新の確認に失敗しました: %@', 'ko': '업데이트 확인 실패: %@',
        'pt': 'Falha ao verificar atualizações: %@', 'ru': 'Не удалось проверить обновления: %@'},
    'win.update.checkFailedGeneric': {
        'zh-Hans': '检查更新失败，请稍后重试。', 'en': 'Update check failed; try again later.',
        'de': 'Updateprüfung fehlgeschlagen; später erneut versuchen.',
        'es': 'Error al buscar actualizaciones; inténtalo más tarde.',
        'fr': 'Échec de la recherche de mises à jour ; réessayez plus tard.',
        'it': 'Verifica aggiornamenti non riuscita; riprova più tardi.',
        'ja': '更新の確認に失敗しました。後でもう一度お試しください。',
        'ko': '업데이트 확인에 실패했습니다. 나중에 다시 시도하세요.',
        'pt': 'Falha ao verificar atualizações; tente novamente mais tarde.',
        'ru': 'Не удалось проверить обновления; попробуйте позже.'},
    'win.update.unsure': {
        'zh-Hans': '暂时无法确认更新，请检查网络后重试。',
        'en': 'Cannot confirm updates right now; check the network and retry.',
        'de': 'Updates lassen sich gerade nicht bestätigen; Netzwerk prüfen und erneut versuchen.',
        'es': 'No se pueden confirmar actualizaciones ahora; revisa la red y reintenta.',
        'fr': 'Impossible de confirmer les mises à jour ; vérifiez le réseau et réessayez.',
        'it': 'Impossibile confermare gli aggiornamenti; controlla la rete e riprova.',
        'ja': '更新を確認できません。ネットワークを確認して再試行してください。',
        'ko': '지금은 업데이트를 확인할 수 없습니다. 네트워크를 확인하고 다시 시도하세요.',
        'pt': 'Não é possível confirmar atualizações agora; verifique a rede e tente de novo.',
        'ru': 'Сейчас не удалось проверить обновления; проверьте сеть и повторите.'},
    # ---- engines page 补充（分组标签/健康检查状态行）----
    'win.engines.healthChecking': {
        'zh-Hans': '正在检查引擎健康状态…', 'en': 'Checking engine health…',
        'de': 'Motorzustand wird geprüft…', 'es': 'Comprobando el estado de los motores…',
        'fr': 'Vérification de l’état des moteurs…',
        'it': 'Verifica dello stato dei motori in corso…',
        'ja': 'エンジンの状態を確認中…', 'ko': '엔진 상태 확인 중…',
        'pt': 'A verificar o estado dos motores…', 'ru': 'Проверка состояния движков…'},
    'win.engines.group.builtin': {
        'zh-Hans': '内置引擎', 'en': 'Built-in', 'de': 'Integriert', 'es': 'Integrados',
        'fr': 'Intégrés', 'it': 'Integrati', 'ja': '内蔵エンジン', 'ko': '내장 엔진',
        'pt': 'Integrados', 'ru': 'Встроенные'},
    'win.engines.group.llm': {
        'zh-Hans': 'LLM', 'en': 'LLM', 'de': 'LLM', 'es': 'LLM', 'fr': 'LLM',
        'it': 'LLM', 'ja': 'LLM', 'ko': 'LLM', 'pt': 'LLM', 'ru': 'LLM'},
    'win.engines.group.other': {
        'zh-Hans': '其他', 'en': 'Other', 'de': 'Weitere', 'es': 'Otros', 'fr': 'Autres',
        'it': 'Altri', 'ja': 'その他', 'ko': '기타', 'pt': 'Outros', 'ru': 'Другие'},
    # ---- prompts page 补充（场景显示名/翻译插入默认）----
    'win.prompts.scene.screenshot': {
        'zh-Hans': '截图翻译', 'en': 'Screenshot translation', 'de': 'Screenshot-Übersetzung',
        'es': 'Traducción de capturas', 'fr': 'Traduction de captures',
        'it': 'Traduzione screenshot', 'ja': 'スクリーンショット翻訳', 'ko': '스크린샷 번역',
        'pt': 'Tradução de capturas', 'ru': 'Перевод скриншотов'},
    'win.prompts.scene.textSelection': {
        'zh-Hans': '划词翻译', 'en': 'Text selection translation',
        'de': 'Textauswahl-Übersetzung', 'es': 'Traducción de texto seleccionado',
        'fr': 'Traduction de la sélection', 'it': 'Traduzione selezione testo',
        'ja': 'テキスト選択翻訳', 'ko': '텍스트 선택 번역',
        'pt': 'Tradução de texto selecionado', 'ru': 'Перевод выделенного текста'},
    'win.prompts.scene.translateInsert': {
        'zh-Hans': '翻译插入', 'en': 'Translate & insert', 'de': 'Übersetzen & einfügen',
        'es': 'Traducir e insertar', 'fr': 'Traduire et insérer', 'it': 'Traduci e inserisci',
        'ja': '翻訳して挿入', 'ko': '번역하여 삽입', 'pt': 'Traduzir e inserir',
        'ru': 'Перевести и вставить'},
    'win.prompts.insertDefault': {
        'zh-Hans': '翻译插入默认', 'en': 'Translate & insert default',
        'de': 'Standard für Übersetzen & einfügen',
        'es': 'Predeterminado de traducir e insertar',
        'fr': 'Valeur par défaut de traduire et insérer',
        'it': 'Predefinito per traduci e inserisci', 'ja': '翻訳挿入のデフォルト',
        'ko': '번역 삽입 기본값', 'pt': 'Predefinido de traduzir e inserir',
        'ru': 'По умолчанию для перевода и вставки'},
    # ---- onboarding 补充（重跑引导后的应用提示）----
    'win.onboarding.applied': {
        'zh-Hans': '默认翻译链已更新，下次启动生效', 'en': 'Default chain updated; takes effect on next launch',
        'de': 'Standardkette aktualisiert; gilt ab dem nächsten Start',
        'es': 'Cadena predeterminada actualizada; surtirá efecto al reiniciar',
        'fr': 'Chaîne par défaut mise à jour ; effective au prochain démarrage',
        'it': 'Catena predefinita aggiornata; avrà effetto al prossimo avvio',
        'ja': 'デフォルトチェーンを更新しました。次回起動時に適用されます',
        'ko': '기본 체인이 업데이트되었습니다. 다음 시작에 적용됩니다',
        'pt': 'Cadeia predefinida atualizada; aplica-se no próximo arranque',
        'ru': 'Цепочка по умолчанию обновлена; применится при следующем запуске'},
    # ---- preview window 补充 ----
    'win.preview.pin': {
        'zh-Hans': '贴图', 'en': 'Pin', 'de': 'Anheften', 'es': 'Fijar', 'fr': 'Épingler',
        'it': 'Aggiungi', 'ja': 'ピン留め', 'ko': '고정', 'pt': 'Afixar', 'ru': 'Закрепить'},
    'win.preview.ok': {
        'zh-Hans': '确定', 'en': 'OK', 'de': 'OK', 'es': 'Aceptar', 'fr': 'OK',
        'it': 'OK', 'ja': 'OK', 'ko': '확인', 'pt': 'OK', 'ru': 'ОК'},
    'win.preview.saved': {
        'zh-Hans': '已保存: %@', 'en': 'Saved: %@', 'de': 'Gespeichert: %@',
        'es': 'Guardado: %@', 'fr': 'Enregistré : %@', 'it': 'Salvato: %@',
        'ja': '保存しました: %@', 'ko': '저장됨: %@', 'pt': 'Guardado: %@',
        'ru': 'Сохранено: %@'},
    'win.preview.copied': {
        'zh-Hans': '已复制到剪贴板', 'en': 'Copied to clipboard',
        'de': 'In die Zwischenablage kopiert', 'es': 'Copiado al portapapeles',
        'fr': 'Copié dans le presse-papiers', 'it': 'Copiato negli appunti',
        'ja': 'クリップボードにコピーしました', 'ko': '클립보드에 복사됨',
        'pt': 'Copiado para a área de transferência', 'ru': 'Скопировано в буфер обмена'},
    'win.preview.copyFailed': {
        'zh-Hans': '复制到剪贴板失败', 'en': 'Copy to clipboard failed',
        'de': 'Kopieren in die Zwischenablage fehlgeschlagen',
        'es': 'Error al copiar al portapapeles',
        'fr': 'Échec de la copie dans le presse-papiers',
        'it': 'Copia negli appunti non riuscita',
        'ja': 'クリップボードへのコピーに失敗しました', 'ko': '클립보드 복사 실패',
        'pt': 'Falha ao copiar para a área de transferência',
        'ru': 'Не удалось скопировать в буфер обмена'},
    'win.preview.saveDialogFailed': {
        'zh-Hans': '保存对话框失败', 'en': 'Save dialog failed',
        'de': 'Speicherdialog fehlgeschlagen', 'es': 'Error en el diálogo de guardado',
        'fr': 'Échec de la boîte de dialogue d’enregistrement',
        'it': 'Finestra di salvataggio non riuscita', 'ja': '保存ダイアログでエラー',
        'ko': '저장 대화 상자 실패', 'pt': 'Falha na caixa de diálogo de gravação',
        'ru': 'Ошибка диалога сохранения'},
}

STRINGS_LINE = re.compile(r'^"((?:[^"\\]|\\.)*)"\s*=\s*"((?:[^"\\]|\\.)*)";')


def unescape(value):
    out = []
    i = 0
    while i < len(value):
        c = value[i]
        if c == '\\' and i + 1 < len(value):
            n = value[i + 1]
            mapping = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', '\\': '\\'}
            if n in mapping:
                out.append(mapping[n])
                i += 2
                continue
        out.append(c)
        i += 1
    return ''.join(out)


def load_mac_table(lang):
    path = MACOS_STRINGS / f'{lang}.lproj' / 'Localizable.strings'
    table = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        m = STRINGS_LINE.match(line.strip())
        if m:
            table[m.group(1)] = unescape(m.group(2))
    return table


def build():
    mac = {lang: load_mac_table(lang) for lang in LANGS}
    missing_in_lang = []
    tables = {}
    for lang in LANGS:
        table = {}
        for key in MAC_KEYS:
            if key in mac[lang]:
                table[key] = mac[lang][key]
            elif key in mac['en']:
                missing_in_lang.append(f'{lang}:{key}')
                table[key] = mac['en'][key]  # keep key sets identical
            else:
                raise SystemExit(f'macOS en table lacks reused key: {key}')
        for key, values in WIN_ONLY.items():
            if lang not in values:
                raise SystemExit(f'WIN_ONLY key {key} lacks {lang}')
            if not values[lang].strip():
                raise SystemExit(f'WIN_ONLY key {key} has empty {lang}')
            table[key] = values[lang]
        tables[lang] = table

    key_sets = {lang: set(t) for lang, t in tables.items()}
    reference = key_sets['en']
    for lang, keys in key_sets.items():
        if keys != reference:
            raise SystemExit(f'{lang}.json key set differs from en.json')
    for warn in missing_in_lang:
        print(f'note: {warn} inherited from en', file=sys.stderr)
    return tables, reference


def write_outputs(tables):
    for out_dir in (OUT_CANONICAL, OUT_ASSETS):
        out_dir.mkdir(parents=True, exist_ok=True)
        for lang, table in tables.items():
            payload = json.dumps(dict(sorted(table.items())), ensure_ascii=False,
                                 indent=1) + '\n'
            (out_dir / f'{lang}.json').write_text(payload, encoding='utf-8')


CHECK_CODE_RE = re.compile(
    r'\b(?:t|tw|Lx|TL)\(\s*"((?:[A-Za-z0-9._-]+))"')


def check_code(paths, keys):
    # Synthetic keys used by unit tests / the smoke tool as parse fixtures
    # (plus doc-comment placeholders).
    synthetic = {'k.a', 'k.b', 'k.missing', 'whatever.key', 'definitely.not.a.key',
                 'key'}
    unknown = []
    used = set()
    for path in paths:
        text = path.read_text(encoding='utf-8', errors='replace')
        for m in CHECK_CODE_RE.finditer(text):
            used.add(m.group(1))
            if m.group(1) not in keys and m.group(1) not in synthetic:
                unknown.append((str(path), m.group(1)))
    if unknown:
        for path, key in unknown:
            print(f'unknown key in {path}: {key}', file=sys.stderr)
        raise SystemExit(f'{len(unknown)} code keys missing from the string tables')
    print(f'check-code: {len(used)} keys verified against the string tables')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-code', nargs='*', default=None, metavar='DIR',
                        help='scan DIRs recursively for t("key") uses and verify')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()

    tables, keys = build()
    if args.check_code is not None:
        roots = [REPO / 'windows' / p for p in args.check_code] or [
            REPO / 'windows' / 'UI', REPO / 'windows' / 'NativeShell', REPO / 'windows' / 'App',
            REPO / 'windows' / 'Core', REPO / 'windows' / 'Tests']
        files = [p for root in roots for p in root.rglob('*.[ch]pp')
                 if 'build' not in p.parts and 'external' not in p.parts
                 and 'vendor' not in p.parts]
        check_code(files, keys)
    if args.dry_run:
        for lang in LANGS:
            print(f'{lang}.json: {len(tables[lang])} keys')
        return
    write_outputs(tables)
    total = sum(len(t) for t in tables.values())
    print(f'wrote {len(LANGS)} files x {len(keys)} keys ({total} strings) to '
          f'{OUT_CANONICAL} and {OUT_ASSETS}')


if __name__ == '__main__':
    main()
