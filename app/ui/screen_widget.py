"""
K1520 Emulator - Screen Display Widget (CRT / green phosphor)
=============================================================

GPU-rendered emulation of the A5120's green-phosphor CRT.

The K7024 core delivers a 640x288 byte framebuffer (one byte per pixel,
0x00 = off, 0xFF = on).  This widget uploads that framebuffer as an 8-bit
texture once per changed frame and renders it through a GLSL fragment shader
that reproduces the *look* of a real picture tube:

  - green-phosphor tint (on/off colours)
  - horizontal scanlines
  - phosphor glow / bloom
  - subtle barrel (tube) curvature + rounded corners
  - vignette and optional flicker
  - aperture-grille style mask

All the heavy per-pixel work happens on the GPU, so language/host overhead is
irrelevant: Python only uploads one texture and issues a handful of draw calls
per frame.  The C-ABI boundary is unchanged - the core still only emits raw
framebuffer bytes; all "make it look like a tube" logic lives here in the
frontend.

The display is deliberately stretched to a **4:3** aspect ratio (the visible
area of the real A5120 tube), letterboxed inside the widget, freely scalable,
and can go fullscreen (F11).

Tuning: every visual parameter lives in :class:`CRTParams` with in-code
defaults.  Edit the defaults (or assign to ``widget.params`` at runtime and the
next frame picks them up) to dial the look toward the real screen.
"""

from dataclasses import dataclass, field
from typing import Optional, Tuple

from PySide6.QtWidgets import QWidget
from PySide6.QtGui import QImage
from PySide6.QtCore import Qt, QTimer, QSize, QElapsedTimer, Signal
from PySide6.QtOpenGLWidgets import QOpenGLWidget
from PySide6.QtOpenGL import (
    QOpenGLShader,
    QOpenGLShaderProgram,
    QOpenGLBuffer,
    QOpenGLVertexArrayObject,
    QOpenGLTexture,
)

from app.ui.keyboard import qt_event_to_core_key

# ── Raw GL enum constants (avoid a PyOpenGL dependency) ───────────────────────
GL_COLOR_BUFFER_BIT = 0x00004000
GL_TRIANGLE_STRIP = 0x0005
GL_FLOAT = 0x1406
GL_TEXTURE0 = 0x84C0

# K7024 framebuffer geometry (pixels emitted by the core).
FB_WIDTH = 640
FB_HEIGHT = 288

# Displayed aspect ratio: the real A5120 tube is 4:3, so the 640x288 (20:9)
# framebuffer is intentionally stretched to fill a 4:3 area (letterboxed).
DISPLAY_ASPECT = 4.0 / 3.0


def hex_to_rgb(s: str) -> Tuple[float, float, float]:
    """'#RRGGBB' -> (r, g, b) in 0..1."""
    s = s.lstrip("#")
    return (int(s[0:2], 16) / 255.0,
            int(s[2:4], 16) / 255.0,
            int(s[4:6], 16) / 255.0)


def rgb_to_hex(rgb) -> str:
    """(r, g, b) in 0..1 -> '#RRGGBB'."""
    return "#{:02X}{:02X}{:02X}".format(
        *(max(0, min(255, int(round(c * 255)))) for c in rgb))


