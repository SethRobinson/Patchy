"""Runs inside Krita (kritarunner -s testy_krita_export -f main <in> <out> <report>).

`krita --export` writes the file as soon as the document is loaded, while fill and
vector layers are still being rendered by Krita's update scheduler, so the same PSD
came out drawn on one run and blank on the next. This opens the document, waits for
the image to settle, and only then exports. The verdict goes to the report file,
because kritarunner's exit code does not follow the script's.

kritarunner finds this module through the working directory (drivers/krita.py starts
it here); PYTHONPATH is replaced by Krita's own.
"""

from krita import InfoObject, Krita


def _settle(document):
    """Wait until the composited image stops changing. waitForDone covers the update
    scheduler; text shapes and fill layers can still land a moment later (the first
    of three exports of one file differed), so the projection is polled until two
    consecutive reads match, for at most about six seconds."""
    from PyQt5.QtCore import QThread
    from PyQt5.QtWidgets import QApplication

    previous = None
    for _attempt in range(20):
        document.waitForDone()
        document.refreshProjection()
        document.waitForDone()
        QApplication.processEvents()
        current = bytes(document.pixelData(0, 0, document.width(), document.height()))
        if previous is not None and current == previous:
            return
        previous = current
        QThread.msleep(300)


# Dialogs that offer a choice with a default already picked, by the class name Krita
# reports for them, and what accepting that default means. Seen on an OpenRaster file
# whose layers are 16-bit PNGs with no profile: the dialog waits with one OK button.
_DEFAULTS = {"KisDlgPngImport": "the default color profile for a PNG that has none"}


def _dismiss_questions(asked):
    """Answer what Krita asks while it works. Batch mode does not silence everything: a
    file layer whose linked file is missing asks where the file is, a PNG without a
    profile asks which one to use, and the process then waits for an answer nobody is
    there to give. A message box is answered "no"; a dialog in _DEFAULTS is accepted as
    it stands. The timer fires inside the dialog's own event loop; what was answered is
    kept for the driver's notes."""
    from PyQt5.QtCore import QTimer
    from PyQt5.QtWidgets import QApplication, QMessageBox

    def dismiss():
        box = QApplication.activeModalWidget()
        if box is None:
            return
        default = _DEFAULTS.get(box.metaObject().className())
        if default:
            asked.append("given " + default)
            box.accept()
        elif isinstance(box, QMessageBox):  # never a progress dialog: closing one cancels the work
            asked.append("answered no: " + " ".join(str(box.text()).split())[:300])
            box.reject()  # the box's own "no" or "cancel"

    timer = QTimer()
    timer.timeout.connect(dismiss)
    timer.start(250)
    return timer


def main(arguments):
    source, output, report = arguments[0], arguments[1], arguments[2]
    verdict = "failed: did not finish"
    asked = []
    try:
        timer = _dismiss_questions(asked)  # kept alive for the whole run: export can ask too
        application = Krita.instance()
        application.setBatchmode(True)
        document = application.openDocument(source)
        if document is None:
            verdict = "failed: open"
        else:
            document.setBatchmode(True)
            _settle(document)
            exported = document.exportImage(output, InfoObject())
            verdict = "ok" if exported else "failed: export"
            document.close()
        timer.stop()
    except Exception as error:  # the report is the only way out of this process
        verdict = f"failed: {error}"
    if asked:
        # Beside the verdict, which stays one word for the driver to compare.
        with open(report + ".asked", "w", encoding="utf-8") as handle:
            handle.write("\n".join(dict.fromkeys(asked)))
    with open(report, "w", encoding="utf-8") as handle:
        handle.write(verdict)
