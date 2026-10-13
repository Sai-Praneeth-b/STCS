import logging


def configure(path: str) -> logging.Logger:
    logger = logging.getLogger("stcs")
    logger.setLevel(logging.INFO)
    if not logger.handlers:
        handler = logging.FileHandler(path)
        handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
        logger.addHandler(handler)
    return logger