@dataclass
class CRTParams:
    """Tunable CRT look parameters (all read into shader uniforms each frame).

    Edit the defaults here to dial the emulation toward the real tube, or set
    ``screen_widget.params = CRTParams(...)`` / mutate fields at runtime.  A few
    fields are fixed (no GUI control) — change them here or via a saved config.
    """

    # Phosphor colours ("Farbe aktiv" / "Farbe inaktiv"), linear 0..1 RGB.
    phosphor_on: Tuple[float, float, float] = hex_to_rgb("#78d41d")   # lit pixel
    phosphor_off: Tuple[float, float, float] = hex_to_rgb("#050f05")  # dark pixel

    # Overall image (adjustable).
    brightness: float = 2.5
    contrast: float = 2.0

    # Scanlines: strength, count = number of horizontal lines (image rows).
    # Fixed (no GUI control).
    scanline_strength: float = 1.4
    scanline_count: float = float(FB_HEIGHT)   # 288

    # Phosphor glow / bloom: strength and blur radius in framebuffer texels.
    # Fixed (no GUI control).
    glow_strength: float = 1.0
    glow_radius: float = 2.5

    # Aperture-grille style vertical mask.  A monochrome green tube has NO shadow
    # mask, so this is fixed off (no GUI control).
    mask_strength: float = 0.0
    mask_pitch: float = 1.0

    # Tube curvature (barrel distortion) amount per axis, and rounded corners
    # (adjustable).
    curvature: Tuple[float, float] = (0.1, 0.1)
    corner_radius: float = 0.1        # 0 = square

    # Edge darkening — fixed (no GUI control).
    vignette_strength: float = 0.35

    # Optional mains-hum flicker — fixed off (no GUI control).
    flicker_strength: float = 0.0

    # Raster geometry (like the CRT's H/V size + position controls): the image
    # can be made smaller than the tube face and shifted, so its content clears
    # the rounded corners.  scale 1.0 = fills the tube; offset in tube-width units.
    scale_x: float = 0.98
    scale_y: float = 0.98
    offset_x: float = 0.0
    offset_y: float = 0.0

    # ── (de)serialisation for config files ───────────────────────────────────

    def to_dict(self) -> dict:
        """Plain dict for YAML; colours as '#RRGGBB', geometry as lists."""
        return {
            "phosphor_on": rgb_to_hex(self.phosphor_on),
            "phosphor_off": rgb_to_hex(self.phosphor_off),
            "brightness": self.brightness,
            "contrast": self.contrast,
            "scanline_strength": self.scanline_strength,
            "scanline_count": self.scanline_count,
            "glow_strength": self.glow_strength,
            "glow_radius": self.glow_radius,
            "mask_strength": self.mask_strength,
            "mask_pitch": self.mask_pitch,
            "curvature": list(self.curvature),
            "corner_radius": self.corner_radius,
            "vignette_strength": self.vignette_strength,
            "flicker_strength": self.flicker_strength,
            "scale_x": self.scale_x,
            "scale_y": self.scale_y,
            "offset_x": self.offset_x,
            "offset_y": self.offset_y,
        }

    def update_from_dict(self, d: dict):
        """Apply values from a config dict in place (tolerant of missing keys)."""
        if not isinstance(d, dict):
            return
        for key in ("phosphor_on", "phosphor_off"):
            if key in d:
                v = d[key]
                setattr(self, key, hex_to_rgb(v) if isinstance(v, str) else tuple(v))
        if "curvature" in d and d["curvature"] is not None:
            self.curvature = tuple(d["curvature"])
        for key in ("brightness", "contrast", "scanline_strength",
                    "scanline_count", "glow_strength", "glow_radius",
                    "mask_strength", "mask_pitch", "corner_radius",
                    "vignette_strength", "flicker_strength", "scale_x",
                    "scale_y", "offset_x", "offset_y"):
            if key in d and d[key] is not None:
                setattr(self, key, float(d[key]))


# ── GLSL shaders (GLSL 1.20 - broadly compatible desktop profile) ─────────────

_VERT_SHADER = """
#version 120
attribute vec2 aPos;
attribute vec2 aUv;
varying vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
"""

