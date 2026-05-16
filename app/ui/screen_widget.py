"""
K1520 Emulator - Screen Display Widget
========================================

Qt6 widget for rendering the A5120 display with authentic green phosphor
color. Updates at 50 Hz to match the original hardware.

The display is 80×24 characters, with each character being 8×12 pixels,
resulting in a 640×288 pixel framebuffer.
"""

from PySide6.QtWidgets import QWidget
from PySide6.QtGui import QImage, QPixmap, QPainter, QColor, QFont
from PySide6.QtCore import Qt, QTimer, QSize
from typing import Optional


class ScreenWidget(QWidget):
    """Display widget for K1520 framebuffer."""
    
    # Display parameters
    COLS = 80
    ROWS = 24
    CHAR_WIDTH = 8
    CHAR_HEIGHT = 12
    WIDTH = COLS * CHAR_WIDTH    # 640
    HEIGHT = ROWS * CHAR_HEIGHT  # 288
    
    # Color scheme (authentic green phosphor)
    COLOR_OFF = QColor(0, 20, 0)        # Very dark green
    COLOR_ON = QColor(0, 255, 0)        # Bright green
    COLOR_BG = QColor(0, 0, 0)          # Black background
    
    def __init__(self, parent=None):
        """Initialize screen widget."""
        super().__init__(parent)
        
        self.emulator = None
        self.framebuffer = bytearray(640 * 288)
        self.image = QImage(self.WIDTH, self.HEIGHT, QImage.Format_RGB32)
        self.image.fill(self.COLOR_BG)
        self.pixmap = QPixmap.fromImage(self.image)
        
        # Update timer (50 Hz like original hardware)
        self.timer = QTimer(self)
        self.timer.timeout.connect(self._on_update)
        self.timer.setInterval(20)  # 20ms = 50 Hz
        
        self.setMinimumSize(self.WIDTH, self.HEIGHT)
        self.setMaximumSize(self.WIDTH, self.HEIGHT)
    
    def set_emulator(self, emulator):
        """Connect to emulator."""
        self.emulator = emulator
    
    def start_display(self):
        """Start display updates."""
        self.timer.start()
    
    def stop_display(self):
        """Stop display updates."""
        self.timer.stop()
    
    def _on_update(self):
        """Update display from framebuffer."""
        if not self.emulator:
            return
        
        if not self.emulator.is_framebuffer_dirty():
            return
        
        # Get latest framebuffer
        self.framebuffer = self.emulator.get_framebuffer()
        self.emulator.clear_framebuffer_dirty_flag()
        
        # Render to image
        self._render_framebuffer()
        self.update()
    
    def _render_framebuffer(self):
        """Render framebuffer to QImage."""
        # Each byte is 8 pixels (1 bit per pixel)
        # Layout: 80 columns × 24 rows × 8 bits per byte
        
        for byte_idx in range(len(self.framebuffer)):
            byte_val = self.framebuffer[byte_idx]
            
            # Convert byte index to pixel coordinates
            x_start = (byte_idx % 80) * 8
            y = (byte_idx // 80)
            
            # Process each bit
            for bit in range(8):
                pixel_on = (byte_val >> (7 - bit)) & 1
                x = x_start + bit
                
                if y < self.HEIGHT and x < self.WIDTH:
                    color = self.COLOR_ON if pixel_on else self.COLOR_OFF
                    self.image.setPixelColor(x, y, color)
        
        self.pixmap = QPixmap.fromImage(self.image)
    
    def paintEvent(self, event):
        """Paint framebuffer to widget."""
        painter = QPainter(self)
        painter.drawPixmap(0, 0, self.pixmap)


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
