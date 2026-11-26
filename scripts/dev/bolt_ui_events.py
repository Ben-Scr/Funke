"""Project UI events onto per-widget storage shared by all Bolt component handles."""
EVENTS = {
    "Interactable": [("Hovered", "Interactable"), ("Pressed", "Interactable"), ("Clicked", "Interactable"),
                     ("MouseDown", "Interactable"), ("MouseUp", "Interactable"),
                     ("FocusEnter", "Interactable"), ("FocusExit", "Interactable")],
    "Button": [(n, "Button") for n in ("ClickDown", "Clicked", "ClickUp", "Pressed")],
    "Slider": [("ValueChanged", "float")], "Toggle": [("ValueChanged", "bool")],
    "Dropdown": [("SelectedIndexChange", "int")],
    "InputField": [("ValueChanged", "string"), ("Select", "InputField"), ("Deselect", "InputField")],
    "Scrollbar": [("ValueChanged", "float")], "CircularSlider": [("ValueChanged", "float")],
    "ScrollRect": [("ValueChanged", "Vector2")],
}

POLL = {
    "Interactable": """bool hovered = widget.IsHovered;
        bool focused = widget.IsFocused;
        if (hovered && !wasHovered) Hovered.Invoke(widget);
        if (widget.IsPressed) Pressed.Invoke(widget);
        if (focused && !wasFocused) FocusEnter.Invoke(widget);
        if (!focused && wasFocused) FocusExit.Invoke(widget);
        if (widget.IsMouseDown) MouseDown.Invoke(widget);
        if (widget.IsClicked) Clicked.Invoke(widget);
        if (widget.IsMouseUp) MouseUp.Invoke(widget);
        wasHovered = hovered;
        wasFocused = focused;""",
    "Button": """Funke.Entity owner = FunkeApiTypes.MakeEntity(widget.ID);
        Interactable? hit = owner.GetComponent<Interactable>();
        if (hit == null) {
            Funke.Entity? target = widget.TargetGraphic;
            if (target != null) hit = target.Value.GetComponent<Interactable>();
        }
        if (hit == null) return;
        if (hit.Value.IsMouseDown) ClickDown.Invoke(widget);
        if (hit.Value.IsClicked) Clicked.Invoke(widget);
        if (hit.Value.IsMouseUp) ClickUp.Invoke(widget);
        if (hit.Value.IsPressed) Pressed.Invoke(widget);""",
    "InputField": """string text = widget.Text;
        if (hasText && text != lastText) ValueChanged.Invoke(text);
        hasText = true;
        lastText = text;
        bool focused = widget.IsFocused;
        if (focused && !wasFocused) Select.Invoke(widget);
        if (!focused && wasFocused) Deselect.Invoke(widget);
        wasFocused = focused;""",
}


def extend(classes, slots, directory):
    header = "import System\nimport System.Memory\nimport Funke\nnamespace Funke.UI;\n\n"
    signals = []
    for widget, events in EVENTS.items():
        name = widget + "Events"
        store = f"FkUiStore<{widget}, {name}>"
        classes[widget].append(f"    {name}& Events {{ get {{ return {store}.For(ID).Get(); }} }}")
        declarations = []
        for event, value in events:
            declarations.append(f"    event Action<{value}> {event};")
            classes[widget].append(f"""    WidgetEvent<{value}> {event} {{
        get {{
            Shared<{name}> state = {store}.For(ID);
            WidgetEvent<{value}> signal;
            signal.FkAdd = (Action<{value}> h) => {{ state.Get().{event} += h; }};
            signal.FkRemove = (Action<{value}> h) => {{ state.Get().{event} -= h; }};
            return signal;
        }}
        set {{}}
    }}""")
        if widget in ("Slider", "Toggle"):
            value = events[0][1]
            classes[widget].append(f"    WidgetEvent<{value}> OnValueChanged {{ get {{ return ValueChanged; }} set {{}} }}")
        previous = {"Interactable": "    private bool wasHovered, wasFocused;\n",
                    "InputField": "    private bool hasText, wasFocused;\n    private string lastText;\n"}.get(widget, "")
        if any(event == "ValueChanged" for event, _ in events):
            value = next(t for event, t in events if event == "ValueChanged")
            declarations.append(f"    void FkChanged({value} value) {{ ValueChanged.Invoke(value); }}")
        if widget == "InputField":
            declarations.append("    void FkObserveText(string value) { hasText = true; lastText = value; }")
        if widget in POLL:
            poll = POLL[widget]
        else:
            event = events[0][0]
            field = {"Toggle": "IsOn", "Dropdown": "SelectedIndex", "ScrollRect": "NormalizedPosition"}.get(widget, "Value")
            slot = widget + ("_GetSelectionChangedThisFrame" if widget == "Dropdown" else "_GetValueChangedThisFrame")
            slots.add(slot)
            poll = f"if (Fk{slot}(widget.ID, 0ul) != 0ul) {event}.Invoke(widget.{field});"
        signals.append(f"public class {name} {{\npublic:\n" + "\n".join(declarations) + "\n" + previous +
                       f"    void FkPoll({widget} widget) {{\n        {poll}\n    }}\n}}")
    calls = [f"        FkUiStore<{w}, {w}Events>.Tick();" for w in EVENTS]
    signals.append("public static class FkUiDispatch {\npublic:\n    static void Tick() {\n" +
                   "\n".join(calls) + "\n    }\n}")
    (directory / "UI/WidgetEvents.bolt").write_text(header + "\n\n".join(signals) + "\n", encoding="utf-8")

