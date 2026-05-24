import os
import logging
import threading

# Unified global thread lock for all C-level geospatial library access 
# (GDAL, rasterio, fiona, pyproj, PROJ, GEOS/shapely).
# All these libraries share underlying C state or contexts that are not 
# fully multi-threaded safe in a Python/Qt environment.
geospatial_lock = threading.RLock()

# Compatibility aliases
gdal_lock = geospatial_lock
pyproj_lock = geospatial_lock

# Centralized Logger for RS Studio
log_file = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "rs_studio.log")

# Create logger
logger = logging.getLogger("RSStudio")
logger.setLevel(logging.DEBUG)

# Formatters
formatter = logging.Formatter('%(asctime)s - %(levelname)s - [%(filename)s:%(lineno)d] - %(message)s')

# File handler
file_handler = logging.FileHandler(log_file, encoding='utf-8')
file_handler.setLevel(logging.DEBUG)
file_handler.setFormatter(formatter)

# Console handler
console_handler = logging.StreamHandler()
console_handler.setLevel(logging.INFO)
console_handler.setFormatter(formatter)

# Add handlers
logger.addHandler(file_handler)
logger.addHandler(console_handler)

def log_info(msg):
    logger.info(msg, stacklevel=2)

def log_error(msg):
    logger.error(msg, stacklevel=2)

def log_debug(msg):
    logger.debug(msg, stacklevel=2)

def log_warning(msg):
    logger.warning(msg, stacklevel=2)
