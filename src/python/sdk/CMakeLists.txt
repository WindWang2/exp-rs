###############################################################################
# src/python/sdk/CMakeLists.txt — ExpRS Python SDK (`exprs` package)
#
# Installed to share/sicnu_geo_rs/python/exprs — the worker daemon adds that
# directory to sys.path so plugin modules can `import exprs` inside the
# isolated Python runtime. It is also pip-installable by copying the
# directory (setup-free: pure stdlib).
###############################################################################

install( DIRECTORY ${CMAKE_SOURCE_DIR}/src/python/sdk/exprs
  DESTINATION share/sicnu_geo_rs/python
  FILES_MATCHING PATTERN "*.py"
  PATTERN "__pycache__" EXCLUDE
)
