#!/usr/bin/env python3
"""Complexweeper GUI (tkinter) with a step-by-step solver.

Run from the repository root:   python3 gui/app.py

Mouse:    left click = open, right click = cycle flag (none, +1, -1, +i/+j, -i/-j),
          middle click or Shift+left click = chord (open the neighbors of a number).
Keyboard: Right = solver step, Left = undo, Space = auto play, F2 = new game.
"""
import argparse
import base64
import io
import random
import sys
import time
import tkinter as tk
from tkinter import ttk

import engine as eng_mod
from engine import Engine, LEVELS, MODE_COMPLEX, MODE_HYPER
from sprites import Atlas, cell_sprite

MODE_NAMES = {
    MODE_COMPLEX: "Circular complex (i² = −1)",
    MODE_HYPER: "Minkowski (j² = +1)",
}
# Highlight colors for the cell the solver just acted on.
COLOR_FIRST = "#3aa0ff"
COLOR_OPEN = "#00b000"
COLOR_MARK = "#ff4500"
COLOR_GUESS = "#ffcc00"


try:
    from PIL import ImageTk
except ImportError:  # e.g. Debian ships ImageTk in a separate package
    ImageTk = None


def to_photo(img):
    """PIL image -> tk PhotoImage (works with or without PIL.ImageTk)."""
    if ImageTk is not None:
        return ImageTk.PhotoImage(img)
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return tk.PhotoImage(data=base64.b64encode(buf.getvalue()))


class Led:
    """A three-digit LED display built from the atlas digits."""

    def __init__(self, parent, app):
        self.app = app
        self.frame = tk.Frame(parent, bg="black", bd=2, relief="sunken")
        self.labels = [tk.Label(self.frame, bd=0, bg="black") for _ in range(3)]
        for lab in self.labels:
            lab.pack(side="left")
        self.text = None

    def set(self, value):
        text = "-%02d" % min(-value, 99) if value < 0 else "%03d" % min(value, 999)
        if text == self.text:
            return
        self.text = text
        for lab, ch in zip(self.labels, text):
            photo = self.app.photo("led_minus" if ch == "-" else "led_" + ch)
            lab.configure(image=photo)
            lab.image = photo