_FRAG_SHADER = """
#version 120
uniform sampler2D uScreen;
uniform vec2  uResolution;     // framebuffer size in texels (640, 288)
uniform vec3  uPhosphorOn;
uniform vec3  uPhosphorOff;
uniform float uBrightness;
uniform float uContrast;
uniform float uScanline;
uniform float uScanlineCount;
uniform float uGlow;
uniform float uGlowRadius;
uniform float uMask;
uniform float uMaskPitch;
uniform vec2  uCurvature;
uniform float uCorner;
uniform float uVignette;
uniform float uFlicker;
uniform float uTime;
uniform vec2  uScale;          // raster width/height (1.0 = fills the tube)
uniform vec2  uOffset;         // raster shift, in tube-width units
varying vec2 vUv;

const float PI = 3.14159265;

// Barrel-distort uv around the centre to fake the curved tube surface.
vec2 curve(vec2 uv) {
    vec2 c = uv * 2.0 - 1.0;            // -1..1
    vec2 off = abs(c.yx) * uCurvature;  // bend more toward the edges
    c += c * off * off;
    return c * 0.5 + 0.5;              // back to 0..1
}

// Sample the (binary) screen texture; 0 outside the raster rectangle.
// The fetch is kept in *uniform* control flow (clamp + multiply instead of an
// early return) so the screen-space derivatives — and thus automatic mipmap LOD
// selection — stay well-defined when the widget is minified.  Without mipmaps a
// downscaled tube drops thin glyph strokes (parts of letters "swallowed"); the
// trilinear mip chain averages them down instead.
float sampleOn(vec2 uv) {
    float inside = (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
                   ? 0.0 : 1.0;
    return texture2D(uScreen, clamp(uv, 0.0, 1.0)).r * inside;
}

void main() {
    vec2 tube = curve(vUv);

    // Outside the (curved) tube surface -> black.
    if (tube.x < 0.0 || tube.x > 1.0 || tube.y < 0.0 || tube.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Map tube coordinates into the (scaled + shifted) raster coordinates.
    vec2 uv = (tube - 0.5 - uOffset) / uScale + 0.5;
    float inR = (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0)
                ? 1.0 : 0.0;

    // Sharp pixel core plus a normalised gaussian halo (soft phosphor aura).
    // sigma = uGlowRadius in texels; ±3 sigma window.  A monochrome tube has no
    // shadow mask, so this halo — not a mask — is what softens the pixels.
    // How hard the brightness knob is pushed past the "normal" full-scale range.
    // 0 for brightness <= 2.0 (the well-behaved region), growing to 2.0 at the
    // slider max (4.0).  Above 2.0 the tube is "overdriven": whiter/blooming
    // pixels and eventually a faintly glowing raster (see below).  Everything
    // keyed off this is exactly 0 at 2.0, so the <=2.0 look is bit-identical.
    float drive = max(uBrightness - 2.0, 0.0);
    // 0 at brightness 2.0, ramps to 1.0 at the slider max (4.0).  Drives the
    // fade-out of the darkening filters (scanlines / vignette) so cranking the
    // brightness removes the "grey veil" instead of adding to it.
    float od = clamp(drive * 0.5, 0.0, 1.0);
    // Fade for the darkening filters: 0 at brightness 2.0, fully 1.0 at 3.0, so
    // scanlines and the vignette are completely gone from 3.0 upward.
    float veilFade = clamp(drive, 0.0, 1.0);
    // Glow overdrive only kicks in above brightness 3.0.
    float glowDrive = max(uBrightness - 3.0, 0.0);

    float base = sampleOn(uv);
    float lit = base;
    if (uGlow > 0.0) {
        float sigma = max(uGlowRadius, 0.001);
        vec2 texel = 1.0 / uResolution;
        float acc = 0.0;
        float wsum = 0.0;
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dy = -3; dy <= 3; ++dy) {
                float w = exp(-float(dx * dx + dy * dy) / (2.0 * sigma * sigma));
                acc += w * sampleOn(uv + vec2(float(dx), float(dy)) * texel);
                wsum += w;
            }
        }
        // Overdrive slightly widens the phosphor aura (subtle bloom / over-steer),
        // but only above brightness 3.0.
        lit = base + (acc / wsum) * uGlow * (1.0 + glowDrive * 0.18);
    }

    // Brightness / contrast operate on the *intensity* (not RGB).  Up to full
    // scale the output stays on the phosphor off<->on line; the overdrive terms
    // below take over once the beam is driven past that point.
    // Contrast pivots around the "on" level: contrast=0 -> whole screen "on".
    lit = (lit - 1.0) * uContrast + 1.0;
    float inten = lit * uBrightness;         // may exceed 1.0 (overdrive headroom)
    float litC = clamp(inten, 0.0, 1.0);

    vec3 col = mix(uPhosphorOff, uPhosphorOn, litC);

    // Overdrive #1 — white core: a harder-driven beam reads whiter.  Weighted by
    // litC so only lit pixels/halos gain white, never the dark background.
    float white = od * litC;
    col = mix(col, vec3(1.0), white * 0.5);

    // Overdrive #2 — ambient raster glow: past ~3.5 the whole tube (incl. unlit
    // cells) starts to glow *faintly* toward the phosphor colour, the way a real
    // CRT's brightness knob eventually makes the blanked raster visible.  Kept
    // low so the text/background contrast (readability) survives.
    float ambient = clamp((uBrightness - 3.5) / 0.5, 0.0, 1.0) * 0.18;
    col = max(col, uPhosphorOn * ambient);

    // Outside the raster (but inside the tube) is unlit -> black.
    col *= inR;

    // Scanlines: one bright hump per image row (locked to the raster rows).
    // Faded out with overdrive (veilFade) so high brightness lifts the "grey
    // veil" instead of banding the image darker.
    //
    // Resolution-aware attenuation: uScanlineCount humps are fixed to the raster,
    // so when the widget is scaled down a single output pixel can span a whole
    // scanline period (or more).  Below the Nyquist limit the pattern can't be
    // drawn faithfully and instead beats against the host display's pixel rows
    // (moiré / bright-dark banding) while eating the glyphs.  fwidth(sp) is the
    // number of scanline periods covered by one output pixel; once that reaches
    // ~0.5 (i.e. < 2 output px per line) we fade the scanlines out entirely.
    float sp = uv.y * uScanlineCount;
    float scanVis = 1.0 - smoothstep(0.5, 1.0, fwidth(sp));
    float scanStr = uScanline * (1.0 - veilFade) * scanVis;
    float f = fract(sp);
    float scan = 1.0 - scanStr * (1.0 - sin(f * PI));
    col *= scan;

    // Aperture-grille style vertical mask (subtle for a monochrome tube).
    float mphase = fract(gl_FragCoord.x / uMaskPitch);
    float mask = mix(1.0, 0.75 + 0.25 * sin(mphase * 2.0 * PI), uMask);
    col *= mask;

    // Vignette (darken toward the edges of the tube face).  Faded out with
    // overdrive (gone from 3.0 up); and below brightness 1.0 it *increases*
    // continuously (up to 3x at brightness 0), dimming the tube from the edges in.
    vec2 d = tube - 0.5;
    float vigBoost = 1.0 + clamp(1.0 - uBrightness, 0.0, 1.0) * 2.0;
    float vig = 1.0 - (uVignette * vigBoost * (1.0 - veilFade)) * dot(d, d) * 4.0;
    col *= clamp(vig, 0.0, 1.0);

    // Rounded corners (mask out the tube corners).
    if (uCorner > 0.0) {
        vec2 q = abs(tube * 2.0 - 1.0);
        vec2 cd = max(q - (1.0 - uCorner), 0.0) / uCorner;
        float r = length(cd);
        col *= 1.0 - smoothstep(0.9, 1.0, r);
    }

    // Optional flicker.
    if (uFlicker > 0.0) {
        col *= 1.0 - uFlicker * (0.5 + 0.5 * sin(uTime * 120.0));
    }

    gl_FragColor = vec4(col, 1.0);
}
"""


