<!-- Ausgelagert aus CLAUDE.md am 2026-08-19.  Diese Datei gilt WIE CLAUDE.md,
     sobald an diesem Teilsystem gearbeitet wird — sie ist nur nicht mehr in jeder
     Anfrage geladen.  Begruendung: doc/merkposten/README.md -->

# Verteilbares Paket (`packaging/`) — Merkposten

`packaging/build_payload.sh` schnürt aus dem Baum ein Anwenderpaket (~2 MB:
Kernbibliothek, GUI, `formats.yaml`, Beispieldisketten); `install.sh` darin holt sich mit
**`uv`** Python und Qt in ein venv **innerhalb der Installation** — benutzerlokal, ohne
Administratorrechte. Python/Qt werden bewusst nicht mitverteilt. Bedienung:
`packaging/README.md`, Entwurf und Begründungen: **`doc/design/13_distribution.md`**.
Umgesetzt sind Linux/macOS (Schritt 1+2) **und Windows**: `packaging/k1520emu.iss`
(Inno Setup ≥ 6.5, per-user) — gebaut mit `build_payload.sh --setup`, gefahren im Job
`paket` von `windows-ci.yml` (`-f paket=true`).

**Der Windows-Assistent installiert SELBST — es gibt kein `install.ps1` mehr** (seit
2026-08-14). Drei Dinge daran nicht kaputtmachen:
- **Kein PowerShell im Installationsweg.** Es kostete ein schwarzes Fenster ohne
  Rückmeldung und scheiterte an der Ausführungsrichtlinie. Guard:
  `test_iss_ruft_kein_powershell`.
