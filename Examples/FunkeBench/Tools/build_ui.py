"""Regenerates the authored Bench scene. Runtime workloads are owned by BenchmarkSession."""
from funke_kit import *

PALETTE = {
    "canvas": "#1C1D20", "panel": "#1E1F22", "chrome": "#161719",
    "control": "#2A2C30", "hover": "#36383E", "pressed": "#41444B",
    "text": "#EBECEF", "muted": "#9A9DA6", "accent": "#5B8DEF",
    "highlight": "#9DBCF7", "success": "#7BC96F", "warning": "#F8BF55",
    "border": "#34363C", "selection": "#28364E"
}
write_json(PROJECT / "Design/palette.json", PALETTE)
s = Scene("Bench")
root = s.add("Bench UI", rect(size=(1440, 900)))
controller = s.add("Benchmark Controller")

def box(name, x, y, w, h, role="panel", parent=root, order=0):
    return s.add(name, rect((x, -y), (w, h), anchor=(0, 1)), image(color=PALETTE[role], order=order), parent=parent)

def label(name, value, x, y, w, h=26, size=16, role="text", align=0, parent=root):
    return s.add(name, rect((x, -y), (w, h), anchor=(0, 1)),
                 text(value, size, PALETTE[role], align=align, valign=1, order=6), parent=parent)

def action(name, title, x, y, w, h, method, arg=None, primary=False):
    role = "accent" if primary else "control"
    uid = s.add(name, rect((x, -y), (w, h), anchor=(0, 1)), image(color=PALETTE[role], order=2),
                button(click(controller, "BenchController", method, arg), normal=PALETTE[role],
                       hovered=PALETTE["hover"] if not primary else "#6999F7", pressed=PALETTE["pressed"]), parent=root)
    s.by_uuid[uid]["Button"]["focused"] = rgba(PALETTE["selection"])
    label(name + " Label", title, 0, 0, w, h, 16, align=1, parent=uid)
    return uid

def number(name, value, x, y, w, h=38):
    uid = box(name, x, y, w, h, "control", order=2)
    txt = label(name + " Text", value, 12, 0, w - 24, h, 18, parent=uid)
    s.by_uuid[uid].update(Interactable={"interactable": True, "focusable": True},
        InputField={"text": value, "textEntity": txt, "characterLimit": 7, "contentType": 3,
                    "normal": rgba(PALETTE["control"]), "hovered": rgba(PALETTE["hover"]),
                    "focused": rgba(PALETTE["selection"]), "textColor": rgba(PALETTE["text"]),
                    "caretColor": rgba(PALETTE["highlight"])})
    return uid

def toggle(name, title, x, y, value):
    uid = box(name, x, y, 24, 24, "control", order=2)
    mark = box(name + " Mark", 5, 5, 14, 14, "accent", parent=uid, order=3)
    s.by_uuid[mark]["disabled"] = not value
    s.by_uuid[uid].update(Interactable={"interactable": True, "focusable": True},
        Toggle={"isOn": value, "checkmarkEntity": mark, "normal": rgba(PALETTE["control"]),
                "hovered": rgba(PALETTE["hover"]), "focused": rgba(PALETTE["selection"])})
    label(name + " Label", title, x + 36, y - 1, 236, 26, 16)
    return uid

box("Top bar", 0, 0, 1440, 64, "chrome")
label("App title", "FunkeBench", 24, 14, 240, 36, 24)
label("App subtitle", "Runtime performance workbench", 254, 20, 480, 26, 16, "muted")
action("Clear workload", "Clear workload", 1080, 14, 154, 36, "Clear")
action("Export results", "Export CSV", 1250, 14, 166, 36, "Export")
box("Left panel", 0, 65, 320, 803)
box("Left separator", 319, 65, 1, 803, "border")
box("Right panel", 1120, 65, 320, 565)
box("Right separator", 1120, 65, 1, 565, "border")
box("Preview toolbar", 320, 65, 800, 53, "chrome")
box("Preview bottom", 320, 584, 800, 46, "chrome")
box("Results panel", 320, 630, 1120, 238)
box("Results separator", 320, 630, 1120, 1, "border")
box("Footer", 0, 869, 1440, 31, "chrome")

label("Scenario header", "Benchmark", 24, 85, 272, 32, 20)
markers = []
for i, title in enumerate(["Create entities", "Destroy entities", "Move entities", "Render 2D sprites", "Render 3D cubes"]):
    y = 130 + i * 43
    action("Scenario " + str(i), title, 24, y, 272, 36, "SelectScenario", i)
    mark = box("Scenario marker " + str(i), 24, y, 3, 36, "accent", order=7)
    s.by_uuid[mark]["disabled"] = i != 0
    markers.append(mark)
label("Count heading", "Entity count", 24, 362, 272, 26, 16, "muted")
count = number("Entity count", "10000", 24, 394, 272, 44)
for title, value, x, w in [("1k",1000,24,82),("10k",10000,116,82),("100k",100000,208,88)]:
    action("Preset " + title, title, x, 449, w, 32, "SetCount", value)
label("Count limit", "1 - 100,000 entities per workload", 24, 489, 272, 22, 13, "muted")
label("Samples heading", "Operation samples", 24, 528, 170, 26, 16, "muted")
iterations = number("Operation samples", "10", 208, 522, 88)
label("Duration heading", "Render seconds", 24, 574, 170, 26, 16, "muted")
seconds = number("Render seconds", "5", 208, 568, 88)
box("Options separator", 24, 629, 272, 1, "border")
jobs = toggle("Jobs", "Workload Jobs / threads", 24, 648, True)
animate = toggle("Animate", "Animate render workload", 24, 690, False)
vsync = toggle("VSync", "VSync", 24, 732, False)
label("Jobs scope", "Jobs affect workload compute/recording.\nEngine worker pools remain enabled.", 24, 764, 274, 40, 13, "muted")
run = action("Run benchmark", "Run benchmark", 24, 819, 174, 36, "Run", primary=True)
once = action("Run once", "Once", 208, 819, 88, 36, "RunOnce")

