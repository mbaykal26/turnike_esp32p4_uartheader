"""
mola.py — Break reminder: plays a Turkish voice message at 15:30, twice.

Message : "Mola vakti, dışarı çıkın ve en az 5 dakika boyunca yürüyün"
Schedule: 15:30 daily (runs once then exits — use Task Scheduler for daily repeat)
Repeats : 2 times with a 2-second pause between plays

Usage:
    python mola.py

Dependencies:
    pip install gtts pygame
"""

import datetime
import io
import os
import sys
import tempfile
import time

# ── Configuration ────────────────────────────────────────────────────
MESSAGE          = "Mola vakti, dışarı çıkın ve en az 5 dakika boyunca yürüyün"
TRIGGER_HOUR     = 15
TRIGGER_MINUTE   = 32
REPEAT_COUNT     = 2
PAUSE_BETWEEN_S  = 2      # seconds between the two plays


# ── TTS generation ───────────────────────────────────────────────────

def generate_mp3(text: str, lang: str = "tr") -> bytes:
    """Generate Turkish TTS and return raw MP3 bytes."""
    try:
        from gtts import gTTS
    except ImportError:
        print("ERROR: gtts not installed.  Run: pip install gtts")
        sys.exit(1)

    buf = io.BytesIO()
    gTTS(text, lang=lang).write_to_fp(buf)
    buf.seek(0)
    return buf.read()


# ── Audio playback ───────────────────────────────────────────────────

def init_pygame() -> None:
    """Initialise pygame mixer once — called at startup."""
    try:
        import pygame
        pygame.mixer.init()
    except ImportError:
        print("ERROR: pygame not installed.  Run: pip install pygame")
        sys.exit(1)


def play_mp3_file(path: str) -> None:
    """Play an MP3 file synchronously (blocks until finished)."""
    import pygame
    pygame.mixer.music.load(path)
    pygame.mixer.music.play()
    while pygame.mixer.music.get_busy():
        pygame.time.wait(100)
    pygame.mixer.music.stop()


# ── Scheduling ───────────────────────────────────────────────────────

def seconds_until(hour: int, minute: int) -> float:
    """Return seconds until the next occurrence of HH:MM (today or tomorrow)."""
    now    = datetime.datetime.now()
    target = now.replace(hour=hour, minute=minute, second=0, microsecond=0)
    if target <= now:
        target += datetime.timedelta(days=1)
    return (target - now).total_seconds()


# ── Main ─────────────────────────────────────────────────────────────

def main() -> None:
    print("Break reminder")
    print(f"  Message  : {MESSAGE}")
    print(f"  Trigger  : {TRIGGER_HOUR:02d}:{TRIGGER_MINUTE:02d}")
    print(f"  Repeats  : {REPEAT_COUNT}× with {PAUSE_BETWEEN_S} s pause")
    print()

    # Pre-generate audio now so there is zero network delay at trigger time.
    print("Pre-generating TTS audio (requires internet)...")
    mp3_data = generate_mp3(MESSAGE)
    print(f"  Audio ready ({len(mp3_data)} bytes)")
    print()

    # Initialise playback engine before the sleep so startup errors surface early.
    init_pygame()

    # Save MP3 to a temp file once; reuse for both plays.
    tmp_fd, tmp_path = tempfile.mkstemp(suffix=".mp3")
    try:
        os.write(tmp_fd, mp3_data)
        os.close(tmp_fd)

        wait_s = seconds_until(TRIGGER_HOUR, TRIGGER_MINUTE)
        fire_at = datetime.datetime.now() + datetime.timedelta(seconds=wait_s)
        print(f"Waiting until {fire_at.strftime('%H:%M:%S')}  "
              f"({wait_s / 60:.1f} min from now)...")
        time.sleep(wait_s)

        print(f"[{datetime.datetime.now().strftime('%H:%M:%S')}] "
              f"Playing break reminder...")

        for i in range(REPEAT_COUNT):
            play_mp3_file(tmp_path)
            if i < REPEAT_COUNT - 1:
                print(f"  Pausing {PAUSE_BETWEEN_S} s...")
                time.sleep(PAUSE_BETWEEN_S)

        print("Done.")

    finally:
        # Always clean up the temp file, even if playback raises an exception.
        try:
            os.unlink(tmp_path)
        except OSError:
            pass

        import pygame
        pygame.mixer.quit()


if __name__ == "__main__":
    main()
