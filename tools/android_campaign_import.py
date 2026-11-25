"""Run the bundled import recipe inside the Android Mods worker."""
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO

from import_campaign import main


def run(arguments):
    log = StringIO()
    with redirect_stdout(log), redirect_stderr(log):
        # The native Mods provider validates the receipt's resources before
        # adopting either the stages or music.
        result = main(arguments, validate_on_install=True)
    if not result:
        return None
    error = next((line.split(":", 1)[1] for line in reversed(log.getvalue().splitlines())
                  if line.startswith("S3K_CD_ERROR:")), None)
    return error or "The selected media could not be converted."
