"""Small test GUI for the Python value API."""

import tkinter as tk
from tkinter import ttk
from threading import Thread

from darkeden import DarkEdenError, ValueType, list_nearby_characters, list_processes, read_value, select_nearby_character
from driver_service import service_status, start_driver


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("DarkEden 값 읽기")
        self.geometry("690x500")
        self.resizable(False, False)
        self.pid = tk.StringVar()
        self.values = {
            ValueType.X: tk.StringVar(value="—"),
            ValueType.Y: tk.StringVar(value="—"),
            ValueType.CHARACTER_NAME: tk.StringVar(value="—"),
        }
        self.status = tk.StringVar(value="대상 프로세스를 선택하세요.")
        self.driver_status = tk.StringVar(value=service_status())
        frame = ttk.Frame(self, padding=18)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="대상 프로세스").grid(row=0, column=0, sticky="w")
        self.process_box = ttk.Combobox(frame, textvariable=self.pid, state="readonly", width=22)
        self.process_box.grid(row=0, column=1, sticky="ew", padx=12)
        ttk.Button(frame, text="새로고침", command=self.refresh_processes).grid(row=0, column=2)
        self.driver_button = ttk.Button(frame, text="드라이버 실행", command=self.start_driver)
        self.driver_button.grid(row=1, column=0, columnspan=2, sticky="w", pady=(14, 4))
        ttk.Label(frame, textvariable=self.driver_status).grid(row=1, column=2, sticky="w")
        for row, (kind, label) in enumerate(((ValueType.X, "X좌표"),
                                              (ValueType.Y, "Y좌표"),
                                              (ValueType.CHARACTER_NAME, "내 캐릭터명")), start=2):
            ttk.Label(frame, text=label).grid(row=row, column=0, sticky="w", pady=12)
            ttk.Label(frame, textvariable=self.values[kind], width=28).grid(
                row=row, column=1, columnspan=2, sticky="w", padx=12)
        ttk.Button(frame, text="모든 값 읽기", command=self.read_all).grid(
            row=5, column=2, sticky="ew", padx=(8, 0), pady=(8, 12))
        ttk.Label(frame, text="주변 캐릭터").grid(row=6, column=0, sticky="w")
        self.nearby_button = ttk.Button(frame, text="목록 새로고침", command=self.refresh_nearby)
        self.nearby_button.grid(row=6, column=2, sticky="e")
        self.nearby_tree = ttk.Treeview(frame, columns=("name", "race", "life", "x", "y", "id"),
                                        show="headings", height=8)
        for column, label, width in (("name", "이름", 170), ("race", "종족", 95),
                                     ("life", "상태", 75),
                                     ("x", "X", 65), ("y", "Y", 65), ("id", "ID", 95)):
            self.nearby_tree.heading(column, text=label)
            self.nearby_tree.column(column, width=width, anchor="center" if column != "name" else "w")
        self.nearby_tree.grid(row=7, column=0, columnspan=3, sticky="nsew", pady=(6, 10))
        nearby_scroll = ttk.Scrollbar(frame, orient="vertical", command=self.nearby_tree.yview)
        nearby_scroll.grid(row=7, column=3, sticky="ns", pady=(6, 10))
        self.nearby_tree.configure(yscrollcommand=nearby_scroll.set)
        self.select_button = ttk.Button(frame, text="선택한 캐릭터 지정", command=self.select_nearby)
        self.select_button.grid(row=8, column=0, columnspan=3, sticky="ew", pady=(0, 10))
        ttk.Label(frame, textvariable=self.status, wraplength=530).grid(
            row=9, column=0, columnspan=3, sticky="w")
        frame.columnconfigure(1, weight=1)
        frame.rowconfigure(7, weight=1)
        self.refresh_processes()
        self.after(2000, self.poll_processes)

    def start_driver(self):
        self.driver_button.configure(state="disabled")
        self.driver_status.set("시작 확인 중…")

        def worker():
            result = start_driver()
            try:
                self.after(0, lambda: self._driver_started(result))
            except RuntimeError:
                pass  # The window was closed while the service request was running.

        Thread(target=worker, daemon=True).start()

    def _driver_started(self, result):
        self.driver_status.set(service_status())
        self.status.set(result)
        self.driver_button.configure(state="normal")

    def refresh_processes(self, silent=False):
        try:
            processes = list_processes()
        except (DarkEdenError, OSError) as exc:
            if not silent:
                self.status.set(str(exc))
            return
        labels = [str(pid) for pid in processes]
        previous = self.pid.get()
        self.process_box["values"] = labels
        if previous not in labels:
            self.pid.set(labels[0] if labels else "")
            self.nearby_tree.delete(*self.nearby_tree.get_children())
        if not silent or previous != self.pid.get():
            self.status.set("게임 프로세스를 새로 선택했습니다." if previous and labels else
                            "프로세스를 선택하고 읽기를 누르세요." if labels else
                            "darkeden.exe가 실행 중이지 않습니다.")

    def poll_processes(self):
        self.refresh_processes(silent=True)
        self.after(2000, self.poll_processes)

    def read_all(self):
        if not self.pid.get():
            self.status.set("대상 프로세스가 없습니다. 새로고침하세요.")
            return
        errors = []
        for kind in ValueType:
            try:
                self.values[kind].set(str(read_value(kind, pid=int(self.pid.get()))))
            except (DarkEdenError, ValueError) as exc:
                self.values[kind].set("오류")
                errors.append(f"{kind.name}: {exc}")
        self.status.set(" / ".join(errors) if errors else "모든 값을 읽었습니다.")

    def refresh_nearby(self):
        if not self.pid.get():
            self.status.set("대상 프로세스가 없습니다. 새로고침하세요.")
            return
        pid = int(self.pid.get())
        self.nearby_button.configure(state="disabled")
        self.status.set("주변 캐릭터 목록을 읽는 중…")

        def worker():
            try:
                result = list_nearby_characters(pid)
                error = None
            except (DarkEdenError, ValueError, OSError) as exc:
                result, error = None, str(exc)
            try:
                self.after(0, lambda: self._nearby_loaded(pid, result, error))
            except RuntimeError:
                pass

        Thread(target=worker, daemon=True).start()

    def _nearby_loaded(self, pid, result, error):
        self.nearby_button.configure(state="normal")
        if self.pid.get() != str(pid):
            return
        if error:
            self.status.set(f"주변 캐릭터 목록: {error}")
            return
        self.nearby_tree.delete(*self.nearby_tree.get_children())
        for character in result:
            self.nearby_tree.insert("", "end", values=(character["name"], character.get("race", "미확인"),
                                                          character.get("life", "미확인"),
                                                          character["x"], character["y"],
                                                          f'{character["id"]:04X}'))
        self.status.set(f"주변 캐릭터 {len(result)}명을 읽었습니다.")

    def select_nearby(self):
        if not self.pid.get():
            self.status.set("게임 프로세스를 선택하세요.")
            return
        selected = self.nearby_tree.selection()
        if len(selected) != 1:
            self.status.set("주변 캐릭터 목록에서 한 명을 선택하세요.")
            return
        values = self.nearby_tree.item(selected[0], "values")
        name, target_id = str(values[0]), int(str(values[5]), 16)
        pid = int(self.pid.get())
        self.select_button.configure(state="disabled")
        self.status.set(f"{name} 선택 중…")

        def worker():
            try:
                select_nearby_character(pid, target_id)
                error = None
            except (DarkEdenError, OSError, ValueError) as exc:
                error = str(exc)
            try:
                self.after(0, lambda: self._target_selected(pid, name, error))
            except RuntimeError:
                pass

        Thread(target=worker, daemon=True).start()

    def _target_selected(self, pid, name, error):
        self.select_button.configure(state="normal")
        if self.pid.get() != str(pid):
            return
        self.status.set(f"캐릭터 선택: {error}" if error else f"{name}을(를) 선택했습니다.")


if __name__ == "__main__":
    App().mainloop()