view_name = label("Preview title", "Create entities", 344, 78, 560, 28, 18)
action("Stop benchmark", "Stop", 1012, 76, 88, 31, "Stop")
preview_note = label("Preview note", "Entity operations have no visual geometry", 344, 594, 746, 23, 14, "muted")
empty = s.add("Preview empty state", rect((0, 0), (1440, 900)), parent=root)
label("Empty title", "Ready to benchmark", 380, 291, 680, 42, 27, align=1, parent=empty)
label("Empty description", "Choose an operation, set the entity count, and run.\nRendering workloads appear here while the app measures frames.",
      380, 342, 680, 64, 16, "muted", align=1, parent=empty)
label("Empty detail", "Repeated runs include warmup. Once measures a single operation.", 380, 427, 680, 28, 14, "muted", align=1, parent=empty)

label("Metrics heading", "Measurement", 1144, 85, 272, 30, 20)
label("Live entities label", "Live workload entities", 1144, 132, 272, 24, 14, "muted")
live = label("Live entities value", "0", 1144, 160, 272, 40, 29)
label("Frame time label", "Application frame time", 1144, 215, 272, 24, 14, "muted")
frame = label("Frame time value", "-- ms", 1144, 243, 272, 40, 29, "highlight")
fps = label("FPS value", "-- FPS", 1144, 284, 272, 24, 15, "muted")
box("Metrics separator", 1144, 325, 272, 1, "border")
label("Average label", "Last mean", 1144, 345, 135, 24, 15, "muted")
mean = label("Average value", "--", 1266, 345, 150, 24, 17, align=2)
label("Median label", "Median", 1144, 383, 135, 24, 15, "muted")
median = label("Median value", "--", 1266, 383, 150, 24, 17, align=2)
label("P95 label", "P95", 1144, 421, 135, 24, 15, "muted")
p95 = label("P95 value", "--", 1266, 421, 150, 24, 17, align=2)
label("Setup label", "Setup", 1144, 459, 135, 24, 15, "muted")
setup = label("Setup value", "--", 1266, 459, 150, 24, 17, align=2)
scope = label("Timing scope", "Create / destroy / move: operation time\nRender: full app frames, UI included", 1144, 510, 272, 60, 13, "muted")
s.by_uuid[scope]["TextRenderer"]["wrapMode"] = 1
progress = label("Progress", "Ready", 1144, 585, 272, 26, 14, "highlight")

label("Results heading", "Recent results", 344, 646, 400, 28, 18)
label("Results note", "Newest first  /  latest 64 results in CSV", 750, 648, 666, 26, 14, "muted", align=2)
columns = [("Names", "Benchmark", 344, 250), ("Counts", "Entities", 600, 150), ("Modes", "Mode", 756, 164),
           ("Samples", "Samples", 926, 100), ("Means", "Mean (ms)", 1032, 160), ("P95s", "P95 (ms)", 1200, 216)]
history = {}
for key, title, x, w in columns:
    label("History " + key + " heading", title, x, 688, w, 24, 14, "muted")
    rows = []
    for row in range(4):
        rows.append(label("History " + key + " row " + str(row), "--", x, 724 + row * 31, w, 27, 15))
    history["History" + key] = f_component_list([(uid, "TextRenderer") for uid in rows])
status = label("Status", "Ready. Select a benchmark to begin.", 24, 871, 1392, 27, 13, "muted")

cam2 = s.add("Benchmark Camera 2D", T2D(), Camera2D={"orthoSize": 10, "zoom": 1,
    "clearR": 0.11, "clearG": 0.114, "clearB": 0.125, "clearA": 1, "postProcessing": False})
cam3 = s.add("Benchmark Camera 3D", T3D(z=20), disabled=True, Camera3D={"projection":1,"orthoSize":10,
    "near":0.1,"far":100,"background":1,"clearColor":rgba(PALETTE["canvas"])})
s.add("Benchmark Light", T3D(rx=-35,ry=-35), DirectionalLight={"intensity":1.1,"ambient":rgba("#808894"),"castShadows":False})
s.script(controller, "BenchController", CountInput=f_comp(count,"InputField"), IterationsInput=f_comp(iterations,"InputField"),
         DurationInput=f_comp(seconds,"InputField"), JobsToggle=f_comp(jobs,"Toggle"), AnimateToggle=f_comp(animate,"Toggle"),
         VsyncToggle=f_comp(vsync,"Toggle"), Camera2DEntity=f_entity(cam2), Camera3DEntity=f_entity(cam3),
         EmptyState=f_entity(empty), ScenarioMarkers=f_entity_list(markers), ScenarioTitle=f_comp(view_name,"TextRenderer"),
         PreviewNote=f_comp(preview_note,"TextRenderer"), StatusLabel=f_comp(status,"TextRenderer"))
s.script(controller, "BenchMetrics", LiveLabel=f_comp(live,"TextRenderer"), FrameLabel=f_comp(frame,"TextRenderer"),
         FpsLabel=f_comp(fps,"TextRenderer"), MeanLabel=f_comp(mean,"TextRenderer"), MedianLabel=f_comp(median,"TextRenderer"),
         P95Label=f_comp(p95,"TextRenderer"), SetupLabel=f_comp(setup,"TextRenderer"), ScopeLabel=f_comp(scope,"TextRenderer"),
         ProgressLabel=f_comp(progress,"TextRenderer"), **history)
s.save()

print(f"Generated Bench.scene with {len(s.entities)} authored entities")
