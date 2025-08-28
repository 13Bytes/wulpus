import os

from fastapi import HTTPException


def ensure_dir(dir: str) -> None:
    os.makedirs(dir, exist_ok=True)


def check_if_filereq_is_legitimate(req_name: str, system_dir: str, allowed_ending: str) -> str:
    """ Check if the requested file seems plausible.

    Raise HTTPExceptions if invalid.

    Returns:
        str: The validated file path.
    """
    if os.path.sep in req_name or (os.path.altsep and os.path.altsep in req_name) or len(req_name) > 100:
        raise HTTPException(status_code=400, detail="Invalid req_name")
    if not req_name.lower().endswith(allowed_ending):
        raise HTTPException(status_code=400, detail="Invalid file type")
    path = os.path.join(system_dir, req_name)
    if not os.path.isfile(path):
        raise HTTPException(status_code=404, detail="File not found")
    return path
