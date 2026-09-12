# Interim wiring (goal D7): the master sample-data generator, built standalone so
# the offline classroom bundle can ship it as bin/sicnu_generate_samples.
# Qt-free (GDAL only) so it configures on every lane without the QGIS stack.
# Goal D1 replaces this file with the sample-foundry version — same executable
# target name (sicnu_generate_samples), so bundle scripts and consumers do not
# drift when the foundry lands.
add_executable(sicnu_generate_samples generate_sample_data.cpp)
target_link_libraries(sicnu_generate_samples PRIVATE GDAL::GDAL)
