"""Example app generator: a two-tab notes app built from ui_gen.py. Copy both into <project>/Tools/ together with
templates/Scripts/** into <project>/Assets/Scripts/, then: python Tools/gen_example.py

Shows the pattern every app generator follows: theme + fonts, camera, canvas with Router, tab pages, pooled lists,
tab bar, overlays (sheet, dialog, toast), project settings."""
import ui_gen as ui
from ui_gen import Scene, top, rect, image, text, button, icon_button, pill, SQUARE


def note_row(s, row, slot):
    s.add(f"{row}/Title", RectTransform2D=rect(48, 22, 860, 48, 0, 0.5), TextRenderer=text("", 32, 12))
    s.add(f"{row}/Subtitle", RectTransform2D=rect(48, -24, 860, 40, 0, 0.5), TextRenderer=text("", 26, 12, color=ui.MUTED))
    icon_button(s, f"{row}/More", ui.ic("More"), -70, 0, 100, 44, "Canvas/Pages/Notes/List", "NoteList", "OnMoreClicked", 13,
                ax=1, tint=ui.MUTED, arg=slot)


def notes_page(s):
    p = ui.page(s, "Notes", scripts={"NotesPage": []})
    s.add(f"{p}/Title", RectTransform2D=top(48, -48, 800, 80), TextRenderer=text("Notizen", 54, 20, bold=True))
    icon_button(s, f"{p}/Add", ui.ic("Plus"), -86, -88, 104, 56, p, "NotesPage", "OnAdd", 20, ax=1, ay=1)
    ui.virtual_list(s, f"{p}/List", "NoteList", top_offset=180, bottom_offset=172, row_height=140, pool=22,
                    build_row=lambda row, slot: note_row(s, row, slot))
    e = f"{p}/Empty"
    s.add(e, RectTransform2D=top(0, -560, 900, 200, 0.5))
    s.add(f"{e}/Title", RectTransform2D=top(0, 0, 900, 60, 0.5), TextRenderer=text("Noch keine Notizen", 40, 20, bold=True, align=1))
    s.add(f"{e}/Hint", RectTransform2D=top(0, -70, 900, 50, 0.5), TextRenderer=text("Tippe auf + um eine zu schreiben.", 28, 20, align=1, color=ui.MUTED))


def settings_page(s):
    p = ui.page(s, "Settings", scripts={"SettingsPage": []})
    s.add(f"{p}/Title", RectTransform2D=top(48, -48, 800, 80), TextRenderer=text("Einstellungen", 54, 20, bold=True))
    r = f"{p}/Reminder"
    s.add(r, RectTransform2D=top(0, -200, 1080, 130, 0.5), Image=image(SQUARE, 20, ui.CLEAR), **button(s, p, "SettingsPage", "OnToggleReminder"))
    s.add(f"{r}/Label", RectTransform2D=rect(48, 22, 800, 48, 0, 0.5), TextRenderer=text("Erinnerungen", 32, 21))
    s.add(f"{r}/Hint", RectTransform2D=rect(48, -24, 800, 40, 0, 0.5), TextRenderer=text("Täglich an offene Notizen erinnern.", 24, 21, color=ui.MUTED))
    s.add(f"{r}/Switch", RectTransform2D=rect(-108, 0, 104, 60, 1, 0.5, 0.5, 0.5), Image=image(ui.rr(104, 60, 30), 21, ui.ACCENT))
    s.add(f"{r}/Switch/Knob", RectTransform2D=rect(22, 0, 48, 48), Image=image(ui.circle_tex(), 22, ui.WHITE))
    pill(s, f"{p}/Clear", "Alle Notizen löschen", 48, -380, 480, 92, ui.SURFACE, ui.WHITE, p, "SettingsPage", "OnClearAll", 20, size=28)


def main():
    # Fonts: drop OFL fonts into Assets/Fonts (e.g. Inter-Regular / Inter-Bold); "0" falls back to the engine font.
    s = Scene("Main")
    ui.camera(s)
    ui.root_canvas(s, scripts={"Router": [("Tabs", "string", "Notes,Settings")]})
    notes_page(s)
    settings_page(s)
    ui.tab_bar(s, [("Notes", ui.ic("Home"), "Notizen"), ("Settings", ui.ic("Gear"), "Einstellungen")])
    ui.toast(s, y=200)
    ui.action_sheet(s)
    ui.input_dialog(s)
    print("Main", s.save("Scenes/Main.scene"))
    ui.mobile_project_settings("Main")


if __name__ == "__main__":
    main()