- **Python kommt direkt von python-build-standalone** (`packaging/python_pins.txt`,
  `--refresh-python`), NICHT über `uv`: dessen Junction auf die Nebenversion scheitert
  unter OneDrive „Dateien bei Bedarf" mit `os error 448`
  (`STATUS_UNTRUSTED_MOUNT_POINT`, astral-sh/uv #19616) — abschalten lässt er sich
  nicht. Unter Linux bleibt uv.
- **Alles Nachladbare läuft in `PrepareToInstall`, also VOR dem Kopieren.** Eine
  Ausnahme in `ssPostInstall` räumt nichts zurück und hinterlässt eine halbe
  Installation mit Startmenü-Einträgen ins Leere. Guards:
  `test_iss_laedt_und_richtet_ein_bevor_kopiert_wird`,
  `test_iss_raeumt_auf_wenn_das_nachladen_scheitert`. Dateien, die der Bootstrap dort
  schon braucht, müssen `dontcopy` sein (`[Files]` wird erst danach abgearbeitet).

Sieben Dinge, die man dabei nicht kaputtmachen darf:

- **`--uninstall` löscht in seinem Ziel, und das Ziel wird ERFRAGT.** Deshalb zwei
  Riegel in `install.sh`: Ziel werden darf nur ein leeres oder bereits von uns belegtes
  Verzeichnis (nie `$HOME`, nie `/`), und gelöscht wird nur, was sich ausweist
  (`.k1520emu-installation`, ersatzweise `VERSION`+`app/paths.py`+`share/k1520emu/`).
  Ohne das löschte die Antwort „`~`" beim Deinstallieren das Heimatverzeichnis — belegt,
  nicht theoretisch. Und gelöscht wird **nur das Inventar aus dem Ausweis** (die Einträge,
  die der Installer anlegte; ein Eintrag ist ein NAME, kein Pfad), nicht die Wurzel als
  Ganzes — fremde Dateien im Ordner überleben, dann bleibt auch der Ordner stehen. Guards:
  `test_installer_verweigert_*`, `test_deinstallieren_loescht_nur_eine_installation`,
  `test_deinstallieren_laesst_eigene_dateien_stehen`,
  `test_deinstallieren_folgt_keinem_pfad_im_ausweis`.
- **Ein Update findet seine Installation selbst** (`vorhandene_installation()` über den
  Starter-Symlink) und schlägt sie als Ziel vor. Ohne das legte ein `install.sh` ohne
  `--prefix` eine ZWEITE Installation am Standardort an und ließe die alte verwaisen.
- **`slim.py` strippt Bibliotheken, aber NIE Programme.** Der Interpreter von
  python-build-standalone überlebt `strip` in keiner Variante („allocated section `.dynstr'
  not in segment" → „undefined symbol: , version"); gearbeitet wird auf einer Kopie, und
  eine Warnung von `strip` verwirft sie. Ebenso: die `ldd`-Zeile wird an der **Ladeadresse
  am Ende** getrennt, nicht am ersten Leerzeichen — sonst kippt bei einem Installationspfad
  mit Leerzeichen die ganze Qt-Hülle in den Sicherheitsrückfall (223 statt 146 MB).
  Begründungen: `doc/design/13_distribution.md` §8.1.
- **`FormatCatalog` findet seine `formats.yaml` über den Pfad des *eigenen Moduls***
  (`dladdr` / `GetModuleHandleEx`, `format_catalog.cpp: moduleDir()`), nicht über
  `/proc/self/exe` — sonst sucht die per `ctypes` geladene Bibliothek neben dem
  venv-Python. Guard: `py_paths`.
- **Release-Bauten setzen `-DK1520_FORMATS_DEFAULT=`** — sonst trägt jede ausgelieferte
  Bibliothek den absoluten Pfad des Baurechners als Suchkandidaten. Guard: `py_packaging`.
- **Arbeitsdisketten liegen außerhalb der Installation**, im **Dokumentenordner**
  (`<Dokumente>/K1520emu/Disketten`), weil der Autosave in die gemountete Datei
  zurückschreibt. Der Ordnername ist sprachabhängig — maßgeblich ist `XDG_DOCUMENTS_DIR`
  aus `~/.config/user-dirs.dirs`, und die Regel steht ZWEIMAL (`paths.documents_dir()` und
  `dokumente_dir()` in `lib/common.sh`, damit `--purge` dort aufräumt, wo der Emulator
  schreibt; Guard: `test_dokumentenordner_shell_und_python_stimmen_ueberein`). Im Paket
  liegen die Abbilder **gepackt** (`*.hfe.gz`), ausgepackt wird beim ersten Start
  (`paths.seed_user_disks()`).
- **Produkt = `k1520emu`, Programm = `a5120emu`.** Installation, Paketname, `share/k1520emu/`,
  Datenordner und Marker tragen den FAMILIENnamen (der Bus, nicht der Rechner); Starter,
  Symbol und `.desktop` heißen nach der Maschine. Weitere K1520-Rechner bekommen ein eigenes
  Programm in derselben Installation: eigener Block beim Starterschreiben + `<name>.desktop.in`
  + Eintrag in `MASCHINEN` (`install.sh`), woran das Deinstallieren die Verknüpfungen findet.
  **Seit AP-UI1 (2026-09-30) gibt es den zweiten: den K8915 Emulator** (`k8915emu`).
  Dieselbe Starter-Vorlage wie `a5120emu` — `launcher.sh`/`launcher.cmd` wählen das
  Programmprofil am eigenen NAMEN (`k8915emu*` ⇒ `app/main.py --machine k8915`),
  `install.sh` schreibt sie zweimal; `k8915emu.desktop.in` (Name „K8915 Emulator",
  `Icon=a5120emu`); Windows: zweiter `[Icons]`-Eintrag `{#Programm2}` mit
  `--machine k8915` und `bin\k8915emu.cmd`. Startmenü-Namen „A5120 Emulator" /
  „K8915 Emulator" (vorher „A5120-Emulator"). Auslieferungskonfiguration je Programm:
  `share/k1520emu/default_config_a5120.yaml` / `…_k8915.yaml`. Die
  K8915-Systemdiskette 901 (`k8915scpx_boot1.hfe`) gehört seit 2026-10-01 zur
  Vorgabeauswahl (Entscheid des Anwenders, `16_k8915.md` §6.23; Wächter
  `test_k8915_systemdiskette_ist_in_der_vorgabeauswahl`).
  **Seit AP-P5g (2026-10-02) gibt es den dritten: den PRG710 Emulator** (`prg710emu`,
  `--machine prg710`, Modellwahl 710/710-1 im Programm): dieselbe Vorlage, `prg710emu.desktop.in`,
  `{#Programm3}` im .iss, `default_config_prg710.yaml` nach `share/k1520emu/`, Eintrag in
  `MASCHINEN` — Wächter `test_der_prg710_emulator_ist_in_allen_paketwegen`. Seit AP-P5h
  (2026-10-03, Entscheid des Anwenders) gehören vier PRG-Systemdisketten (UDOS und SCPX, je 710/710-1)
  zur Vorgabeauswahl; Wächter `test_prg710_systemdisketten_sind_in_der_vorgabeauswahl`.
  **Seit AP-5c (2026-10-04) gibt es den vierten: den PC1715 Emulator** (`pc1715emu`,
  `--machine pc1715`, Modellwahl = Bildschirm K7222/K7221): dieselbe Vorlage, `pc1715emu.desktop.in`,
  `{#Programm4}` im .iss, `default_config_pc1715.yaml`, Eintrag in `MASCHINEN`; Wächter
  `test_der_pc1715_emulator_ist_in_allen_paketwegen`.  Vorgabeauswahl um vier Disketten
  (SCP 1715 V0006/V0007, CP/A 1715, UDOS1715; Wächter
  `test_pc1715_disketten_sind_in_der_vorgabeauswahl`); die Vorgabe mountet keine (kein `disks:`).
  Rauchtests erzeugen jetzt auch `k1520_create(3)` und das PC1715-Fenster.
  **Seit AP P18 (2026-10-08) gibt es den fünften: P8000 Emulator (`p8000emu`, `--machine p8000`)**; das zeitweilige sechste `p8000term` ist mit P23a entfallen (Betriebsart „nur Terminal“ im selben Programm, `--mode terminal`): dieselbe Vorlage (der Name wählt das Profil), `.desktop.in`, `{#Programm5}`, Vorvorgabedatei, Rauchtests mit `k1520_create(4)` und dem Fenster; **keine
  P8000-Disketten** in `DISKS_DEFAULT` (es gibt keine in `disks/`, Entscheid offen). Wächter
  `test_die_p8000_programme_sind_in_allen_paketwegen`.  Im `.iss` wächst das Rauchtest-Feld
  `Zeilen` mit (SetArrayLength + laufende Nummern: neue Zeilen ANS ENDE, vor `sys.stdout.flush()`).
  **Alle Rauchtests (install.sh, .iss, beide release.yml-Jobs) erzeugen ALLE Maschinen**
  (`k1520_create(0)`, `(1)` und `(2)`) — Wächter `test_rauchtests_pruefen_beide_maschinen`. Wächter `py_packaging`
  (`test_launcher_sh_waehlt_die_maschine_am_namen`, `test_iss_hat_den_k8915_emulator_im_startmenue`,
  `test_install_sh_schreibt_und_kennt_den_k8915_starter`).

**Drei Programme, nicht eins** (2026-08-18, `doc/design/13_distribution.md` §10a/§10a.5).
Neben Emulator und DiskTool liefert das Paket den **Debugger `k1520dbg`** aus, dazu die
**Greaseweazle-Anbindung** für echte Laufwerke. Vier Festlegungen:
- **Der Debugger hat kein Symbol, aber drei Hinweise.** Er wird in einen vorhandenen
  Arbeitsablauf aus Editor, Assembler und Konsole eingebunden — deshalb nur ein Verweis
  in `~/.local/bin` (Linux, Liste **`KONSOLENWERKZEUGE`** in `install.sh`; dabei fiel auf,
  dass `k1520disktool-cli` in KEINER Aufräumliste stand und als toter Verweis liegenblieb)
  bzw. `bin\k1520dbg.cmd` (Windows). Verknüpft wird unter Windows die **`.cmd`**
  (`cmd /k`, bleibt stehen), NIE `k1520dbg.exe` (das Fenster ginge mangels Diskette sofort
  wieder zu) — Wächter `test_iss_schreibt_die_werkzeug_eingabeaufforderung`. Gesagt wird es
  in der Schlussmeldung von `install.sh`, auf einer eigenen Assistentenseite **nach dem
  Kopieren** (`wpInfoAfter` — vorher wären die Pfade noch leer) und in `paket_readme.md`.
- **`k1520dbg` hat keinen `--help`-Schalter** — jedes freie Argument gilt als Diskette. Der
  Rauchtest ist deshalb überall `printf 'q\n' | k1520dbg` (Sitzung auf, Sitzung zu); eine
  Diskette braucht er dafür nicht.
- **Greaseweazle liegt als fertiges wheel im Paket** — ein *wheel* (`.whl`) ist das
  einspielfertige Format für Python-Pakete, das `pip` nur noch auspackt statt es zu
  bauen (`packaging/gw_pins.txt`, `wheels/`
  neben der Payload). Es liegt **nicht auf PyPI**, und sein Quellarchiv erklärt eine
  C-Erweiterung, die beim Anwender übersetzt werden müsste — unter Windows aussichtslos.
  Das wheel ist daher **`py3-none-any`**, die Erweiterung entfällt über einen
  **vorgeschalteten Aufsatz** (`setuptools.setup` abfangen, `ext_modules` verwerfen; nicht
  in `setup.py` schneiden — das hielte die nächste Fassung nicht). Kosten: **~25 ms je
  Spur** gegen 500–800 ms Lesezeit. Solange `ext_modules` gesetzt ist, wird das wheel an
  Plattform UND ABI gebunden, auch ohne Übersetzung — `build_payload.sh` prüft den Namen
  nach. Die vier Abhängigkeiten (crcmod, bitarray, pyserial, requests) kommen mit
  Prüfsumme aus `requirements.lock`; das wheel selbst spielt der Installer mit **`--no-deps`**
  ein, und ein Fehlschlag dabei wirft die Installation NICHT hin.
- **Ohne die C-Erweiterung meldet sich `greaseweazle.optimised` auf der STANDARDAUSGABE.**
  Bei `k1520disktool --physical` ist die die Nutzlast — `app/gw/device.py` liest die
  Schicht deshalb nur noch über **`_leise()`** ein (`redirect_stdout(sys.stderr)`);
  umgeleitet, nicht verworfen. Wächter `test_die_anbindung_schreibt_nicht_auf_die_nutzlast`
  (es darf **keinen** Import an `_leise` vorbei geben).

Tests: `py_paths` + `py_packaging` (schnell, ohne Netz, in der Standardregression). Der
vollständige Installationslauf (lädt ~120 MB) liegt hinter
`K1520_PACKAGING_FULL=1 venv/bin/python3 -m pytest tests/python/test_packaging.py`.