class ScreenWidget(QOpenGLWidget):
    """GPU CRT display widget for the K1520 framebuffer."""

    # Kept for backward compatibility with callers/tests referencing them.
    COLS = 80
    ROWS = 24
    WIDTH = FB_WIDTH
    HEIGHT = FB_HEIGHT

    # Fullscreen is owned by the main window (reparenting a QOpenGLWidget crashes
    # the GL context), so the widget only *requests* it via these signals.
    toggleFullscreenRequested = Signal()
    exitFullscreenRequested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)

        self.emulator = None
        self.params = CRTParams()

        # Power state: when off, the tube is dark and the framebuffer is not
        # polled (as if the machine had no power).
        self._powered = True

        # Latest framebuffer bytes waiting to be uploaded to the GL texture.
        self._fb_bytes: Optional[bytes] = None
        self._pending_upload = False

        # GL objects (created in initializeGL, recreated on context loss).
        self._program: Optional[QOpenGLShaderProgram] = None
        self._vbo: Optional[QOpenGLBuffer] = None
        self._vao: Optional[QOpenGLVertexArrayObject] = None
        self._texture: Optional[QOpenGLTexture] = None
        self._loc_pos = -1
        self._loc_uv = -1

        # Wall-clock for time-based effects (flicker).
        self._clock = QElapsedTimer()
        self._clock.start()

        # Poll the core for framebuffer changes at 50 Hz (like the hardware).
        self.timer = QTimer(self)
        self.timer.timeout.connect(self._on_update)
        self.timer.setInterval(20)  # 20 ms = 50 Hz

        # Scalable: allow any size >= a sensible minimum, keep a 4:3 hint.
        self.setMinimumSize(320, 240)
        self.setFocusPolicy(Qt.StrongFocus)

    # ── Public API (unchanged from the old widget) ───────────────────────────

    def set_emulator(self, emulator):
        """Connect to the emulator core binding."""
        self.emulator = emulator

    def start_display(self):
        """Start polling the framebuffer and repainting."""
        self.timer.start()

    def stop_display(self):
        """Stop display updates."""
        self.timer.stop()

    def set_powered(self, on: bool):
        """Schaltet die Röhre ein/aus.  Aus = schwarzer Bildschirm, kein Polling."""
        self._powered = bool(on)
        if on:
            self.start_display()
        else:
            self.stop_display()
        self.update()   # sofort neu zeichnen (schwarz bzw. aktuelles Bild)

    def sizeHint(self) -> QSize:
        return QSize(640, 480)  # 4:3

    # ── Fullscreen (handled by the main window; see signals above) ────────────

    def focusNextPrevChild(self, next_widget: bool) -> bool:
        """Kein Fokuswechsel per Tabulator — TAB gehört dem emulierten Rechner.

        Qt wertet Tab/Shift-Tab normalerweise VOR :meth:`keyPressEvent` als
        Fokusweiterschaltung aus; die Taste käme dann nie beim K7637 an
        (``qt_event_to_core_key`` bildet ``Key_Tab`` bewusst ab).  ``False``
        heißt „nicht weitergeschaltet" — die Taste läuft normal weiter.
        """
        return False

    def keyPressEvent(self, event):
        key = event.key()
        # F11 toggelt Vollbild und wird NICHT an den Emulator weitergereicht.
        if key == Qt.Key_F11:
            self.toggleFullscreenRequested.emit()
            event.accept()
            return
        # Escape verlässt ggf. das Vollbild; das Fenster ignoriert das Signal,
        # wenn kein Vollbild aktiv ist.  ESC wird zusätzlich an den Emulator
        # gereicht (CP/A braucht die ESC-Taste), s.u.
        if key == Qt.Key_Escape and event.modifiers() == Qt.NoModifier:
            self.exitFullscreenRequested.emit()

        # Alle übrigen Tasten an den K7637/Core weiterleiten.  Ohne diese
        # Weiterleitung erreichte bisher KEINE Host-Eingabe den Emulator.
        if self._forward_key_press(event):
            event.accept()
            return
        super().keyPressEvent(event)

    def keyReleaseEvent(self, event):
        if event.key() == Qt.Key_F11:
            event.accept()
            return
        if self._forward_key_release(event):
            event.accept()
            return
        super().keyReleaseEvent(event)

    def _forward_key_press(self, event) -> bool:
        if self.emulator is None or event.isAutoRepeat():
            return False
        mapped = qt_event_to_core_key(event)
        if mapped is None:
            return False
        self.emulator.key_press(*mapped)
        return True

    def _forward_key_release(self, event) -> bool:
        if self.emulator is None or event.isAutoRepeat():
            return False
        mapped = qt_event_to_core_key(event)
        if mapped is None:
            return False
        self.emulator.key_release(mapped[0])
        return True

    # ── Framebuffer polling (runs outside the GL context) ────────────────────

    def _on_update(self):
        if not self.emulator or not self._powered:
            return
        if not self.emulator.is_framebuffer_dirty():
            return
        # Copy the framebuffer out of the core and stage it for upload.
        self._fb_bytes = bytes(self.emulator.get_framebuffer())
        self.emulator.clear_framebuffer_dirty_flag()
        self._pending_upload = True
        self.update()  # schedule a repaint (paintGL runs with GL current)

    # ── OpenGL lifecycle ─────────────────────────────────────────────────────

    def initializeGL(self):
        # QOpenGLWidget destroys and recreates its GL context whenever the widget
        # is reparented (e.g. docking/undocking or floating the screen dock).
        # Any GL object created against the OLD context must be released while
        # that context is still current, otherwise it is later freed against the
        # wrong context → segfault ("Texture has not been destroyed").  Hook the
        # context's aboutToBeDestroyed to do exactly that; initializeGL then
        # rebuilds everything on the fresh context.
        ctx = self.context()
        if ctx is not None:
            ctx.aboutToBeDestroyed.connect(self._cleanup_gl, Qt.DirectConnection)

        self._build_program()
        self._build_geometry()
        self._build_texture()

        # After a context rebuild, re-upload the last framebuffer we had.
        if self._fb_bytes is not None:
            self._pending_upload = True

    def _cleanup_gl(self):
        """Release all GL objects while the outgoing context is still current.

        Connected to ``QOpenGLContext.aboutToBeDestroyed`` (fires on reparent).
        """
        self.makeCurrent()
        try:
            if self._texture is not None:
                self._texture.destroy()
                self._texture = None
            if self._vbo is not None:
                self._vbo.destroy()
                self._vbo = None
            if self._vao is not None:
                self._vao.destroy()
                self._vao = None
            if self._program is not None:
                self._program.removeAllShaders()
                self._program = None
            self._loc_pos = -1
            self._loc_uv = -1
        finally:
            self.doneCurrent()

    def _build_program(self):
        # No QObject parent: the only owner is self._program, so dropping that
        # reference in _cleanup_gl deletes the C++ object (freeing its GL
        # program) synchronously while the context is still current.
        prog = QOpenGLShaderProgram()
        if not prog.addShaderFromSourceCode(QOpenGLShader.Vertex, _VERT_SHADER):
            raise RuntimeError("CRT vertex shader failed:\n" + prog.log())
        if not prog.addShaderFromSourceCode(QOpenGLShader.Fragment, _FRAG_SHADER):
            raise RuntimeError("CRT fragment shader failed:\n" + prog.log())
        if not prog.link():
            raise RuntimeError("CRT shader link failed:\n" + prog.log())
        self._program = prog
        self._loc_pos = prog.attributeLocation("aPos")
        self._loc_uv = prog.attributeLocation("aUv")

    def _build_geometry(self):
        # Fullscreen quad as a triangle strip: (pos.xy, uv.xy).
        # uv (0,0) at the top-left so framebuffer row 0 renders at the top.
        import struct
        verts = [
            -1.0,  1.0, 0.0, 0.0,
            -1.0, -1.0, 0.0, 1.0,
             1.0,  1.0, 1.0, 0.0,
             1.0, -1.0, 1.0, 1.0,
        ]
        data = struct.pack("%df" % len(verts), *verts)

        self._vao = QOpenGLVertexArrayObject()   # no parent (see _build_program)
        self._vao.create()
        self._vao.bind()

        self._vbo = QOpenGLBuffer(QOpenGLBuffer.VertexBuffer)
        self._vbo.create()
        self._vbo.bind()
        self._vbo.allocate(data, len(data))

        stride = 4 * 4  # 4 floats per vertex
        self._program.bind()
        self._program.enableAttributeArray(self._loc_pos)
        self._program.setAttributeBuffer(self._loc_pos, GL_FLOAT, 0, 2, stride)
        self._program.enableAttributeArray(self._loc_uv)
        self._program.setAttributeBuffer(self._loc_uv, GL_FLOAT, 2 * 4, 2, stride)
        self._program.release()

        self._vbo.release()
        self._vao.release()

    def _build_texture(self):
        tex = QOpenGLTexture(QOpenGLTexture.Target2D)
        tex.setFormat(QOpenGLTexture.R8_UNorm)
        tex.setSize(FB_WIDTH, FB_HEIGHT)
        # Full mip chain so minification (small/scaled-down window) samples an
        # averaged level instead of dropping thin glyph strokes.  Trilinear
        # (LinearMipMapLinear) min filter, plain Linear when magnifying.
        tex.setMipLevels(tex.maximumMipLevels())
        tex.setMinificationFilter(QOpenGLTexture.LinearMipMapLinear)
        tex.setMagnificationFilter(QOpenGLTexture.Linear)
        tex.setWrapMode(QOpenGLTexture.ClampToEdge)
        tex.allocateStorage(QOpenGLTexture.Red, QOpenGLTexture.UInt8)
        self._texture = tex

    def _upload_texture(self):
        """Upload the staged framebuffer bytes into the GL texture."""
        if self._fb_bytes is None or self._texture is None:
            return
        try:
            # Fast path: sub-image the raw single-channel bytes in place, then
            # rebuild the mip chain so the downscaled view stays anti-aliased.
            self._texture.setData(
                QOpenGLTexture.Red, QOpenGLTexture.UInt8, self._fb_bytes
            )
            self._texture.generateMipMaps()
        except (TypeError, RuntimeError):
            # Fallback: rebuild the texture from a grayscale QImage.  QImage is
            # top-row-first while QOpenGLTexture uploads bottom-first, so mirror.
            # The QImage ctor generates a full mip chain by default.
            img = QImage(self._fb_bytes, FB_WIDTH, FB_HEIGHT, FB_WIDTH,
                         QImage.Format_Grayscale8).mirrored(False, True)
            if self._texture is not None:
                self._texture.destroy()
            tex = QOpenGLTexture(img)
            tex.setMinificationFilter(QOpenGLTexture.LinearMipMapLinear)
            tex.setMagnificationFilter(QOpenGLTexture.Linear)
            tex.setWrapMode(QOpenGLTexture.ClampToEdge)
            self._texture = tex

    def resizeGL(self, w: int, h: int):
        # Viewport is (re)computed in paintGL so we can letterbox to 4:3.
        pass

    def paintGL(self):
        f = self.context().functions()

        # Clear the whole widget (letterbox bars) to black.
        f.glClearColor(0.0, 0.0, 0.0, 1.0)
        f.glClear(GL_COLOR_BUFFER_BIT)

        # Powered off → dark tube (leave the cleared black frame).
        if not self._powered:
            return

        if self._program is None or self._texture is None:
            return

        if self._pending_upload:
            self._upload_texture()
            self._pending_upload = False

        # Compute the largest 4:3 rectangle that fits the widget (device px).
        dpr = self.devicePixelRatioF()
        vw_full = max(1, int(round(self.width() * dpr)))
        vh_full = max(1, int(round(self.height() * dpr)))
        if vw_full / vh_full > DISPLAY_ASPECT:
            vh = vh_full
            vw = int(round(vh * DISPLAY_ASPECT))
        else:
            vw = vw_full
            vh = int(round(vw / DISPLAY_ASPECT))
        vx = (vw_full - vw) // 2
        vy = (vh_full - vh) // 2
        f.glViewport(vx, vy, vw, vh)

        self._program.bind()
        self._set_uniforms(f)

        f.glActiveTexture(GL_TEXTURE0)
        self._texture.bind()
        f.glUniform1i(self._program.uniformLocation("uScreen"), 0)

        if self._vao is not None:
            self._vao.bind()
        f.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4)
        if self._vao is not None:
            self._vao.release()

        self._texture.release()
        self._program.release()

    def _set_uniforms(self, f):
        """Set all shader uniforms via the typed glUniform* calls.

        NB: QOpenGLShaderProgram.setUniformValue(loc, <python float>) resolves to
        the *int* overload in PySide6 (glUniform1i) — a GL error on a float
        uniform, which then silently stays 0.  Using the explicit glUniform1f /
        glUniform2f / glUniform3f functions avoids that overload trap.
        """
        prog = self._program
        p = self.params

        def u(name):
            return prog.uniformLocation(name)

        f.glUniform2f(u("uResolution"), float(FB_WIDTH), float(FB_HEIGHT))
        f.glUniform3f(u("uPhosphorOn"), *[float(c) for c in p.phosphor_on])
        f.glUniform3f(u("uPhosphorOff"), *[float(c) for c in p.phosphor_off])
        f.glUniform1f(u("uBrightness"), float(p.brightness))
        f.glUniform1f(u("uContrast"), float(p.contrast))
        f.glUniform1f(u("uScanline"), float(p.scanline_strength))
        f.glUniform1f(u("uScanlineCount"), float(p.scanline_count))
        f.glUniform1f(u("uGlow"), float(p.glow_strength))
        f.glUniform1f(u("uGlowRadius"), float(p.glow_radius))
        f.glUniform1f(u("uMask"), float(p.mask_strength))
        f.glUniform1f(u("uMaskPitch"), float(p.mask_pitch))
        f.glUniform2f(u("uCurvature"), float(p.curvature[0]), float(p.curvature[1]))
        f.glUniform1f(u("uCorner"), float(p.corner_radius))
        f.glUniform1f(u("uVignette"), float(p.vignette_strength))
        f.glUniform1f(u("uFlicker"), float(p.flicker_strength))
        f.glUniform1f(u("uTime"), float(self._clock.elapsed() / 1000.0))
        f.glUniform2f(u("uScale"), float(p.scale_x), float(p.scale_y))
        f.glUniform2f(u("uOffset"), float(p.offset_x), float(p.offset_y))


class StatusWidget(QWidget):
    """Status bar showing emulator state."""

    def __init__(self, parent=None):
        """Initialize status widget."""
        super().__init__(parent)
        from PySide6.QtWidgets import QHBoxLayout, QLabel

        layout = QHBoxLayout(self)

        self.cycles_label = QLabel("Cycles: 0")
        self.fps_label = QLabel("FPS: 0")
        self.disk_label = QLabel("Drives: ----")

        layout.addWidget(self.cycles_label)
        layout.addWidget(self.fps_label)
        layout.addWidget(self.disk_label, 1)

        self.setStyleSheet("background-color: #f0f0f0; padding: 2px;")

    def update_status(self, cycles: int, fps: float, disk_states: str):
        """Update status display."""
        self.cycles_label.setText(f"Cycles: {cycles:,}")
        self.fps_label.setText(f"FPS: {fps:.1f}")
        self.disk_label.setText(f"Drives: {disk_states}")