class App:
    def __init__(self, root):
        self.root = root
        root.title("Complexweeper")
        root.resizable(False, False)
        self.eng = Engine()
        self.atlas = Atlas()
        self.photos = {}
        self.zoom = 2
        self.items = []
        self.shown = []
        self.highlight = None  # (cell, color) of the solver's latest step
        self.auto = False
        self.auto_job = None
        self.t_start = None
        self.elapsed = 0
        self.prev_state = eng_mod.READY

        self.mode_var = tk.StringVar(value=MODE_NAMES[MODE_COMPLEX])
        self.level_var = tk.StringVar(value="Beginner")
        self.seed_var = tk.StringVar()
        self.zoom_var = tk.StringVar(value=str(self.zoom))
        self.delay_var = tk.IntVar(value=400)
        self.status_var = tk.StringVar()
        self._build_widgets()
        self._bind_keys()
        self.new_game()
        self._tick()

    # ------------------------------------------------------------------ widgets
    def _build_widgets(self):
        top = ttk.Frame(self.root, padding=(8, 6))
        top.grid(row=0, column=0, sticky="ew")
        ttk.Label(top, text="Mode").pack(side="left")
        mode_box = ttk.Combobox(top, textvariable=self.mode_var, values=list(MODE_NAMES.values()),
                                state="readonly", width=24)
        mode_box.pack(side="left", padx=(4, 10))
        ttk.Label(top, text="Level").pack(side="left")
        level_box = ttk.Combobox(top, textvariable=self.level_var, values=list(LEVELS), state="readonly", width=13)
        level_box.pack(side="left", padx=(4, 10))
        for box in (mode_box, level_box):
            box.bind("<<ComboboxSelected>>", lambda _e: self.new_game())
        ttk.Label(top, text="Seed").pack(side="left")
        ttk.Entry(top, textvariable=self.seed_var, width=11).pack(side="left", padx=(4, 6))
        ttk.Button(top, text="New game", command=self.new_game).pack(side="left", padx=(0, 10))
        ttk.Label(top, text="Zoom").pack(side="left")
        ttk.Spinbox(top, from_=1, to=3, width=3, textvariable=self.zoom_var, state="readonly",
                    command=self.on_zoom).pack(side="left", padx=4)

        self.header = tk.Frame(self.root, bd=2, relief="groove", padx=6, pady=4)
        self.header.grid(row=1, column=0, sticky="ew", padx=8)
        self.mine_led = Led(self.header, self)
        self.mine_led.frame.pack(side="left")
        self.time_led = Led(self.header, self)
        self.time_led.frame.pack(side="right")
        self.face = tk.Label(self.header, bd=0)
        self.face.pack()
        self.face.bind("<Button-1>", lambda _e: self.new_game())

        self.canvas = tk.Canvas(self.root, highlightthickness=0, bd=0)
        self.canvas.grid(row=2, column=0, padx=8, pady=(4, 4))
        self.canvas.bind("<ButtonRelease-1>", self.on_left)
        self.canvas.bind("<Button-3>", self.on_right)
        self.canvas.bind("<Button-2>", self.on_middle)

        bar = ttk.Frame(self.root, padding=(8, 4))
        bar.grid(row=3, column=0, sticky="ew")
        self.undo_btn = ttk.Button(bar, text="◀ Undo", command=self.undo)
        self.undo_btn.pack(side="left")
        self.step_btn = ttk.Button(bar, text="Step ▶", command=self.step)
        self.step_btn.pack(side="left", padx=6)
        self.auto_btn = ttk.Button(bar, text="Auto play", command=self.toggle_auto)
        self.auto_btn.pack(side="left")
        ttk.Label(bar, text="Delay (ms)").pack(side="left", padx=(14, 4))
        ttk.Scale(bar, from_=30, to=1500, variable=self.delay_var, orient="horizontal", length=140,
                  command=lambda v: self.delay_var.set(int(float(v)))).pack(side="left")
        self.delay_label = ttk.Label(bar, textvariable=self.delay_var, width=5)
        self.delay_label.pack(side="left")

        self.status = ttk.Label(self.root, textvariable=self.status_var, justify="left", padding=(10, 2, 10, 8))
        self.status.grid(row=4, column=0, sticky="w")

    def _bind_keys(self):
        def guard(fn):
            def handler(_event):
                if isinstance(self.root.focus_get(), (ttk.Entry, ttk.Spinbox, ttk.Combobox)):
                    return None
                fn()
                return "break"
            return handler
        self.root.bind("<Right>", guard(self.step))
        self.root.bind("<Left>", guard(self.undo))
        self.root.bind("<space>", guard(self.toggle_auto))
        self.root.bind("<F2>", lambda _e: self.new_game())

    # ------------------------------------------------------------------ sprites
    def photo(self, name):
        key = (name, self.zoom)
        if key not in self.photos:
            self.photos[key] = to_photo(self.atlas.scaled(name, self.zoom))
        return self.photos[key]

    # ------------------------------------------------------------------ game flow
    def now_ms(self):
        return int(time.monotonic() * 1000)

    def new_game(self):
        self.stop_auto()
        mode = MODE_HYPER if self.mode_var.get() == MODE_NAMES[MODE_HYPER] else MODE_COMPLEX
        w, h, mines = LEVELS[self.level_var.get()]
        try:
            seed = int(self.seed_var.get())
        except ValueError:
            seed = random.randrange(1, 2 ** 31)
        self.seed_var.set(str(seed))
        self.eng.new_game(w, h, mines, mode, seed)
        self.t_start = None
        self.elapsed = 0
        self.prev_state = eng_mod.READY
        self.highlight = None
        self.build_board()
        self.say("New game. Click a cell to start, or press Step and let the solver play.")
        self.refresh()

    def build_board(self):
        w, h = self.eng.width, self.eng.height
        cs = 16 * self.zoom
        self.canvas.delete("all")
        self.canvas.configure(width=w * cs, height=h * cs)
        closed = self.photo("closed")
        self.items = [self.canvas.create_image((i % w) * cs, (i // w) * cs, anchor="nw", image=closed)
                      for i in range(w * h)]
        self.shown = ["closed"] * (w * h)

    def on_zoom(self):
        self.zoom = int(self.zoom_var.get())
        self.photos.clear()
        self.build_board()
        self.mine_led.text = self.time_led.text = None
        self.refresh()

    def cell_at(self, event):
        cs = 16 * self.zoom
        col, row = event.x // cs, event.y // cs
        if 0 <= col < self.eng.width and 0 <= row < self.eng.height:
            return row * self.eng.width + col
        return None

    def over(self):
        return self.eng.state in (eng_mod.WON, eng_mod.LOST)

    def on_left(self, event):
        cell = self.cell_at(event)
        if cell is None or self.over():
            return
        if event.state & 0x1:  # Shift held: chord
            self.manual(self.eng.chord(cell))
        else:
            self.manual(self.eng.click(cell, self.now_ms()))

    def on_right(self, event):
        cell = self.cell_at(event)
        if cell is not None:
            self.manual(self.eng.cycle_flag(cell))

    def on_middle(self, event):
        cell = self.cell_at(event)
        if cell is not None:
            self.manual(self.eng.chord(cell))

    def manual(self, changed):
        if changed:
            self.stop_auto()
            self.highlight = None
            self.say("")
            self.refresh()

    # ------------------------------------------------------------------ solver
    def describe(self, mv):
        w = self.eng.width
        where = "row %d, column %d" % (mv.cell // w + 1, mv.cell % w + 1)
        if mv.reason == eng_mod.REASON_FIRST_CLICK:
            return "First click at %s, the middle of the board. The opening cell and its neighbors are always safe." % where
        if mv.reason == eng_mod.REASON_GUESS:
            return ("No certain move is left, so guess: open %s. Estimated mine probability: %.0f%%."
                    % (where, mv.risk * 100))
        if mv.kind == eng_mod.KIND_OPEN:
            return "Open %s: logically certain to be safe." % where
        return ("Mark %s as a mine: logically certain. (The numbers never reveal which of the four "
                "types it is, so this flag has no type.)" % where)

    def step(self):
        if self.over():
            return False
        mv = self.eng.solver_step(self.now_ms())
        if mv is None:
            self.stop_auto()
            self.say("The solver has no further move.")
            return False
        if mv.reason == eng_mod.REASON_FIRST_CLICK:
            color = COLOR_FIRST
        elif mv.reason == eng_mod.REASON_GUESS:
            color = COLOR_GUESS
        else:
            color = COLOR_OPEN if mv.kind == eng_mod.KIND_OPEN else COLOR_MARK
        self.highlight = (mv.cell, color)
        self.say("Step %d: %s" % (self.eng.undo_depth, self.describe(mv)))
        self.refresh()
        return True

    def undo(self):
        if self.eng.undo():
            self.stop_auto()
            self.highlight = None
            self.say("Undid one step (%d left to undo)." % self.eng.undo_depth)
            self.refresh()

    def toggle_auto(self):
        if self.auto:
            self.stop_auto()
        elif not self.over():
            self.auto = True
            self.auto_btn.configure(text="Pause")
            self.refresh()
            self._auto_tick()

    def stop_auto(self):
        self.auto = False
        if self.auto_job is not None:
            self.root.after_cancel(self.auto_job)
            self.auto_job = None
        self.auto_btn.configure(text="Auto play")

    def _auto_tick(self):
        self.auto_job = None
        if not self.auto:
            return
        if not self.step() or self.over():
            self.stop_auto()
            self.refresh()
            return
        self.auto_job = self.root.after(self.delay_var.get(), self._auto_tick)

    # ------------------------------------------------------------------ display
    def say(self, text):
        self.status_var.set(text)

    def refresh(self):
        e = self.eng
        state = e.state
        over = state in (eng_mod.WON, eng_mod.LOST)
        hyper = e.mode == MODE_HYPER
        boom = e.boom_cell
        for i, c in enumerate(e.cells()):
            name = cell_sprite(c, i, hyper, over, boom, self.atlas)
            if name != self.shown[i]:
                self.canvas.itemconfigure(self.items[i], image=self.photo(name))
                self.shown[i] = name

        self.canvas.delete("highlight")
        if self.highlight is not None:
            cell, color = self.highlight
            cs = 16 * self.zoom
            x, y = (cell % e.width) * cs, (cell // e.width) * cs
            self.canvas.create_rectangle(x + 1, y + 1, x + cs - 2, y + cs - 2, outline=color, width=3,
                                         tags="highlight")

        # Timer: starts with the first click, freezes at the end, resets with the game.
        if state == eng_mod.READY:
            self.t_start, self.elapsed = None, 0
        elif state == eng_mod.PLAYING and self.t_start is None:
            self.t_start = time.monotonic() - self.elapsed
        self.mine_led.set(0 if state == eng_mod.WON else e.mines - e.marked)
        self.time_led.set(self.elapsed)

        if state == eng_mod.LOST:
            face = "face_dead"
        elif state == eng_mod.WON:
            face = "face_win"
        else:
            face = "face_scan" if self.auto else "face_normal"
        self.face.configure(image=self.photo(face))
        self.face.image = self.photo(face)

        if state != self.prev_state:
            if state == eng_mod.WON:
                self.say("All safe cells are open. You win!")
            elif state == eng_mod.LOST:
                self.say(self.status_var.get() + "  Boom, the game is lost." if self.highlight else "Boom, the game is lost.")
            self.prev_state = state

        self.undo_btn.state(["!disabled"] if e.undo_depth > 0 else ["disabled"])
        self.step_btn.state(["disabled"] if over else ["!disabled"])
        self.auto_btn.state(["disabled"] if over and not self.auto else ["!disabled"])

    def _tick(self):
        if self.eng.state == eng_mod.PLAYING and self.t_start is not None:
            self.elapsed = int(time.monotonic() - self.t_start)
            self.time_led.set(self.elapsed)
        self.root.after(200, self._tick)


# ---------------------------------------------------------------------- smoke test
def smoke_test(app):
    """Drive the window the way a user would, check the results, then quit."""
    results = []

    def check(cond, text):
        results.append((bool(cond), text))

    def run():
        root, e = app.root, app.eng
        root.update()
        # Manual play
        app.mode_var.set(MODE_NAMES[MODE_COMPLEX])
        app.level_var.set("Beginner")
        app.seed_var.set("12345")
        app.new_game()
        check(e.state == eng_mod.READY and e.undo_depth == 0, "new game is ready, no history")
        app.manual(e.click(40, app.now_ms()))
        check(e.state == eng_mod.PLAYING and e.undo_depth == 1, "first click starts the game")
        app.undo()
        check(e.state == eng_mod.READY and e.undo_depth == 0, "undo returns to the ready state")
        # The same actions through real mouse events
        app.new_game()
        cs = 16 * app.zoom
        cx, cy = 4 * cs + cs // 2, 4 * cs + cs // 2  # the middle cell of a 9x9 board
        app.canvas.event_generate("<ButtonRelease-1>", x=cx, y=cy)
        root.update()
        check(e.state == eng_mod.PLAYING, "left click event starts the game")
        closed = next(i for i, c in enumerate(e.cells()) if not c.open)
        fx, fy = (closed % e.width) * cs + cs // 2, (closed // e.width) * cs + cs // 2
        app.canvas.event_generate("<Button-3>", x=fx, y=fy)
        root.update()
        check(e.cells()[closed].flag == 1 and app.shown[closed] == "flag_1", "right click event places a flag and shows it")
        app.canvas.event_generate("<Button-2>", x=cx, y=cy)
        app.canvas.event_generate("<ButtonRelease-1>", x=cx, y=cy, state=0x1)
        root.update()
        check(e.state in (eng_mod.PLAYING, eng_mod.LOST), "middle click and shift+click chord events are handled")
        # Solver stepping with undo
        app.new_game()
        for _ in range(8):
            app.step()
        depth = e.undo_depth
        check(depth == 8, "eight solver steps leave eight history entries")
        for _ in range(3):
            app.undo()
        check(e.undo_depth == depth - 3, "three undos remove three entries")
        # Auto play to the end, on both modes
        app.delay_var.set(1)
        app.toggle_auto()
        for _ in range(4000):
            root.update()
            if not app.auto:
                break
            time.sleep(0.002)
        check(e.state in (eng_mod.WON, eng_mod.LOST), "auto play runs the game to its end")
        app.mode_var.set(MODE_NAMES[MODE_HYPER])
        app.level_var.set("Intermediate")
        app.new_game()
        check(e.mode == MODE_HYPER and (e.width, e.height) == (16, 16), "mode and level selection start the right game")
        app.zoom_var.set("1")
        app.on_zoom()
        app.zoom_var.set("3")
        app.on_zoom()
        app.step()
        app.step()
        root.update()
        check(app.zoom == 3 and e.undo_depth == 2, "zoom change keeps the game")
        root.destroy()

    app.root.after(100, run)
    app.root.mainloop()
    failed = [t for ok, t in results if not ok]
    for ok, text in results:
        print("%s  %s" % ("ok  " if ok else "FAIL", text))
    print("%d checks, %d failed" % (len(results), len(failed)))
    return 1 if failed or not results else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--smoke", action="store_true", help="drive the window automatically, check, and exit")
    args = parser.parse_args()
    root = tk.Tk()
    app = App(root)
    if args.smoke:
        return smoke_test(app)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
